#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "memory/memory_service_client.h"

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
namespace
{
class MockServer
{
  public:
    MockServer()
    {
        fd_ = ::socket( AF_INET, SOCK_STREAM, 0 );
        REQUIRE( fd_ >= 0 );
        int yes = 1;
        setsockopt( fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof( yes ) );
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        addr.sin_port = 0;
        REQUIRE( ::bind( fd_, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) == 0 );
        REQUIRE( ::listen( fd_, 8 ) == 0 );
        socklen_t len = sizeof( addr );
        REQUIRE( ::getsockname( fd_, reinterpret_cast<sockaddr*>( &addr ), &len ) == 0 );
        port_ = ntohs( addr.sin_port );
        thread_ = std::thread( [this]{ serve(); } );
    }

    ~MockServer()
    {
        stop_.store( true );
        const int wake = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( wake >= 0 )
        {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
            addr.sin_port = htons( port_ );
            ::connect( wake, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) );
            ::close( wake );
        }
        if( thread_.joinable() ) thread_.join();
        if( fd_ >= 0 ) ::close( fd_ );
    }

    unsigned port() const noexcept { return port_; }
    std::vector<std::string> requests() const { return requests_; }

  private:
    void serve()
    {
        while( !stop_.load() )
        {
            const int client = ::accept( fd_, nullptr, nullptr );
            if( client < 0 ) continue;
            std::string req;
            std::array<char, 4096> buf{};
            for( ;; )
            {
                const ssize_t n = ::recv( client, buf.data(), buf.size(), 0 );
                if( n <= 0 ) break;
                req.append( buf.data(), static_cast<std::size_t>( n ) );
                const auto headerEnd = req.find( "\r\n\r\n" );
                if( headerEnd != std::string::npos )
                {
                    std::size_t contentLength = 0;
                    const auto cl = req.find( "Content-Length: " );
                    if( cl != std::string::npos )
                        contentLength = static_cast<std::size_t>( std::stoul( req.substr( cl + 16 ) ) );
                    if( req.size() >= headerEnd + 4 + contentLength ) break;
                }
            }
            if( stop_.load() ) { ::close( client ); break; }
            requests_.push_back( req );
            const std::string body = "{\"ok\":true}";
            const std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                     + std::to_string( body.size() ) + "\r\nConnection: close\r\n\r\n" + body;
            ::send( client, resp.data(), resp.size(), 0 );
            ::close( client );
        }
    }

    int fd_ = -1;
    unsigned port_ = 0;
    std::atomic<bool> stop_{ false };
    std::thread thread_;
    std::vector<std::string> requests_;
};
}
#endif

TEST_CASE( "CodeCortex Memory adapter refuses remote and non-http endpoints" )
{
    rw::memory::MemoryServiceClient remote( { .baseUrl = "http://example.com:3111" } );
    const auto r1 = remote.health();
    CHECK_FALSE( r1.ok );
    CHECK( r1.error.find( "non-loopback" ) != std::string::npos );

    rw::memory::MemoryServiceClient tls( { .baseUrl = "https://127.0.0.1:3111" } );
    const auto r2 = tls.health();
    CHECK_FALSE( r2.ok );
    CHECK( r2.error.find( "http://" ) != std::string::npos );
}

#if !defined(_WIN32)
TEST_CASE( "CodeCortex Memory adapter maps bounded provider operations to REST" )
{
    MockServer server;
    rw::memory::MemoryServiceClient client( {
        .baseUrl = "http://127.0.0.1:" + std::to_string( server.port() ),
        .bearerToken = "secret-token",
        .timeoutMs = 1000
    } );

    CHECK( client.health().ok );
    CHECK( client.recall( { .query = "auth bug", .project = "demo", .agentId = "claude-1", .limit = 3 } ).ok );
    CHECK( client.smartRecall( { .query = "auth bug", .project = "demo", .agentId = "claude-1", .sessionId = "session-7", .includeLessons = true, .limit = 4 } ).ok );
    CHECK( client.remember( { .content = "cache invalidation failed", .project = "demo", .agentId = "claude-1", .type = "bug" } ).ok );
    CHECK( client.checkpoint( { .name = "run-7", .description = "next: inspect restore", .type = "codecortex-run" } ).ok );
    CHECK( client.get( rw::memory::servicePath( "sessions" ) ).ok );

    const auto reqs = server.requests();
    REQUIRE( reqs.size() == 6 );
    CHECK( reqs[0].find( "GET " + rw::memory::servicePath( "health" ) ) != std::string::npos );
    CHECK( reqs[1].find( "POST " + rw::memory::servicePath( "search" ) ) != std::string::npos );
    CHECK( reqs[1].find( "\"query\":\"auth bug\"" ) != std::string::npos );
    CHECK( reqs[1].find( "\"project\":\"demo\"" ) != std::string::npos );
    CHECK( reqs[1].find( "\"agentId\":\"claude-1\"" ) != std::string::npos );
    CHECK( reqs[2].find( "POST " + rw::memory::servicePath( "smart-search" ) ) != std::string::npos );
    CHECK( reqs[2].find( "\"sessionId\":\"session-7\"" ) != std::string::npos );
    CHECK( reqs[2].find( "\"includeLessons\":true" ) != std::string::npos );
    CHECK( reqs[2].find( "\"source\":\"codecortex\"" ) != std::string::npos );
    CHECK( reqs[3].find( "POST " + rw::memory::servicePath( "remember" ) ) != std::string::npos );
    CHECK( reqs[3].find( "\"project\":\"demo\"" ) != std::string::npos );
    CHECK( reqs[3].find( "\"agentId\":\"claude-1\"" ) != std::string::npos );
    CHECK( reqs[3].find( "\"type\":\"bug\"" ) != std::string::npos );
    CHECK( reqs[4].find( "POST " + rw::memory::servicePath( "checkpoints" ) ) != std::string::npos );
    CHECK( reqs[5].find( "GET " + rw::memory::servicePath( "sessions" ) ) != std::string::npos );
    for( const auto& req : reqs ) CHECK( req.find( "Authorization: Bearer secret-token" ) != std::string::npos );
}
#endif
