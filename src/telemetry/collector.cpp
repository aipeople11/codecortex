// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "collector.h"
#include "project_identity.h"

#include "infra/jsonesc.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace rw::telemetry
{
namespace
{

std::string jsonString( std::string_view value )
{
    std::string escaped;
    escaped.reserve( value.size() + 8 );
    rw::jsonesc::escapeInto( value, escaped, true, true, true );
    return '"' + escaped + '"';
}

std::uint64_t fnv1a64( std::string_view value, std::uint64_t seed = 1469598103934665603ULL ) noexcept
{
    std::uint64_t h = seed;
    for( const unsigned char c : value )
    {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string makeRunId( int argc, char** argv )
{
    if( const char* explicitId = std::getenv( "CODECORTEX_RUN_ID" ); explicitId && *explicitId )
    {
        return explicitId;
    }

    std::uint64_t h = fnv1a64( std::to_string( unixTimeMs() ) );
#if defined(_WIN32)
    const auto pid = static_cast<unsigned long long>( _getpid() );
#else
    const auto pid = static_cast<unsigned long long>( getpid() );
#endif
    h = fnv1a64( std::to_string( pid ), h );
    for( int i = 0; i < argc; ++i )
    {
        if( argv[i] ) h = fnv1a64( argv[i], h );
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string out = "ccx-";
    out.resize( 4 + 16 );
    for( int i = 0; i < 16; ++i )
    {
        out[ 4 + i ] = hex[ ( h >> ( ( 15 - i ) * 4 ) ) & 0xF ];
    }
    return out;
}

std::string commandSummary( int argc, char** argv )
{
    std::string out;
    for( int i = 1; i < argc; ++i )
    {
        if( !argv[i] ) continue;
        std::string_view arg = argv[i];
        // Never persist known secret-bearing flags. More secrets can still exist in arbitrary task text,
        // so telemetry is opt-in and should be treated as developer-local diagnostic data.
        if( arg.starts_with( "--mcp-token=" ) )
        {
            arg = "--mcp-token=<redacted>";
        }
        if( !out.empty() ) out.push_back( ' ' );
        out.append( arg );
    }
    return out;
}

std::string serialize( const Event& event )
{
    std::string out;
    out.reserve( 256 + event.target.size() + event.detail.size() );
    out += "{\"schema\":1,\"type\":";
    out += jsonString( eventTypeName( event.type ) );
    out += ",\"run_id\":";
    out += jsonString( event.runId );
    if( !event.projectId.empty() ) { out += ",\"project_id\":"; out += jsonString( event.projectId ); }
    if( !event.projectName.empty() ) { out += ",\"project_name\":"; out += jsonString( event.projectName ); }
    if( !event.repoId.empty() ) { out += ",\"repo_id\":"; out += jsonString( event.repoId ); }
    if( !event.sessionId.empty() ) { out += ",\"session_id\":"; out += jsonString( event.sessionId ); }
    if( !event.turnId.empty() ) { out += ",\"turn_id\":"; out += jsonString( event.turnId ); }
    if( !event.toolCallId.empty() ) { out += ",\"tool_call_id\":"; out += jsonString( event.toolCallId ); }
    if( !event.model.empty() ) { out += ",\"model\":"; out += jsonString( event.model ); }
    if( !event.agentId.empty() ) { out += ",\"agent_id\":"; out += jsonString( event.agentId ); }
    if( !event.hostName.empty() ) { out += ",\"host_name\":"; out += jsonString( event.hostName ); }
    if( !event.hostVersion.empty() ) { out += ",\"host_version\":"; out += jsonString( event.hostVersion ); }
    if( !event.connectionId.empty() ) { out += ",\"connection_id\":"; out += jsonString( event.connectionId ); }
    if( !event.source.empty() ) { out += ",\"source\":"; out += jsonString( event.source ); }
    if( !event.workspaceRoot.empty() ) { out += ",\"workspace_root\":"; out += jsonString( event.workspaceRoot ); }
    out += ",\"ts_ms\":" + std::to_string( event.timestampMs );
    out += ",\"success\":";
    out += event.success ? "true" : "false";
    if( !event.target.empty() )
    {
        out += ",\"target\":";
        out += jsonString( event.target );
    }
    if( !event.detail.empty() )
    {
        out += ",\"detail\":";
        out += jsonString( event.detail );
    }
    if( event.durationMs >= 0 ) out += ",\"duration_ms\":" + std::to_string( event.durationMs );
    if( event.inputTokens >= 0 ) out += ",\"input_tokens\":" + std::to_string( event.inputTokens );
    if( event.outputTokens >= 0 ) out += ",\"output_tokens\":" + std::to_string( event.outputTokens );
    if( event.numericValue >= 0 ) out += ",\"value\":" + std::to_string( event.numericValue );
    out += "}\n";
    return out;
}

} // namespace

Collector::Collector( std::string_view outputPath )
{
    if( outputPath.empty() ) return;
    path_.assign( outputPath );
    std::error_code ec;
    const std::filesystem::path p( path_ );
    if( p.has_parent_path() ) std::filesystem::create_directories( p.parent_path(), ec );
    file_ = std::fopen( path_.c_str(), "ab" );
}

Collector::~Collector() { close(); }

Collector::Collector( Collector&& other ) noexcept
{
    std::scoped_lock lock( other.mutex_ );
    file_ = std::exchange( other.file_, nullptr );
    path_ = std::move( other.path_ );
}

Collector& Collector::operator=( Collector&& other ) noexcept
{
    if( this == &other ) return *this;
    std::scoped_lock lock( mutex_, other.mutex_ );
    close();
    file_ = std::exchange( other.file_, nullptr );
    path_ = std::move( other.path_ );
    return *this;
}

void Collector::close() noexcept
{
    if( file_ )
    {
        std::fflush( file_ );
        std::fclose( file_ );
        file_ = nullptr;
    }
}

bool Collector::record( const Event& event ) noexcept
{
    if( !file_ ) return false;
    try
    {
        const std::string line = serialize( event );
        std::scoped_lock lock( mutex_ );
        if( std::fwrite( line.data(), 1, line.size(), file_ ) != line.size() ) return false;
        return std::fflush( file_ ) == 0;
    }
    catch( ... )
    {
        return false;
    }
}

RunSession::RunSession( std::string_view outputPath, int argc, char** argv, std::string_view root )
    : collector_( outputPath ), runId_( makeRunId( argc, argv ) ), started_( std::chrono::steady_clock::now() )
{
    if( !collector_.enabled() ) return;
    Event event;
    event.type = EventType::RunStarted;
    event.runId = runId_;
    const auto identity = rw::project::identify( root );
    event.projectId = identity.projectId;
    event.projectName = identity.projectName;
    event.repoId = identity.repoId;
    event.sessionId = identity.sessionId;
    event.agentId = identity.agentId;
    event.workspaceRoot = identity.root;
    event.timestampMs = unixTimeMs();
    event.target.assign( root );
    event.detail = commandSummary( argc, argv );
    collector_.record( event );
}

RunSession::~RunSession()
{
    if( collector_.enabled() && !finished_ ) finish( 255 );
}

bool RunSession::record( Event event ) noexcept
{
    if( !collector_.enabled() ) return false;
    if( event.runId.empty() ) event.runId = runId_;
    if( event.timestampMs == 0 ) event.timestampMs = unixTimeMs();
    return collector_.record( event );
}

void RunSession::finish( int exitCode ) noexcept
{
    if( finished_ || !collector_.enabled() )
    {
        finished_ = true;
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started_ )
                             .count();
    Event event;
    event.type = EventType::RunFinished;
    event.runId = runId_;
    event.timestampMs = unixTimeMs();
    event.durationMs = elapsed;
    event.numericValue = exitCode;
    event.success = exitCode == 0;
    collector_.record( event );
    finished_ = true;
}


std::string makeIntegrationRunId( std::string_view prefix )
{
    std::uint64_t h = fnv1a64( std::to_string( unixTimeMs() ) );
#if defined(_WIN32)
    const auto pid = static_cast<unsigned long long>( _getpid() );
#else
    const auto pid = static_cast<unsigned long long>( getpid() );
#endif
    h = fnv1a64( std::to_string( pid ), h );
    h = fnv1a64( prefix, h );

    static constexpr char hex[] = "0123456789abcdef";
    std::string out( prefix );
    if( out.empty() ) out = "integration";
    out.push_back( '-' );
    const std::size_t base = out.size();
    out.resize( base + 16 );
    for( int i = 0; i < 16; ++i )
        out[ base + i ] = hex[ ( h >> ( ( 15 - i ) * 4 ) ) & 0xF ];
    return out;
}

bool appendToolCall( std::string_view outputPath, std::string_view runId,
                     std::string_view toolName, std::int64_t durationMs,
                     bool success, std::string_view transport ) noexcept
{
    Collector collector( outputPath );
    if( !collector.enabled() ) return false;
    Event event;
    event.type = EventType::ToolCall;
    event.runId.assign( runId );
    event.target.assign( toolName );
    event.detail.assign( transport );
    event.timestampMs = unixTimeMs();
    event.durationMs = durationMs;
    event.source = "observed";
    event.success = success;
    return collector.record( event );
}

bool appendTokenUsage( std::string_view outputPath, std::string_view runId,
                       std::int64_t inputTokens, std::int64_t outputTokens,
                       std::string_view provider ) noexcept
{
    Collector collector( outputPath );
    if( !collector.enabled() ) return false;
    Event event;
    event.type = EventType::TokenUsage;
    event.runId.assign( runId );
    event.target.assign( provider );
    event.source = provider.empty() ? "host_reported" : "provider_reported";
    event.timestampMs = unixTimeMs();
    event.inputTokens = inputTokens;
    event.outputTokens = outputTokens;
    return collector.record( event );
}

} // namespace rw::telemetry
