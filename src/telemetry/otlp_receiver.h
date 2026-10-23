// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Local-only OpenTelemetry (OTLP/HTTP JSON) receiver. OFF by default.
//
//  * Listens on 127.0.0.1 only (never a network address). Port: CODECORTEX_OTLP_PORT, default 4318.
//  * Reads Claude Code's own usage reports and writes them to the same local telemetry file the dashboard
//    already reads. Nothing is forwarded, exported or sent anywhere.
//  * Privacy: parsing keeps only the allow-list in otlp_json.h. Prompts, replies, tool input and account
//    identifiers are dropped on read and never stored.
//  * First CodeCortex process to bind the port wins; the others retry every 30 s (several Claude windows can
//    each run a CodeCortex process).
//  * Limits: 1 MB body, 16 live connections, 5 s read timeout. JSON only (protobuf gets 415).
//
// Opt in with the environment variable CODECORTEX_OTLP=1 or the marker file <home>/.codecortex/otlp.enabled
// (written by `codecortex install --host claude --otel`).

#include "otlp_json.h"
#include "otlp_map.h"
#include "collector.h"
#include "infra/os.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <thread>

namespace rw::telemetry::otlp
{

struct ReceiverStats
{
    std::atomic<bool> listening{ false };
    std::atomic<int> port{ 0 };
    std::atomic<std::int64_t> requests{ 0 };       // accepted POST requests
    std::atomic<std::int64_t> events{ 0 };         // api_request events recorded
    std::atomic<std::int64_t> lastEventMs{ 0 };
    std::atomic<std::int64_t> rejected{ 0 };       // bad / oversized / wrong type
};

inline ReceiverStats& stats() { static ReceiverStats s; return s; }

inline bool enabledByEnvOrMarker()
{
    if( const char* v = std::getenv( "CODECORTEX_OTLP" ) ) return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y';
    const char* home = std::getenv( "USERPROFILE" );
    if( !home || !*home ) home = std::getenv( "HOME" );
    if( !home || !*home ) return false;
    std::error_code ec;
    return std::filesystem::exists( std::filesystem::path( home ) / ".codecortex" / "otlp.enabled", ec );
}

namespace detail
{

inline constexpr std::size_t kMaxBody = 1024 * 1024;

inline void sendAll( int fd, const std::string& data )
{
    std::size_t sent = 0;
    while( sent < data.size() )
    {
        const auto n = rw::os::send( fd, data.data() + sent, data.size() - sent, 0 );
        if( n <= 0 ) return;
        sent += static_cast<std::size_t>( n );
    }
}

inline void reply( int fd, const char* status, const std::string& body = "{}" )
{
    std::string r = "HTTP/1.1 ";
    r += status;
    r += "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string( body.size() ) + "\r\nConnection: close\r\n\r\n";
    r += body;
    sendAll( fd, r );
}

inline std::string lower( std::string s )
{
    for( char& c : s ) if( c >= 'A' && c <= 'Z' ) c = static_cast<char>( c - 'A' + 'a' );
    return s;
}

struct Shared
{
    std::string telemetryPath;
    std::mutex mu;                 // guards dedupe + collector
    Dedupe dedupe;
    Collector collector;
    std::atomic<int> live{ 0 };
};

inline void handle( int fd, Shared& sh )
{
    // A stalled client must not hold a slot forever.
#ifdef _WIN32
    unsigned long ms = 5000;
    rw::os::setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof( ms ) );
#else
    timeval tv{ 5, 0 };
    rw::os::setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) );
#endif
    std::string req;
    std::array<char, 8192> buf{};
    std::size_t headerEnd = std::string::npos;
    while( ( headerEnd = req.find( "\r\n\r\n" ) ) == std::string::npos && req.size() < 16384 )
    {
        const auto n = rw::os::recv( fd, buf.data(), buf.size(), 0 );
        if( n <= 0 ) { stats().rejected++; return; }
        req.append( buf.data(), static_cast<std::size_t>( n ) );
    }
    if( headerEnd == std::string::npos ) { stats().rejected++; reply( fd, "431 Request Header Fields Too Large" ); return; }

    const std::string head = lower( req.substr( 0, headerEnd ) );
    if( head.rfind( "post ", 0 ) != 0 ) { stats().rejected++; reply( fd, "405 Method Not Allowed" ); return; }
    const bool isLogs = head.find( " /v1/logs" ) != std::string::npos;
    const bool isMetrics = head.find( " /v1/metrics" ) != std::string::npos;
    if( !isLogs && !isMetrics ) { stats().rejected++; reply( fd, "404 Not Found" ); return; }
    if( head.find( "application/json" ) == std::string::npos ) { stats().rejected++; reply( fd, "415 Unsupported Media Type", "{\"error\":\"send OTLP as JSON: OTEL_EXPORTER_OTLP_PROTOCOL=http/json\"}" ); return; }

    std::size_t length = 0;
    if( const auto p = head.find( "content-length:" ); p != std::string::npos )
        length = static_cast<std::size_t>( std::strtoull( head.c_str() + p + 15, nullptr, 10 ) );
    if( length > kMaxBody ) { stats().rejected++; reply( fd, "413 Payload Too Large" ); return; }

    std::string body = req.substr( headerEnd + 4 );
    while( body.size() < length )
    {
        const auto n = rw::os::recv( fd, buf.data(), buf.size(), 0 );
        if( n <= 0 ) { stats().rejected++; return; }
        body.append( buf.data(), static_cast<std::size_t>( n ) );
        if( body.size() > kMaxBody ) { stats().rejected++; reply( fd, "413 Payload Too Large" ); return; }
    }
    if( body.size() > length ) body.resize( length );
    stats().requests++;

    if( isLogs )
    {
        std::vector<Record> records;
        if( !parseLogs( body, records ) ) { stats().rejected++; reply( fd, "400 Bad Request" ); return; }
        for( const Record& r : records )
        {
            auto ev = mapRecord( r );
            if( !ev ) continue;
            std::lock_guard<std::mutex> lock( sh.mu );
            if( !sh.dedupe.firstTime( ev->requestId ) ) continue;
            if( sh.collector.record( *ev ) )
            {
                stats().events++;
                stats().lastEventMs = ev->timestampMs;
            }
        }
    }
    // Metrics are accepted (so Claude Code does not log errors) but not stored in this first slice.
    reply( fd, "200 OK" );
}

} // namespace detail

// Blocks forever; run on its own detached thread. Retries binding every 30 s.
inline void runReceiver( std::string telemetryPath )
{
    int port = 4318;
    if( const char* p = std::getenv( "CODECORTEX_OTLP_PORT" ) ) { const int v = std::atoi( p ); if( v > 0 && v < 65536 ) port = v; }

    auto sh = std::make_shared<detail::Shared>();
    sh->telemetryPath = std::move( telemetryPath );
    sh->collector = Collector( sh->telemetryPath );

    for( ;; )
    {
        int listenFd = rw::os::socket( AF_INET, SOCK_STREAM, 0 );
        bool bound = false;
        if( listenFd >= 0 )
        {
            int one = 1;
            rw::os::setsockopt( listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof( one ) );
            rw::os::sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons( static_cast<std::uint16_t>( port ) );
            bound = rw::os::inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr ) == 1
                    && rw::os::bind( listenFd, reinterpret_cast<rw::os::sockaddr*>( &addr ), sizeof( addr ) ) == 0
                    && rw::os::listen( listenFd, 16 ) == 0;
            if( !bound ) { rw::os::close( listenFd ); listenFd = -1; }
        }
        if( !bound )
        {
            // Another CodeCortex process already owns the port. Wait and try again (it may exit).
            stats().listening = false;
            std::this_thread::sleep_for( std::chrono::seconds( 30 ) );
            continue;
        }
        stats().port = port;
        stats().listening = true;
        std::fprintf( stderr, "codecortex: OpenTelemetry receiver on http://127.0.0.1:%d (loopback only, local only)\n", port );
        for( ;; )
        {
            const int fd = rw::os::accept( listenFd, nullptr, nullptr );
            if( fd < 0 ) { if( errno == EINTR ) continue; break; }
            if( sh->live.load() >= 16 ) { detail::reply( fd, "503 Service Unavailable" ); rw::os::close( fd ); continue; }
            sh->live++;
            std::thread( [fd, sh] {
                detail::handle( fd, *sh );
                rw::os::close( fd );
                sh->live--;
            } ).detach();
        }
        rw::os::close( listenFd );
        stats().listening = false;
        std::this_thread::sleep_for( std::chrono::seconds( 5 ) );
    }
}

// Starts the receiver on a detached thread, at most once per process (dashboard click and startup both call it).
inline void startOnce( const std::string& telemetryPath )
{
    static std::once_flag flag;
    std::call_once( flag, [&telemetryPath] {
        std::string path = telemetryPath;
        std::thread( [path]() { runReceiver( path ); } ).detach();
    } );
}

} // namespace rw::telemetry::otlp
