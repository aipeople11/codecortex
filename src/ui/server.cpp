// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "server.h"
#include "graph_service.h"

#include "memory/memory_service_client.h"
#include "project_identity.h"
#include "commands/safe_commands.h"
#include "enterprise/edition.h"
#include "evidence/evidence_service.h"
#include "infra/jsonesc.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rw::ui
{
namespace
{
constexpr std::size_t kMaxRequestBytes = 32 * 1024;
constexpr std::size_t kMaxTelemetryBytes = 8 * 1024 * 1024;

std::string jsonString( std::string_view value )
{
    std::string out;
    out.push_back( '"' );
    rw::jsonesc::escapeInto( value, out, true, true, true );
    out.push_back( '"' );
    return out;
}

std::filesystem::path assetsDir( const ServerConfig& cfg )
{
    // Explicit override wins. This is useful for development and for embedding CodeCortex
    // in another launcher that owns its resource layout.
    if( const char* env = std::getenv( "CODECORTEX_UI_ASSETS" ); env && *env ) return env;

    // Installed layout: <prefix>/bin/codecortex + <prefix>/share/codecortex/ui.
    // P2H.2: the previous implementation returned a cwd-relative "ui" directory, so a
    // correctly installed binary worked only when launched from the source checkout. MCP
    // clients normally launch by absolute path from an arbitrary repository, making the
    // dashboard return "UI asset unavailable". Resolve beside the executable first.
    if( !cfg.executable.empty() )
    {
        std::error_code ec;
        std::filesystem::path exe = std::filesystem::absolute( cfg.executable, ec );
        if( !ec )
        {
            const auto installed = exe.parent_path().parent_path() / "share" / "codecortex" / "ui";
            if( std::filesystem::is_regular_file( installed / "index.html", ec ) && !ec ) return installed;
            ec.clear();
        }
    }

#ifdef CODECORTEX_UI_SOURCE_DIR
    {
        std::error_code ec;
        const std::filesystem::path sourceDir( CODECORTEX_UI_SOURCE_DIR );
        if( std::filesystem::is_regular_file( sourceDir / "index.html", ec ) && !ec ) return sourceDir;
    }
#endif

    // Source-tree fallback for an uninstalled development binary. Prefer the analyzed root
    // when it is the CodeCortex checkout, then the current working directory.
    if( !cfg.root.empty() )
    {
        std::error_code ec;
        const auto rootUi = std::filesystem::path( cfg.root ) / "ui";
        if( std::filesystem::is_regular_file( rootUi / "index.html", ec ) && !ec ) return rootUi;
    }
    return "ui";
}

bool readFileBounded( const std::filesystem::path& path, std::size_t cap, std::string& out )
{
    std::error_code ec;
    const auto size = std::filesystem::file_size( path, ec );
    if( ec || size > cap ) return false;
    std::ifstream in( path, std::ios::binary );
    if( !in ) return false;
    out.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
    return true;
}


bool readFileTailBounded( const std::filesystem::path& path, std::size_t cap, std::string& out )
{
    std::error_code ec;
    const auto size = std::filesystem::file_size( path, ec );
    if( ec ) return false;
    std::ifstream in( path, std::ios::binary );
    if( !in ) return false;
    const auto start = size > cap ? static_cast<std::streamoff>( size - cap ) : std::streamoff{0};
    in.seekg( start );
    if( start > 0 )
    {
        std::string discard;
        std::getline( in, discard ); // drop the partial JSONL record at the tail boundary
    }
    out.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
    return true;
}

bool sendAll( int fd, std::string_view data ) noexcept
{
    std::size_t sent = 0;
    while( sent < data.size() )
    {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send( fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL );
#else
        const ssize_t n = ::send( fd, data.data() + sent, data.size() - sent, 0 );
#endif
        if( n <= 0 ) return false;
        sent += static_cast<std::size_t>( n );
    }
    return true;
}

void respond( int fd, const char* status, const char* contentType, std::string_view body )
{
    std::string response;
    response.reserve( body.size() + 256 );
    response += "HTTP/1.1 "; response += status;
    response += "\r\nContent-Type: "; response += contentType;
    response += "\r\nContent-Length: "; response += std::to_string( body.size() );
    response += "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    response += "Content-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'\r\n";
    response += "Connection: close\r\n\r\n";
    response.append( body );
    sendAll( fd, response );
}

std::string telemetryJson( const std::string& path )
{
    if( path.empty() ) return "{\"events\":[],\"source\":null}";
    std::string data;
    if( !readFileTailBounded( path, kMaxTelemetryBytes, data ) )
        return "{\"events\":[],\"source\":" + jsonString( path ) + ",\"available\":false}";

    std::string out = "{\"events\":[";
    bool first = true;
    std::size_t pos = 0;
    while( pos < data.size() )
    {
        const auto end = data.find( '\n', pos );
        const auto stop = end == std::string::npos ? data.size() : end;
        std::string_view line( data.data() + pos, stop - pos );
        while( !line.empty() && ( line.back() == '\r' || line.back() == ' ' || line.back() == '\t' ) ) line.remove_suffix( 1 );
        while( !line.empty() && ( line.front() == ' ' || line.front() == '\t' ) ) line.remove_prefix( 1 );
        if( line.size() >= 2 && line.front() == '{' && line.back() == '}' )
        {
            if( !first ) out.push_back( ',' );
            out.append( line );
            first = false;
        }
        if( end == std::string::npos ) break;
        pos = end + 1;
    }
    out += "],\"source\":" + jsonString( path ) + ",\"available\":true}";
    return out;
}

std::string trimJsonObject( std::string text );

std::string healthJson( const ServerConfig& cfg )
{
    const char* memoryUrl = std::getenv( "CODECORTEX_MEMORY_URL" );
    const bool memoryConfigured = memoryUrl && *memoryUrl;
    rw::memory::Result mem;
    if( memoryConfigured )
    {
        rw::memory::MemoryServiceConfig memoryCfg;
        memoryCfg.baseUrl = memoryUrl;
        if( const char* tok = std::getenv( "CODECORTEX_MEMORY_TOKEN" ); tok && *tok ) memoryCfg.bearerToken = tok;
        memoryCfg.timeoutMs = 400;
        rw::memory::MemoryServiceClient memory( std::move( memoryCfg ) );
        mem = memory.health();
    }
    else
    {
        mem.ok = false;
        mem.status = 0;
        mem.error = "optional memory service not configured";
    }
    const auto profile = rw::enterprise::loadProfile();
    const auto identity = rw::project::identify( cfg.root );
    std::string out = "{\"ok\":true,\"product\":\"CodeCortex\",\"version\":" + jsonString( cfg.version )
                    + ",\"binary\":" + jsonString( cfg.executable )
                    + ",\"root\":" + jsonString( identity.root )
                    + ",\"project_id\":" + jsonString( identity.projectId )
                    + ",\"project_name\":" + jsonString( identity.projectName )
                    + ",\"repo_id\":" + jsonString( identity.repoId )
                    + ",\"session_id\":" + jsonString( identity.sessionId )
                    + ",\"agent_id\":" + jsonString( identity.agentId )
                    + ",\"telemetry\":" + jsonString( cfg.telemetryPath )
                    + ",\"deployment\":{\"local_first\":true,\"mcp_active\":" + std::string( cfg.mcpActive ? "true" : "false" )
                    + ",\"mcp_transport\":" + jsonString( cfg.mcpTransport )
                    + ",\"mcp_local_only\":" + std::string( cfg.mcpLocalOnly ? "true" : "false" )
                    + ",\"dashboard_bind\":\"127.0.0.1\",\"dashboard_port\":" + std::to_string( cfg.port ) + "}"
                    + ",\"capabilities\":{\"code_intelligence\":true,\"graph\":true,\"impact\":true,\"test_selection\":true,\"proof_checks\":true,\"host_observation_conditional\":true,\"token_usage_conditional\":true}"
                    + ",\"edition\":" + jsonString( profile.edition )
                    + ",\"organization\":" + jsonString( profile.organization )
                    + ",\"memory\":{\"configured\":" + std::string( memoryConfigured ? "true" : "false" ) + ",\"ok\":" + std::string( mem.ok ? "true" : "false" );
    if( !mem.error.empty() ) out += ",\"error\":" + jsonString( mem.error );
    if( mem.ok )
    {
        const std::string details = trimJsonObject( mem.body );
        if( !details.empty() ) out += ",\"details\":" + details;
    }
    out += "}}";
    return out;
}

struct ParsedRequest { std::string method; std::string target; std::string query; };

int hexValue( char c ) noexcept
{
    if( c >= '0' && c <= '9' ) return c - '0';
    if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return -1;
}

std::string urlDecode( std::string_view s )
{
    std::string out; out.reserve( s.size() );
    for( std::size_t i = 0; i < s.size(); ++i )
    {
        if( s[i] == '%' && i + 2 < s.size() )
        {
            const int a = hexValue( s[i+1] ), b = hexValue( s[i+2] );
            if( a >= 0 && b >= 0 ) { out.push_back( static_cast<char>( (a << 4) | b ) ); i += 2; continue; }
        }
        out.push_back( s[i] == '+' ? ' ' : s[i] );
    }
    return out;
}

std::string queryParam( std::string_view query, std::string_view name )
{
    std::size_t pos = 0;
    while( pos <= query.size() )
    {
        const auto amp = query.find( '&', pos );
        const auto end = amp == std::string_view::npos ? query.size() : amp;
        const auto part = query.substr( pos, end - pos );
        const auto eq = part.find( '=' );
        const auto key = eq == std::string_view::npos ? part : part.substr( 0, eq );
        if( key == name ) return urlDecode( eq == std::string_view::npos ? std::string_view{} : part.substr( eq + 1 ) );
        if( amp == std::string_view::npos ) break;
        pos = amp + 1;
    }
    return {};
}

std::size_t querySize( std::string_view query, std::string_view name, std::size_t fallback, std::size_t cap )
{
    const std::string value = queryParam( query, name );
    if( value.empty() ) return fallback;
    std::size_t out = 0;
    for( char c : value )
    {
        if( c < '0' || c > '9' ) return fallback;
        const std::size_t d = static_cast<std::size_t>( c - '0' );
        if( out > ( cap - std::min( d, cap ) ) / 10 ) return cap;
        out = out * 10 + d;
        if( out >= cap ) return cap;
    }
    return out;
}

std::string graphRootFor( const ServerConfig& cfg, std::string_view query )
{
    const std::string requested = queryParam( query, "path" );
    if( requested.empty() ) return cfg.root;
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical( std::filesystem::path( requested ), ec );
    if( ec || !std::filesystem::is_directory( canonical, ec ) || ec ) return cfg.root;
    return canonical.string();
}

ParsedRequest parseRequestLine( std::string_view request )
{
    ParsedRequest out;
    const auto lineEnd = request.find( "\r\n" );
    if( lineEnd == std::string_view::npos ) return out;
    const auto line = request.substr( 0, lineEnd );
    const auto sp1 = line.find( ' ' );
    if( sp1 == std::string_view::npos ) return out;
    const auto sp2 = line.find( ' ', sp1 + 1 );
    if( sp2 == std::string_view::npos ) return out;
    out.method.assign( line.substr( 0, sp1 ) );
    std::string_view raw = line.substr( sp1 + 1, sp2 - sp1 - 1 );
    const auto q = raw.find( '?' );
    out.target.assign( q == std::string_view::npos ? raw : raw.substr( 0, q ) );
    if( q != std::string_view::npos ) out.query.assign( raw.substr( q + 1 ) );
    return out;
}

std::string commandJson( const ServerConfig& cfg, std::string_view name, std::string_view target )
{
    const auto plan = rw::commands::buildSafePlan( cfg.executable, cfg.root, name, target );
    if( !plan.ok )
        return "{\"ok\":false,\"error\":" + jsonString( plan.error ) + "}";
    const auto result = rw::commands::execute( plan );
    std::string out = "{\"ok\":" + std::string( result.ok ? "true" : "false" )
                    + ",\"exit_code\":" + std::to_string( result.exitCode )
                    + ",\"timed_out\":" + std::string( result.timedOut ? "true" : "false" )
                    + ",\"stdout\":" + jsonString( result.stdoutText )
                    + ",\"stderr\":" + jsonString( result.stderrText );
    if( !result.error.empty() ) out += ",\"error\":" + jsonString( result.error );
    out += '}';
    return out;
}

std::string trimJsonObject( std::string text )
{
    const auto first = text.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = text.find_last_not_of( " \t\r\n" );
    text = text.substr( first, last - first + 1 );
    if( text.size() < 2 || text.front() != '{' || text.back() != '}' ) return {};
    return text;
}

struct ApiJsonResponse
{
    const char* status = "200 OK";
    std::string body;
};

ApiJsonResponse memoryProxyResponse( std::string_view resource, std::string_view query )
{
    static constexpr std::array<std::string_view, 14> allowed = {
        "sessions", "memories", "lessons", "actions", "frontier", "crystals",
        "audit", "observations", "profile", "replay/sessions", "replay/load",
        "graph/stats", "procedural", "insights"
    };
    bool ok = false;
    for( auto name : allowed ) if( resource == name ) { ok = true; break; }
    if( !ok ) return { "404 Not Found", "{\"ok\":false,\"error\":\"memory resource not allowed\"}" };

    const char* url = std::getenv( "CODECORTEX_MEMORY_URL" );
    if( !url || !*url )
        return { "503 Service Unavailable", "{\"ok\":false,\"available\":false,\"configured\":false,\"error\":\"optional memory service not configured\"}" };

    rw::memory::MemoryServiceConfig memoryCfg;
    memoryCfg.baseUrl = url;
    if( const char* tok = std::getenv( "CODECORTEX_MEMORY_TOKEN" ); tok && *tok ) memoryCfg.bearerToken = tok;
    memoryCfg.timeoutMs = 1200;
    rw::memory::MemoryServiceClient memory( std::move( memoryCfg ) );
    std::string path = rw::memory::servicePath( resource );
    if( !query.empty() ) path += "?" + std::string( query );
    const auto result = memory.get( path );
    if( result.ok ) return { "200 OK", result.body };
    const char* status = result.status == 404 ? "404 Not Found" : "503 Service Unavailable";
    return { status, "{\"ok\":false,\"available\":false,\"configured\":true,\"status\":" + std::to_string( result.status )
         + ",\"error\":" + jsonString( result.error.empty() ? "CodeCortex Memory unavailable" : result.error ) + "}" };
}

const char* graphStatusFor( std::string_view body )
{
    if( body.find( "\"ok\":true" ) != std::string_view::npos ) return "200 OK";
    if( body.find( "is required" ) != std::string_view::npos ) return "400 Bad Request";
    if( body.find( "unavailable" ) != std::string_view::npos ) return "404 Not Found";
    return "503 Service Unavailable";
}

// P2G.4: one thin facade composes the existing static graph, tests and git evidence.

} // namespace

int runServer( const ServerConfig& cfg )
{
    if( cfg.port < 1 || cfg.port > 65535 )
    {
        std::fprintf( stderr, "codecortex: --ui-port must be 1..65535\n" );
        return 1;
    }

    const int listenFd = ::socket( AF_INET, SOCK_STREAM, 0 );
    if( listenFd < 0 ) { std::fprintf( stderr, "codecortex: UI socket failed: %s\n", std::strerror( errno ) ); return 1; }
    int one = 1;
    ::setsockopt( listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof( one ) );
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons( static_cast<std::uint16_t>( cfg.port ) );
    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    if( ::bind( listenFd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) != 0 )
    {
        std::fprintf( stderr, "codecortex: UI bind 127.0.0.1:%d failed: %s\n", cfg.port, std::strerror( errno ) );
        ::close( listenFd ); return 1;
    }
    if( ::listen( listenFd, 16 ) != 0 )
    {
        std::fprintf( stderr, "codecortex: UI listen failed: %s\n", std::strerror( errno ) );
        ::close( listenFd ); return 1;
    }

    const auto assets = assetsDir( cfg );
    rw::evidence::EvidenceService evidence( cfg.telemetryPath );
    evidence.refresh();
    std::fprintf( stderr, "codecortex: Developer Console on http://127.0.0.1:%d (loopback only)\n", cfg.port );

    for( ;; )
    {
        const int fd = ::accept( listenFd, nullptr, nullptr );
        if( fd < 0 ) { if( errno == EINTR ) continue; break; }
        std::string req;
        std::array<char, 8192> buf{};
        while( req.find( "\r\n\r\n" ) == std::string::npos && req.size() < kMaxRequestBytes )
        {
            const ssize_t n = ::recv( fd, buf.data(), buf.size(), 0 );
            if( n <= 0 ) break;
            req.append( buf.data(), static_cast<std::size_t>( n ) );
        }
        const ParsedRequest parsed = parseRequestLine( req );
        const std::string& target = parsed.target;
        if( target.empty() || parsed.method != "GET" ) respond( fd, "400 Bad Request", "text/plain; charset=utf-8", "bad request\n" );
        else if( target == "/api/health" ) respond( fd, "200 OK", "application/json; charset=utf-8", healthJson( cfg ) );
        else if( target == "/api/telemetry" ) respond( fd, "200 OK", "application/json; charset=utf-8", telemetryJson( cfg.telemetryPath ) );
        else if( target == "/api/evidence" )
        {
            evidence.refresh();
            const std::size_t limit = querySize( parsed.query, "limit", 1000, 5000 );
            const std::string cursorText = queryParam( parsed.query, "cursor" );
            const std::size_t cursor = cursorText.empty()
                ? ( evidence.eventCount() > limit ? evidence.eventCount() - limit : 0 )
                : querySize( parsed.query, "cursor", 0, 100000000 );
            respond( fd, "200 OK", "application/json; charset=utf-8", evidence.evidenceJson( limit, cursor ) );
        }
        else if( target == "/api/evidence/runs" )
        {
            evidence.refresh();
            respond( fd, "200 OK", "application/json; charset=utf-8", evidence.runsJson( querySize( parsed.query, "limit", 50, 500 ), querySize( parsed.query, "cursor", 0, 100000000 ) ) );
        }
        else if( target == "/api/evidence/run" )
        {
            evidence.refresh();
            const std::string id = queryParam( parsed.query, "id" );
            if( id.empty() ) respond( fd, "400 Bad Request", "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"id is required\"}" );
            else
            {
                const std::string body = evidence.runJson( id );
                respond( fd, body.find( "\"ok\":true" ) != std::string::npos ? "200 OK" : "404 Not Found", "application/json; charset=utf-8", body );
            }
        }
        else if( target == "/api/productivity" )
        {
            evidence.refresh();
            const std::string body = evidence.productivityJson( queryParam( parsed.query, "id" ) );
            respond( fd, body.find( "\"ok\":true" ) != std::string::npos ? "200 OK" : "404 Not Found", "application/json; charset=utf-8", body );
        }
        else if( target == "/api/causal/run" )
        {
            evidence.refresh();
            const std::string id = queryParam( parsed.query, "id" );
            if( id.empty() ) respond( fd, "400 Bad Request", "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"id is required\"}" );
            else
            {
                const std::string body = evidence.causalJson( id );
                respond( fd, body.find( "\"ok\":true" ) != std::string::npos ? "200 OK" : "404 Not Found", "application/json; charset=utf-8", body );
            }
        }
        else if( target == "/api/efficiency/run" )
        {
            evidence.refresh();
            const std::string id = queryParam( parsed.query, "id" );
            if( id.empty() ) respond( fd, "400 Bad Request", "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"id is required\"}" );
            else
            {
                const std::string body = evidence.efficiencyJson( id );
                respond( fd, body.find( "\"ok\":true" ) != std::string::npos ? "200 OK" : "404 Not Found", "application/json; charset=utf-8", body );
            }
        }
        else if( target.starts_with( "/api/memory/" ) )
        {
            const std::string resource = target.substr( std::string( "/api/memory/" ).size() );
            const auto api = memoryProxyResponse( resource, parsed.query );
            respond( fd, api.status, "application/json; charset=utf-8", api.body );
        }
        else if( target == "/api/graph/symbol" )
        {
            const std::string symbol = queryParam( parsed.query, "target" );
            GraphService graph( { .executable = cfg.executable, .root = graphRootFor( cfg, parsed.query ) } );
            const std::string body = graph.symbol( symbol );
            respond( fd, graphStatusFor( body ), "application/json; charset=utf-8", body );
        }
        else if( target == "/api/graph/architecture" )
        {
            GraphService graph( { .executable = cfg.executable, .root = graphRootFor( cfg, parsed.query ) } );
            const std::string body = graph.architecture();
            respond( fd, graphStatusFor( body ), "application/json; charset=utf-8", body );
        }
        else if( target == "/api/graph/community" )
        {
            GraphService graph( { .executable = cfg.executable, .root = graphRootFor( cfg, parsed.query ) } );
            const std::string body = graph.community( queryParam( parsed.query, "id" ) );
            respond( fd, graphStatusFor( body ), "application/json; charset=utf-8", body );
        }
        else if( target == "/api/graph/path" )
        {
            GraphService graph( { .executable = cfg.executable, .root = graphRootFor( cfg, parsed.query ) } );
            const std::string body = graph.path( queryParam( parsed.query, "from" ), queryParam( parsed.query, "to" ) );
            respond( fd, graphStatusFor( body ), "application/json; charset=utf-8", body );
        }
        else if( target == "/api/graph/dependencies" )
        {
            GraphService graph( { .executable = cfg.executable, .root = graphRootFor( cfg, parsed.query ) } );
            const std::string body = graph.dependencies();
            respond( fd, graphStatusFor( body ), "application/json; charset=utf-8", body );
        }
        else if( target.starts_with( "/api/command/" ) )
        {
            const std::string name = target.substr( std::string( "/api/command/" ).size() );
            const std::string commandTarget = queryParam( parsed.query, "target" );
            respond( fd, "200 OK", "application/json; charset=utf-8", commandJson( cfg, name, commandTarget ) );
        }
        else
        {
            std::filesystem::path file;
            const char* type = "text/plain; charset=utf-8";
            if( target == "/" || target == "/index.html" ) { file = assets / "index.html"; type = "text/html; charset=utf-8"; }
            else if( target == "/app.js" ) { file = assets / "app.js"; type = "application/javascript; charset=utf-8"; }
            else if( target == "/graph.js" ) { file = assets / "graph.js"; type = "application/javascript; charset=utf-8"; }
            else if( target == "/styles.css" ) { file = assets / "styles.css"; type = "text/css; charset=utf-8"; }
            else if( target == "/codecortex-logo-header.png" ) { file = assets / "codecortex-logo-header.png"; type = "image/png"; }
            else if( target == "/codecortex-logo.png" ) { file = assets / "codecortex-logo.png"; type = "image/png"; }
            if( file.empty() ) respond( fd, "404 Not Found", "text/plain; charset=utf-8", "not found\n" );
            else
            {
                std::string body;
                if( !readFileBounded( file, 2 * 1024 * 1024, body ) ) respond( fd, "500 Internal Server Error", "text/plain; charset=utf-8", "UI asset unavailable\n" );
                else respond( fd, "200 OK", type, body );
            }
        }
        ::close( fd );
    }
    ::close( listenFd );
    return 1;
}

} // namespace rw::ui
