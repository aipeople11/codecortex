// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// One-click "exact usage" setup for the dashboard (Tokens & cost page).
//
//   enable : writes ~/.codecortex/otlp.enabled, adds five env settings to Claude Code's settings.json
//            (only keys that are absent) and starts the local receiver in this process, so no restart of
//            CodeCortex is needed. Claude Code itself must be reopened once to pick the settings up.
//   disable: removes the marker and removes ONLY the settings whose value is exactly what we wrote.
//
// Safety rules (docs/OTEL_INTEGRATION_DESIGN.md):
//   - never overwrite an existing OTEL_* / telemetry setting; if one conflicts, change nothing and say why;
//   - stop when IT manages Claude Code's telemetry settings (managed-settings.json);
//   - never set OTEL_LOG_USER_PROMPTS or OTEL_LOG_TOOL_DETAILS;
//   - everything stays on 127.0.0.1.

#include "host_observer.h"
#include "telemetry/otlp_receiver.h"

#include <string>
#include <utility>
#include <vector>

namespace rw::otelsetup
{
namespace fs = std::filesystem;
namespace ho = rw::hostobserve;

inline int receiverPort()
{
    if( const char* p = std::getenv( "CODECORTEX_OTLP_PORT" ) ) { const int v = std::atoi( p ); if( v > 0 && v < 65536 ) return v; }
    return 4318;
}

inline fs::path settingsPath()
{
    return fs::path( ho::envOr( "CLAUDE_CONFIG_DIR", ( ho::homeDir() / ".claude" ).string() ) ) / "settings.json";
}

inline fs::path markerPath() { return ho::homeDir() / ".codecortex" / "otlp.enabled"; }

inline std::vector<std::pair<std::string, std::string>> wanted()
{
    return {
        { "CLAUDE_CODE_ENABLE_TELEMETRY", "1" },
        { "OTEL_LOGS_EXPORTER", "otlp" },
        { "OTEL_METRICS_EXPORTER", "otlp" },
        { "OTEL_EXPORTER_OTLP_PROTOCOL", "http/json" },
        { "OTEL_EXPORTER_OTLP_ENDPOINT", "http://127.0.0.1:" + std::to_string( receiverPort() ) },
    };
}

inline bool managedByIt()
{
    std::vector<fs::path> files;
#if defined( _WIN32 )
    files.emplace_back( "C:\\Program Files\\ClaudeCode\\managed-settings.json" );
#elif defined( __APPLE__ )
    files.emplace_back( "/Library/Application Support/ClaudeCode/managed-settings.json" );
#else
    files.emplace_back( "/etc/claude-code/managed-settings.json" );
#endif
    for( const auto& f : files )
    {
        const std::string doc = ho::readFile( f );
        if( doc.find( "OTEL_" ) != std::string::npos || doc.find( "CLAUDE_CODE_ENABLE_TELEMETRY" ) != std::string::npos ) return true;
    }
    return false;
}

// "" = fine; otherwise a plain-words reason why we must not touch the settings.
inline std::string conflict( const std::string& env )
{
    for( const auto& [k, v] : wanted() )
    {
        const std::string have = rw::mcpdetail::findString( env, k.c_str() );
        if( have.empty() || have == v ) continue;
        if( k == "CLAUDE_CODE_ENABLE_TELEMETRY" ) continue;   // any truthy value already means "on"
        return "Claude Code already has " + k + " set to something else (" + have + "). It can send usage to one place only, so CodeCortex left it alone.";
    }
    return {};
}

inline bool allPresent( const std::string& env )
{
    for( const auto& [k, v] : wanted() )
    {
        const std::string have = rw::mcpdetail::findString( env, k.c_str() );
        if( have != v ) return false;
    }
    return true;
}

inline std::string envOf( const std::string& doc )
{
    return rw::mcpdetail::findObject( doc, "env" );
}

inline std::string boolJson( bool b ) { return b ? "true" : "false"; }

inline std::string statusJson( const std::string& message = {}, bool ok = true )
{
    const std::string doc = ho::readFile( settingsPath() );
    const std::string env = envOf( doc );
    std::error_code ec;
    const bool marker = fs::exists( markerPath(), ec );
    const auto& st = rw::telemetry::otlp::stats();
    std::string out = "{";
    out += "\"ok\":" + boolJson( ok );
    out += ",\"enabled\":" + boolJson( marker );
    out += ",\"configured\":" + boolJson( !env.empty() && allPresent( env ) );
    out += ",\"managed\":" + boolJson( managedByIt() );
    out += ",\"listening\":" + boolJson( st.listening.load() );
    out += ",\"port\":" + std::to_string( st.listening.load() ? st.port.load() : receiverPort() );
    out += ",\"requests\":" + std::to_string( st.requests.load() );
    out += ",\"events\":" + std::to_string( st.events.load() );
    out += ",\"rejected\":" + std::to_string( st.rejected.load() );
    out += ",\"last_event_ms\":" + std::to_string( st.lastEventMs.load() );
    out += ",\"conflict\":" + ho::jsonString( env.empty() ? std::string() : conflict( env ) );
    out += ",\"message\":" + ho::jsonString( message );
    out += "}";
    return out;
}

inline std::string enable( const std::string& telemetryPath )
{
    if( managedByIt() )
        return statusJson( "Your organisation manages Claude Code's telemetry settings, so CodeCortex did not change them. Ask IT to allow a local receiver.", false );

    const fs::path settings = settingsPath();
    std::string doc = ho::readFile( settings );
    if( doc.empty() ) doc = "{}";
    if( rw::mcpdetail::checkFrame( doc ).shape != rw::mcpdetail::FrameShape::Object )
        return statusJson( "Claude Code's settings file could not be read, so nothing was changed.", false );

    std::string env = envOf( doc );
    if( env.empty() ) env = "{}";
    if( const std::string why = conflict( env ); !why.empty() ) return statusJson( why, false );

    for( const auto& [k, v] : wanted() )
    {
        if( !rw::mcpdetail::findString( env, k.c_str() ).empty() ) continue;   // never overwrite
        if( !ho::objectSet( env, k, ho::jsonString( v ) ) )
            return statusJson( "Could not add " + k + " to Claude Code's settings.", false );
    }
    if( !ho::objectSet( doc, "env", env ) || !ho::writeFileAtomic( settings, doc + ( doc.ends_with( '\n' ) ? "" : "\n" ) ) )
        return statusJson( "Could not write Claude Code's settings file.", false );

    std::error_code ec;
    fs::create_directories( markerPath().parent_path(), ec );
    (void)ho::writeFileAtomic( markerPath(), "on\n" );
    rw::telemetry::otlp::startOnce( telemetryPath );
    return statusJson( "Turned on. Reopen Claude Code once; usage will appear here after your next message." );
}

inline std::string disable()
{
    std::error_code ec;
    fs::remove( markerPath(), ec );

    const fs::path settings = settingsPath();
    std::string doc = ho::readFile( settings );
    const std::string env = envOf( doc );
    if( !env.empty() )
    {
        std::string rebuilt = "{";
        bool first = true;
        for( const std::string& key : rw::mcpdetail::objectKeys( env ) )
        {
            bool ours = false;
            for( const auto& [k, v] : wanted() )
                if( k == key && rw::mcpdetail::findString( env, k.c_str() ) == v ) ours = true;
            if( ours ) continue;
            const std::size_t at = rw::mcpdetail::findKeyValuePos( env, key );
            if( at == std::string::npos ) continue;
            const std::size_t end = ho::jsonValueEnd( env, at );
            if( !first ) rebuilt += ",";
            first = false;
            rebuilt += "\n    " + ho::jsonString( key ) + ": " + env.substr( at, end - at );
        }
        rebuilt += first ? "}" : "\n  }";
        if( ho::objectSet( doc, "env", rebuilt ) )
            (void)ho::writeFileAtomic( settings, doc + ( doc.ends_with( '\n' ) ? "" : "\n" ) );
    }
    return statusJson( "Turned off. Reopen Claude Code once. The receiver stops listening the next time CodeCortex restarts." );
}

} // namespace rw::otelsetup
