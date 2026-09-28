// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "memory_service_client.h"

#include "infra/jsonesc.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rw::memory
{
namespace
{

struct ParsedUrl
{
    std::string host;
    std::string port;
    std::string prefix;
};

std::string jsonString( std::string_view value )
{
    std::string out;
    out.push_back( '"' );
    rw::jsonesc::escapeInto( value, out, true, true, true );
    out.push_back( '"' );
    return out;
}

bool parseLocalHttpUrl( std::string_view url, ParsedUrl& out, std::string& error )
{
    constexpr std::string_view scheme = "http://";
    if( !url.starts_with( scheme ) )
    {
        error = "CodeCortex Memory adapter currently accepts local http:// URLs only";
        return false;
    }
    url.remove_prefix( scheme.size() );
    const std::size_t slash = url.find( '/' );
    std::string_view authority = slash == std::string_view::npos ? url : url.substr( 0, slash );
    out.prefix = slash == std::string_view::npos ? "" : std::string( url.substr( slash ) );

    std::string_view host;
    std::string_view port = "3111";
    if( authority.starts_with( '[' ) )
    {
        const auto close = authority.find( ']' );
        if( close == std::string_view::npos ) { error = "invalid IPv6 CodeCortex Memory URL"; return false; }
        host = authority.substr( 1, close - 1 );
        if( close + 1 < authority.size() )
        {
            if( authority[ close + 1 ] != ':' ) { error = "invalid CodeCortex Memory URL authority"; return false; }
            port = authority.substr( close + 2 );
        }
    }
    else
    {
        const auto colon = authority.rfind( ':' );
        if( colon != std::string_view::npos )
        {
            host = authority.substr( 0, colon );
            port = authority.substr( colon + 1 );
        }
        else host = authority;
    }

    if( host != "127.0.0.1" && host != "localhost" && host != "::1" )
    {
        error = "CodeCortex Memory adapter refuses non-loopback endpoints in P2B";
        return false;
    }
    if( port.empty() ) { error = "CodeCortex Memory URL has an empty port"; return false; }
    out.host.assign( host );
    out.port.assign( port );
    return true;
}

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
void closeSocket( Socket s ) { if( s != kInvalidSocket ) closesocket( s ); }
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
void closeSocket( Socket s ) { if( s >= 0 ) ::close( s ); }
#endif

class SocketGuard
{
  public:
    explicit SocketGuard( Socket s = kInvalidSocket ) : socket_( s ) {}
    ~SocketGuard() { closeSocket( socket_ ); }
    SocketGuard( const SocketGuard& ) = delete;
    SocketGuard& operator=( const SocketGuard& ) = delete;
    [[nodiscard]] Socket get() const noexcept { return socket_; }
    void reset( Socket s ) { closeSocket( socket_ ); socket_ = s; }
  private:
    Socket socket_;
};

bool setTimeouts( Socket s, int timeoutMs )
{
#if defined(_WIN32)
    DWORD ms = static_cast<DWORD>( std::max( 1, timeoutMs ) );
    return setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>( &ms ), sizeof( ms ) ) == 0
        && setsockopt( s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>( &ms ), sizeof( ms ) ) == 0;
#else
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = ( timeoutMs % 1000 ) * 1000;
    return setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) ) == 0
        && setsockopt( s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof( tv ) ) == 0;
#endif
}

bool sendAll( Socket s, std::string_view bytes )
{
    std::size_t offset = 0;
    while( offset < bytes.size() )
    {
#if defined(_WIN32)
        const int n = ::send( s, bytes.data() + offset, static_cast<int>( bytes.size() - offset ), 0 );
#else
        const ssize_t n = ::send( s, bytes.data() + offset, bytes.size() - offset, 0 );
#endif
        if( n <= 0 ) return false;
        offset += static_cast<std::size_t>( n );
    }
    return true;
}

std::string decodeChunked( std::string_view body )
{
    std::string out;
    std::size_t pos = 0;
    while( pos < body.size() )
    {
        const auto lineEnd = body.find( "\r\n", pos );
        if( lineEnd == std::string_view::npos ) return {};
        std::string_view sizeText = body.substr( pos, lineEnd - pos );
        if( const auto semi = sizeText.find( ';' ); semi != std::string_view::npos ) sizeText = sizeText.substr( 0, semi );
        std::size_t chunkSize = 0;
        const auto [ ptr, ec ] = std::from_chars( sizeText.data(), sizeText.data() + sizeText.size(), chunkSize, 16 );
        if( ec != std::errc{} || ptr != sizeText.data() + sizeText.size() ) return {};
        pos = lineEnd + 2;
        if( chunkSize == 0 ) return out;
        if( pos + chunkSize + 2 > body.size() ) return {};
        out.append( body.substr( pos, chunkSize ) );
        pos += chunkSize;
        if( body.substr( pos, 2 ) != "\r\n" ) return {};
        pos += 2;
    }
    return {};
}

Result parseHttpResponse( std::string response )
{
    Result result;
    const auto headerEnd = response.find( "\r\n\r\n" );
    if( headerEnd == std::string::npos )
    {
        result.error = "invalid HTTP response from CodeCortex Memory";
        return result;
    }
    const auto statusEnd = response.find( "\r\n" );
    if( statusEnd == std::string::npos ) { result.error = "missing HTTP status"; return result; }
    const std::string_view statusLine( response.data(), statusEnd );
    const auto firstSpace = statusLine.find( ' ' );
    if( firstSpace == std::string_view::npos || firstSpace + 4 > statusLine.size() )
    {
        result.error = "invalid HTTP status from CodeCortex Memory";
        return result;
    }
    int status = 0;
    const auto [ ptr, ec ] = std::from_chars( statusLine.data() + firstSpace + 1, statusLine.data() + firstSpace + 4, status );
    if( ec != std::errc{} ) { result.error = "invalid HTTP status code"; return result; }
    result.status = status;

    std::string_view headers( response.data(), headerEnd );
    std::string body = response.substr( headerEnd + 4 );
    std::string lowerHeaders( headers );
    std::transform( lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(), []( unsigned char c ){ return static_cast<char>( std::tolower( c ) ); } );
    if( lowerHeaders.find( "transfer-encoding: chunked" ) != std::string::npos )
    {
        std::string decoded = decodeChunked( body );
        if( decoded.empty() && body.find( "0\r\n" ) == std::string::npos )
        {
            result.error = "invalid chunked response from CodeCortex Memory";
            return result;
        }
        body = std::move( decoded );
    }
    result.body = std::move( body );
    result.ok = status >= 200 && status < 300;
    if( !result.ok ) result.error = "CodeCortex Memory HTTP status " + std::to_string( status );
    return result;
}

} // namespace

MemoryServiceClient::MemoryServiceClient( MemoryServiceConfig config ) : config_( std::move( config ) ) {}

Result MemoryServiceClient::request( std::string_view method, std::string_view path, std::string_view body )
{
    ParsedUrl url;
    std::string error;
    if( !parseLocalHttpUrl( config_.baseUrl, url, error ) ) return { false, 0, {}, std::move( error ) };

#if defined(_WIN32)
    WSADATA data{};
    if( WSAStartup( MAKEWORD( 2, 2 ), &data ) != 0 ) return { false, 0, {}, "WSAStartup failed" };
    struct WsaCleanup { ~WsaCleanup(){ WSACleanup(); } } cleanup;
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if( getaddrinfo( url.host.c_str(), url.port.c_str(), &hints, &addresses ) != 0 )
        return { false, 0, {}, "cannot resolve CodeCortex Memory loopback endpoint" };
    struct AddrGuard { addrinfo* p; ~AddrGuard(){ if( p ) freeaddrinfo( p ); } } guard{ addresses };

    SocketGuard socket;
    for( addrinfo* ai = addresses; ai; ai = ai->ai_next )
    {
        Socket candidate = ::socket( ai->ai_family, ai->ai_socktype, ai->ai_protocol );
        if( candidate == kInvalidSocket ) continue;
        setTimeouts( candidate, config_.timeoutMs );
        if( ::connect( candidate, ai->ai_addr, static_cast<socklen_t>( ai->ai_addrlen ) ) == 0 )
        {
            socket.reset( candidate );
            break;
        }
        closeSocket( candidate );
    }
    if( socket.get() == kInvalidSocket ) return { false, 0, {}, "cannot connect to CodeCortex Memory at " + config_.baseUrl };

    std::string request;
    const std::string fullPath = url.prefix + std::string( path );
    request.reserve( 256 + body.size() + config_.bearerToken.size() );
    request += method;
    request += ' ';
    request += fullPath.empty() ? "/" : fullPath;
    request += " HTTP/1.1\r\nHost: ";
    request += url.host;
    request += ':';
    request += url.port;
    request += "\r\nAccept: application/json\r\nConnection: close\r\n";
    if( !config_.bearerToken.empty() )
    {
        request += "Authorization: Bearer ";
        request += config_.bearerToken;
        request += "\r\n";
    }
    if( method == "POST" )
    {
        request += "Content-Type: application/json\r\nContent-Length: ";
        request += std::to_string( body.size() );
        request += "\r\n";
    }
    request += "\r\n";
    request += body;
    if( !sendAll( socket.get(), request ) ) return { false, 0, {}, "failed sending request to CodeCortex Memory" };

    std::string response;
    std::array<char, 8192> buffer{};
    constexpr std::size_t kMaxResponseBytes = 8 * 1024 * 1024;
    for( ;; )
    {
#if defined(_WIN32)
        const int n = ::recv( socket.get(), buffer.data(), static_cast<int>( buffer.size() ), 0 );
#else
        const ssize_t n = ::recv( socket.get(), buffer.data(), buffer.size(), 0 );
#endif
        if( n == 0 ) break;
        if( n < 0 ) return { false, 0, {}, "failed receiving CodeCortex Memory response" };
        if( response.size() + static_cast<std::size_t>( n ) > kMaxResponseBytes )
            return { false, 0, {}, "CodeCortex Memory response exceeded 8 MiB safety cap" };
        response.append( buffer.data(), static_cast<std::size_t>( n ) );
    }
    return parseHttpResponse( std::move( response ) );
}

Result MemoryServiceClient::health()
{
    return request( "GET", servicePath( "health" ) );
}

Result MemoryServiceClient::recall( const RecallQuery& query )
{
    std::string body = "{\"query\":" + jsonString( query.query ) + ",\"limit\":" + std::to_string( query.limit );
    if( !query.project.empty() ) body += ",\"project\":" + jsonString( query.project );
    if( !query.agentId.empty() ) body += ",\"agentId\":" + jsonString( query.agentId );
    body += '}';
    return request( "POST", servicePath( "search" ), body );
}

Result MemoryServiceClient::smartRecall( const RecallQuery& query )
{
    std::string body = "{\"query\":" + jsonString( query.query ) + ",\"limit\":" + std::to_string( query.limit );
    if( !query.project.empty() ) body += ",\"project\":" + jsonString( query.project );
    if( !query.agentId.empty() ) body += ",\"agentId\":" + jsonString( query.agentId );
    if( !query.sessionId.empty() ) body += ",\"sessionId\":" + jsonString( query.sessionId );
    body += ",\"includeLessons\":" + std::string( query.includeLessons ? "true" : "false" );
    body += ",\"source\":\"codecortex\"}";

    Result result = request( "POST", servicePath( "smart-search" ), body );
    // CodeCortex Memory versions before smart-search or constrained deployments can
    // safely fall back to the stable bounded /search endpoint.
    if( result.status == 404 || result.status == 405 ) return recall( query );
    return result;
}

Result MemoryServiceClient::remember( const RememberRequest& requestValue )
{
    std::string body = "{\"content\":" + jsonString( requestValue.content );
    if( !requestValue.type.empty() ) body += ",\"type\":" + jsonString( requestValue.type );
    if( !requestValue.project.empty() ) body += ",\"project\":" + jsonString( requestValue.project );
    if( !requestValue.agentId.empty() ) body += ",\"agentId\":" + jsonString( requestValue.agentId );
    body += '}';
    return request( "POST", servicePath( "remember" ), body );
}

Result MemoryServiceClient::checkpoint( const CheckpointRequest& requestValue )
{
    std::string body = "{\"name\":" + jsonString( requestValue.name );
    if( !requestValue.description.empty() ) body += ",\"description\":" + jsonString( requestValue.description );
    if( !requestValue.type.empty() ) body += ",\"type\":" + jsonString( requestValue.type );
    body += '}';
    return request( "POST", servicePath( "checkpoints" ), body );
}

Result MemoryServiceClient::get( std::string_view path )
{
    return request( "GET", path );
}
} // namespace rw::memory
