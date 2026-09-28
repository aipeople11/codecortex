// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Native host-observation and hook-configuration helper for CodeCortex.
// This deliberately keeps the public runtime dependency-free: Codex/Claude hooks
// invoke `codecortex observe --host <name>` directly instead of requiring Python.

#include "mcpjson.h"
#include "telemetry/collector.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rw::hostobserve
{
namespace fs = std::filesystem;

inline std::string readAllStdin()
{
    std::string out;
    char buf[8192];
    while( std::cin.good() )
    {
        std::cin.read( buf, sizeof( buf ) );
        out.append( buf, static_cast<std::size_t>( std::cin.gcount() ) );
    }
    return out;
}

inline std::string readFile( const fs::path& p )
{
    std::ifstream in( p, std::ios::binary );
    if( !in ) return {};
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

inline bool writeFileAtomic( const fs::path& p, std::string_view data )
{
    std::error_code ec;
    if( p.has_parent_path() ) fs::create_directories( p.parent_path(), ec );
    const fs::path tmp = p.string() + ".codecortex.tmp";
    {
        std::ofstream out( tmp, std::ios::binary | std::ios::trunc );
        if( !out ) return false;
        out.write( data.data(), static_cast<std::streamsize>( data.size() ) );
        out.flush();
        if( !out ) return false;
    }
    fs::rename( tmp, p, ec );
    if( ec )
    {
        fs::remove( p, ec ); ec.clear();
        fs::rename( tmp, p, ec );
    }
    return !ec;
}

inline std::uint64_t fnv1a64( std::string_view value, std::uint64_t seed = 1469598103934665603ULL ) noexcept
{
    std::uint64_t h = seed;
    for( const unsigned char c : value ) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

inline std::string hex16( std::uint64_t h )
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out( 16, '0' );
    for( int i = 0; i < 16; ++i ) out[static_cast<std::size_t>(i)] = kHex[( h >> ((15-i)*4) ) & 0xF];
    return out;
}

inline std::string stableKey( std::string_view value ) { return hex16( fnv1a64( value ) ); }

inline std::string jsonString( std::string_view s )
{
    std::string out; out.reserve( s.size() + 8 );
    rw::jsonesc::escapeInto( s, out, true, true, true );
    return '"' + out + '"';
}

inline std::string envOr( const char* name, std::string fallback = {} )
{
    if( const char* v = std::getenv( name ); v && *v ) return v;
    return fallback;
}

inline fs::path homeDir()
{
#if defined(_WIN32)
    return fs::path( envOr( "USERPROFILE", envOr( "HOMEDRIVE" ) + envOr( "HOMEPATH" ) ) );
#else
    return fs::path( envOr( "HOME" ) );
#endif
}

inline fs::path telemetryPath( std::string_view cwd )
{
    const std::string explicitPath = envOr( "CODECORTEX_TELEMETRY_FILE" );
    if( !explicitPath.empty() ) return fs::path( explicitPath );
    fs::path root = cwd.empty() ? fs::current_path() : fs::path( cwd );
    return root / ".codecortex" / "runs.jsonl";
}

inline fs::path stateDir( std::string_view cwd )
{
    fs::path root = cwd.empty() ? fs::current_path() : fs::path( cwd );
    return root / ".codecortex" / "hook-state";
}

inline std::string firstString( const std::string& j, std::initializer_list<const char*> keys )
{
    for( const char* key : keys )
    {
        std::string v = rw::mcpdetail::findString( j, key );
        if( !v.empty() ) return v;
    }
    return {};
}

inline std::string eventName( const std::string& j ) { return firstString( j, {"hook_event_name","event","hookEventName"} ); }
inline std::string sessionId( const std::string& j ) { return firstString( j, {"session_id","sessionId"} ); }
inline std::string turnId( const std::string& j ) { return firstString( j, {"turn_id","turnId"} ); }
inline std::string toolId( const std::string& j ) { return firstString( j, {"tool_use_id","toolUseId","tool_call_id","toolCallId"} ); }
inline std::string toolName( const std::string& j ) { return firstString( j, {"tool_name","toolName"} ); }

inline std::string currentRun( std::string_view cwd, std::string_view session, std::string_view turn, bool create )
{
    if( !turn.empty() ) return std::string( turn );
    std::error_code ec;
    fs::create_directories( stateDir( cwd ), ec );
    const fs::path p = stateDir( cwd ) / ( "session-" + stableKey( session.empty() ? "anonymous" : session ) + ".txt" );
    if( !create )
    {
        const std::string existing = readFile( p );
        if( !existing.empty() ) return existing;
    }
    const std::string rid = rw::telemetry::makeIntegrationRunId( "ccx-host" );
    (void)writeFileAtomic( p, rid );
    return rid;
}

inline void rememberRun( std::string_view cwd, std::string_view session, std::string_view runId )
{
    if( session.empty() || runId.empty() ) return;
    std::error_code ec; fs::create_directories( stateDir( cwd ), ec );
    (void)writeFileAtomic( stateDir( cwd ) / ( "session-" + stableKey( session ) + ".txt" ), runId );
}

inline fs::path toolStartPath( std::string_view cwd, std::string_view id )
{
    return stateDir( cwd ) / ( "tool-" + stableKey( id ) + ".txt" );
}

inline bool looksLikeTestCommand( std::string_view cmd )
{
    static constexpr std::string_view needles[] = {
        "pytest", "ctest", "cargo test", "go test", "npm test", "npm run test", "pnpm test",
        "yarn test", "bun test", "jest", "vitest", "mvn test", "mvn verify", "gradle test", "./gradlew test"
    };
    std::string lower( cmd );
    std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ){ return static_cast<char>( std::tolower(c) ); } );
    for( auto n : needles ) if( lower.find( n ) != std::string::npos ) return true;
    return lower.find( "python -m pytest" ) != std::string::npos || lower.find( "python3 -m pytest" ) != std::string::npos;
}

inline std::string inputObject( const std::string& payload )
{
    std::string in = rw::mcpdetail::findObject( payload, "tool_input" );
    if( in.empty() ) in = rw::mcpdetail::findObject( payload, "toolInput" );
    return in;
}

inline std::string targetFor( std::string_view tool, const std::string& input )
{
    for( const char* key : {"file_path","filePath","path","target","filename"} )
    {
        std::string v = rw::mcpdetail::findString( input, key );
        if( !v.empty() ) return v.substr( 0, 1000 );
    }
    std::string lower( tool );
    std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ){ return static_cast<char>( std::tolower(c) ); } );
    if( lower == "grep" || lower == "glob" || lower == "search" || lower == "rg" || lower == "find" )
    {
        for( const char* key : {"pattern","query","glob"} )
        {
            std::string v = rw::mcpdetail::findString( input, key );
            if( !v.empty() ) return v.substr( 0, 300 );
        }
    }
    if( lower == "bash" || lower == "shell" || lower == "terminal" || lower == "run_command" || lower == "execute" )
    {
        std::string cmd = firstString( input, {"command","cmd"} );
        return looksLikeTestCommand( cmd ) ? "test_command" : "shell"; // never persist arbitrary shell text
    }
    return std::string( tool.substr( 0, std::min<std::size_t>( tool.size(), 300 ) ) );
}

inline std::optional<rw::telemetry::EventType> semanticType( std::string_view tool, const std::string& input )
{
    std::string lower( tool );
    std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ){ return static_cast<char>( std::tolower(c) ); } );
    if( lower == "read" || lower == "readfile" || lower == "read_file" || lower == "view" ) return rw::telemetry::EventType::FileRead;
    if( lower == "edit" || lower == "write" || lower == "multiedit" || lower == "notebookedit" || lower == "apply_patch" || lower == "write_file" || lower == "replace" ) return rw::telemetry::EventType::Edit;
    if( lower == "grep" || lower == "glob" || lower == "search" || lower == "rg" || lower == "find" ) return rw::telemetry::EventType::Search;
    if( lower == "bash" || lower == "shell" || lower == "terminal" || lower == "run_command" || lower == "execute" )
    {
        if( looksLikeTestCommand( firstString( input, {"command","cmd"} ) ) ) return rw::telemetry::EventType::Test;
    }
    return std::nullopt;
}

inline rw::telemetry::Event baseEvent( const std::string& payload, std::string_view host, std::string_view runId,
                                       rw::telemetry::EventType type )
{
    rw::telemetry::Event ev;
    ev.type = type;
    ev.runId = std::string( runId );
    ev.sessionId = sessionId( payload );
    ev.turnId = turnId( payload );
    ev.hostName = std::string( host );
    ev.source = "host_reported";
    ev.workspaceRoot = firstString( payload, {"cwd"} );
    if( ev.workspaceRoot.empty() ) ev.workspaceRoot = fs::current_path().string();
    ev.timestampMs = rw::telemetry::unixTimeMs();
    ev.toolCallId = toolId( payload );
    ev.model = firstString( payload, {"model"} );
    return ev;
}

inline int observe( int argc, char** argv )
{
    std::string host = "unknown";
    for( int i = 2; i < argc; ++i )
    {
        const std::string_view a = argv[i] ? argv[i] : "";
        if( a == "--host" && i + 1 < argc ) host = argv[++i];
        else if( a.starts_with("--host=") ) host = std::string( a.substr(7) );
    }
    const std::string payload = readAllStdin();
    if( payload.empty() ) return 0;
    const std::string evt = eventName( payload );
    std::string cwd = firstString( payload, {"cwd"} );
    if( cwd.empty() ) cwd = fs::current_path().string();
    const std::string sid = sessionId( payload );
    const std::string tid = turnId( payload );
    const fs::path outPath = telemetryPath( cwd );
    rw::telemetry::Collector collector( outPath.string() );
    if( !collector.enabled() ) return 0; // observation must never block the host

    if( evt == "UserPromptSubmit" )
    {
        const std::string run = currentRun( cwd, sid, tid, true ); rememberRun( cwd, sid, run );
        auto ev = baseEvent( payload, host, run, rw::telemetry::EventType::RunStarted );
        ev.target = cwd; ev.detail = "host_turn_started"; collector.record( ev ); return 0;
    }
    const std::string run = currentRun( cwd, sid, tid, false );
    if( evt == "SessionStart" ) return 0;

    if( evt == "PreToolUse" )
    {
        const std::string id = toolId( payload );
        if( !id.empty() ) { std::error_code ec; fs::create_directories( stateDir(cwd), ec ); (void)writeFileAtomic( toolStartPath(cwd,id), std::to_string(rw::telemetry::unixTimeMs()) ); }
        return 0;
    }
    if( evt == "PostToolUse" || evt == "PostToolUseFailure" )
    {
        const bool ok = evt != "PostToolUseFailure";
        const std::string name = toolName( payload );
        const std::string inp = inputObject( payload );
        std::int64_t duration = -1;
        const std::string id = toolId( payload );
        if( !id.empty() )
        {
            const fs::path sp = toolStartPath( cwd, id ); const std::string s = readFile(sp);
            if( !s.empty() ) { try { duration = rw::telemetry::unixTimeMs() - std::stoll(s); } catch(...) {} }
            std::error_code ec; fs::remove( sp, ec );
        }
        auto toolEv = baseEvent( payload, host, run, rw::telemetry::EventType::ToolCall );
        toolEv.target = name; toolEv.detail = "host_tool"; toolEv.durationMs = duration; toolEv.success = ok; collector.record( toolEv );
        if( const auto st = semanticType( name, inp ) )
        {
            auto ev = baseEvent( payload, host, run, *st ); ev.target = targetFor(name, inp); ev.durationMs = duration; ev.success = ok; collector.record( ev );
        }
        return 0;
    }
    if( evt == "Stop" || evt == "Interrupt" || evt == "SessionEnd" )
    {
        auto ev = baseEvent( payload, host, run, rw::telemetry::EventType::RunFinished );
        ev.target = cwd; ev.detail = evt == "Interrupt" ? "host_interrupted" : "host_turn_finished"; ev.success = evt != "Interrupt"; collector.record( ev );
        return 0;
    }
    return 0;
}

inline std::size_t jsonValueEnd( const std::string& s, std::size_t at )
{
    if( at >= s.size() ) return at;
    if( s[at] == '"' ) { auto q = rw::mcpdetail::stringEnd(s,at); return q == std::string::npos ? s.size() : q + 1; }
    if( s[at] == '{' ) { auto span = rw::mcpdetail::containerSpanAt(s,at,'{','}'); return span.empty() ? s.size() : at + span.size(); }
    if( s[at] == '[' ) { auto span = rw::mcpdetail::containerSpanAt(s,at,'[',']'); return span.empty() ? s.size() : at + span.size(); }
    std::size_t p = at; while( p < s.size() && s[p] != ',' && s[p] != '}' ) ++p; return p;
}

inline bool objectSet( std::string& object, std::string_view key, std::string_view rawValue )
{
    if( rw::mcpdetail::checkFrame(object).shape != rw::mcpdetail::FrameShape::Object ) return false;
    const std::size_t at = rw::mcpdetail::findKeyValuePos( object, key );
    if( at != std::string::npos )
    {
        const std::size_t end = jsonValueEnd( object, at );
        object.replace( at, end - at, rawValue ); return true;
    }
    const std::size_t close = object.find_last_of('}'); if( close == std::string::npos ) return false;
    std::size_t p = close; while( p > 0 && std::isspace(static_cast<unsigned char>(object[p-1])) ) --p;
    const bool empty = p > 0 && object[p-1] == '{';
    std::string insert = empty ? "" : ",";
    insert += "\n  " + jsonString(key) + ": " + std::string(rawValue) + "\n";
    object.insert( close, insert ); return true;
}

inline std::string cleanedHookArray( std::string arr, std::string_view command )
{
    std::vector<std::string> keep;
    if( !arr.empty() )
    {
        for( auto& elem : rw::mcpdetail::arrayTopLevelElements(arr) )
        {
            const bool ours = elem.find("codecortex-observe.py") != std::string::npos ||
                              elem.find("codecortex-nudge.sh") != std::string::npos ||
                              elem.find("codecortex-codex-nudge.sh") != std::string::npos ||
                              elem.find("codecortex-claude-route.sh") != std::string::npos ||
                              elem.find("codecortex-codex-route.sh") != std::string::npos ||
                              elem.find("codecortex observe --host") != std::string::npos;
            if( !ours ) keep.push_back( elem );
        }
    }
    keep.push_back( "{\"matcher\":\".*\",\"hooks\":[{\"type\":\"command\",\"command\":" + jsonString(command) + ",\"timeout\":3}]}" );
    std::string out = "[";
    for( std::size_t i = 0; i < keep.size(); ++i ) { if(i) out += ','; out += keep[i]; }
    out += ']'; return out;
}

inline std::string quoteShellCommandPath( std::string_view p )
{
    std::string out = "\"";
    for( char c : p ) { if( c == '\\' || c == '"' ) out.push_back('\\'); out.push_back(c); }
    out += "\""; return out;
}

inline int configureHooks( int argc, char** argv, std::string_view executablePath )
{
    std::string host; fs::path settings;
    for( int i = 2; i < argc; ++i )
    {
        const std::string_view a = argv[i] ? argv[i] : "";
        if( a == "--host" && i + 1 < argc ) host = argv[++i];
        else if( a.starts_with("--host=") ) host = std::string(a.substr(7));
        else if( a == "--settings" && i + 1 < argc ) settings = argv[++i];
        else if( a.starts_with("--settings=") ) settings = std::string(a.substr(11));
    }
    if( host != "codex" && host != "claude" ) { std::fputs("codecortex configure-hooks: --host must be codex or claude\n", stderr); return 2; }
    if( settings.empty() )
    {
        if( host == "codex" ) settings = fs::path(envOr("CODEX_HOME", (homeDir()/".codex").string())) / "hooks.json";
        else settings = fs::path(envOr("CLAUDE_CONFIG_DIR", (homeDir()/".claude").string())) / "settings.json";
    }
    std::string doc = readFile(settings); if( doc.empty() ) doc = "{}";
    {
        const std::string marker = " observe --host " + host;
        std::size_t count = 0, pos = 0;
        while( ( pos = doc.find( marker, pos ) ) != std::string::npos ) { ++count; pos += marker.size(); }
        const bool hasLegacy = doc.find("codecortex-observe.py") != std::string::npos || doc.find("codecortex-route") != std::string::npos || doc.find("codecortex-nudge") != std::string::npos;
        if( count >= 6 && !hasLegacy )
        {
            std::fprintf(stdout,"CodeCortex: %s observation hooks already ready in %s\n",host.c_str(),settings.string().c_str());
            return 0;
        }
    }
    if( rw::mcpdetail::checkFrame(doc).shape != rw::mcpdetail::FrameShape::Object )
    { std::fprintf(stderr,"codecortex configure-hooks: cannot parse %s as a JSON object\n",settings.string().c_str()); return 1; }
    std::string hooks = rw::mcpdetail::findObject(doc,"hooks"); if( hooks.empty() ) hooks = "{}";
    const std::string command = quoteShellCommandPath(executablePath) + " observe --host " + host;
    std::vector<std::string> events = {"SessionStart","UserPromptSubmit","PreToolUse","PostToolUse","Stop"};
    if( host == "claude" ) events.push_back("PostToolUseFailure");
    if( host == "codex" ) events.push_back("Interrupt");
    for( const auto& event : events )
    {
        const std::string existing = rw::mcpdetail::findArray(hooks,event.c_str());
        if( !objectSet(hooks,event,cleanedHookArray(existing,command)) ) return 1;
    }
    if( !objectSet(doc,"hooks",hooks) || !writeFileAtomic(settings,doc + (doc.ends_with('\n') ? "" : "\n")) )
    { std::fprintf(stderr,"codecortex configure-hooks: cannot write %s\n",settings.string().c_str()); return 1; }
    std::fprintf(stdout,"CodeCortex: %s observation hooks ready in %s\n",host.c_str(),settings.string().c_str());
    return 0;
}

} // namespace rw::hostobserve
