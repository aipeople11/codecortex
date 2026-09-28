#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ui/server.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
namespace
{
std::string get( int port, const char* path )
{
    for( int attempt = 0; attempt < 40; ++attempt )
    {
        const int fd = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( fd < 0 ) return {};
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        addr.sin_port = htons( static_cast<std::uint16_t>( port ) );
        if( ::connect( fd, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) ) == 0 )
        {
            const std::string req = std::string( "GET " ) + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
            ::send( fd, req.data(), req.size(), 0 );
            std::string out;
            char buf[4096];
            for( ;; )
            {
                const ssize_t n = ::recv( fd, buf, sizeof( buf ), 0 );
                if( n <= 0 ) break;
                out.append( buf, static_cast<std::size_t>( n ) );
            }
            ::close( fd );
            return out;
        }
        ::close( fd );
        std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );
    }
    return {};
}
}
#endif

TEST_CASE( "developer console serves loopback UI and telemetry" )
{
#if defined(_WIN32)
    MESSAGE( "UI fork smoke is POSIX-only; build coverage still compiles the server on supported targets" );
#else
    const int port = 17431;
    const auto telemetry = std::filesystem::temp_directory_path() / "codecortex-ui-test.jsonl";
    {
        std::ofstream out( telemetry );
        out << R"({"schema":1,"type":"run_started","run_id":"ui-run","host_name":"Codex","model":"ui-model","source":"host_reported","ts_ms":1,"success":true})" << '\n';
        out << R"({"schema":1,"type":"tool_call","run_id":"ui-run","target":"code_search","source":"observed","ts_ms":2,"success":true})" << '\n';
        out << R"({"schema":1,"type":"file_read","run_id":"ui-run","target":"src/a.cpp","source":"host_reported","ts_ms":3,"success":true})" << '\n';
        out << R"({"schema":1,"type":"test","run_id":"ui-run","target":"test_a","source":"host_reported","ts_ms":4,"success":true})" << '\n';
        out << R"({"schema":1,"type":"proof_check","run_id":"ui-run","target":"prove_change","source":"observed","ts_ms":5,"success":true})" << '\n';
        out << R"({"schema":1,"type":"token_usage","run_id":"ui-run","target":"provider","source":"provider_reported","ts_ms":6,"input_tokens":100,"output_tokens":20,"success":true})" << '\n';
        out << R"({"schema":1,"type":"run_finished","run_id":"ui-run","source":"host_reported","ts_ms":7,"duration_ms":6,"success":true})" << '\n';
    }
    const pid_t pid = ::fork();
    REQUIRE( pid >= 0 );
    if( pid == 0 )
    {
        rw::ui::ServerConfig cfg;
        cfg.root = ".";
        cfg.telemetryPath = telemetry.string();
        cfg.port = port;
        cfg.executable = "/bin/echo";
        _exit( rw::ui::runServer( cfg ) );
    }

    const std::string page = get( port, "/" );
    const std::string graphJs = get( port, "/graph.js" );
    const std::string appJs = get( port, "/app.js" );
    const std::string logo = get( port, "/codecortex-logo-header.png" );
    const std::string api = get( port, "/api/telemetry" );
    const std::string evidence = get( port, "/api/evidence?limit=100" );
    const std::string evidenceRuns = get( port, "/api/evidence/runs?limit=10" );
    const std::string evidenceRun = get( port, "/api/evidence/run?id=ui-run" );
    const std::string productivity = get( port, "/api/productivity?id=ui-run" );
    const std::string causal = get( port, "/api/causal/run?id=ui-run" );
    const std::string efficiency = get( port, "/api/efficiency/run?id=ui-run" );
    const std::string health = get( port, "/api/health" );
    const std::string command = get( port, "/api/command/doctor" );
    const std::string memorySessions = get( port, "/api/memory/sessions" );
    const std::string memoryGraphStats = get( port, "/api/memory/graph/stats" );
    const std::string memoryProcedural = get( port, "/api/memory/procedural" );
    const std::string memoryInsights = get( port, "/api/memory/insights" );
    const std::string memoryInvalid = get( port, "/api/memory/not-allowed" );
    const std::string symbolGraph = get( port, "/api/graph/symbol?target=SessionManager.refresh" );
    const std::string architecture = get( port, "/api/graph/architecture" );
    const std::string dependencies = get( port, "/api/graph/dependencies" );
    const std::string pathGraph = get( port, "/api/graph/path?from=A&to=B" );
    ::kill( pid, SIGTERM );
    int status = 0;
    ::waitpid( pid, &status, 0 );
    std::error_code ec;
    std::filesystem::remove( telemetry, ec );

    CHECK( page.find( "200 OK" ) != std::string::npos );
    CHECK( page.find( "CodeCortex Developer Console" ) != std::string::npos );
    CHECK( page.find( "Content-Security-Policy" ) != std::string::npos );
    CHECK( page.find( "graph-canvas" ) != std::string::npos );
    CHECK( page.find( "codecortex-logo-header.png" ) != std::string::npos );
    CHECK( page.find( "projectContext" ) != std::string::npos );
    CHECK( page.find( "repoContext" ) != std::string::npos );
    CHECK( page.find( "sessionContext" ) != std::string::npos );
    CHECK( page.find( "data-view=\"sessions\"" ) != std::string::npos );
    CHECK( page.find( "data-view=\"crystals\"" ) != std::string::npos );
    CHECK( page.find( "data-view=\"activity\"" ) != std::string::npos );
    CHECK( page.find( "id=\"productivitySignal\"" ) != std::string::npos );
    CHECK( page.find( "id=\"verificationStatus\"" ) != std::string::npos );
    CHECK( page.find( "id=\"tokensSource\"" ) != std::string::npos );
    CHECK( graphJs.find( "200 OK" ) != std::string::npos );
    CHECK( graphJs.find( "CodeCortexGraph" ) != std::string::npos );
    CHECK( graphJs.find( "MODE_META" ) != std::string::npos );
    CHECK( graphJs.find( "What the coding agent actually did" ) != std::string::npos );
    CHECK( graphJs.find( "memory_recall" ) != std::string::npos );
    CHECK( graphJs.find( "RECALLED" ) != std::string::npos );
    CHECK( graphJs.find( "Live Run" ) != std::string::npos );
    CHECK( graphJs.find( "code:{icon:" ) != std::string::npos );
    CHECK( graphJs.find( "memory:{icon:" ) != std::string::npos );
    CHECK( graphJs.find( "test_selection" ) != std::string::npos );
    CHECK( graphJs.find( "proof_check" ) != std::string::npos );
    CHECK( graphJs.find( "structural" ) != std::string::npos );
    CHECK( graphJs.find( "RUNS_MODEL" ) != std::string::npos );
    CHECK( graphJs.find( "Evidence lenses" ) != std::string::npos );
    CHECK( graphJs.find( "Git churn" ) != std::string::npos );
    CHECK( graphJs.find( "Test coverage" ) != std::string::npos );
    CHECK( appJs.find( "200 OK" ) != std::string::npos );
    CHECK( appJs.find( "runEvidence" ) != std::string::npos );
    CHECK( appJs.find( "PROVIDER-REPORTED" ) != std::string::npos );
    CHECK( appJs.find( "Verification status" ) == std::string::npos ); // UI label stays in HTML; app owns evidence only.
    CHECK( logo.find( "200 OK" ) != std::string::npos );
    CHECK( logo.find( "Content-Type: image/png" ) != std::string::npos );
    CHECK( api.find( "\"run_id\":\"ui-run\"" ) != std::string::npos );
    CHECK( evidence.find( "200 OK" ) != std::string::npos );
    CHECK( evidence.find( "\"source\":\"indexed-jsonl\"" ) != std::string::npos );
    CHECK( evidence.find( "\"run_id\":\"ui-run\"" ) != std::string::npos );
    CHECK( evidenceRuns.find( "200 OK" ) != std::string::npos );
    CHECK( evidenceRuns.find( "\"run_id\":\"ui-run\"" ) != std::string::npos );
    CHECK( evidenceRun.find( "200 OK" ) != std::string::npos );
    CHECK( productivity.find( "200 OK" ) != std::string::npos );
    CHECK( productivity.find( "PROVIDER-REPORTED" ) != std::string::npos );
    CHECK( causal.find( "200 OK" ) != std::string::npos );
    CHECK( causal.find( "RUNS_MODEL" ) != std::string::npos );
    CHECK( efficiency.find( "200 OK" ) != std::string::npos );
    CHECK( efficiency.find( "repeated_read_pct" ) != std::string::npos );
    CHECK( health.find( "\"ok\":true" ) != std::string::npos );
    CHECK( health.find( "\"edition\":\"community\"" ) != std::string::npos );
    CHECK( health.find( "\"project_id\":" ) != std::string::npos );
    CHECK( health.find( "\"project_name\":" ) != std::string::npos );
    CHECK( health.find( "\"repo_id\":" ) != std::string::npos );
    CHECK( command.find( "\"ok\":true" ) != std::string::npos );
    CHECK( command.find( "--doctor" ) != std::string::npos );
    CHECK( memorySessions.find( "503 Service Unavailable" ) != std::string::npos );
    CHECK( memorySessions.find( "\"available\":false" ) != std::string::npos );
    CHECK( memorySessions.find( "\"configured\":false" ) != std::string::npos );
    CHECK( memoryGraphStats.find( "503 Service Unavailable" ) != std::string::npos );
    CHECK( memoryProcedural.find( "503 Service Unavailable" ) != std::string::npos );
    CHECK( memoryInsights.find( "503 Service Unavailable" ) != std::string::npos );
    CHECK( memoryInvalid.find( "404 Not Found" ) != std::string::npos );
    CHECK( memoryInvalid.find( "memory resource not allowed" ) != std::string::npos );
    CHECK( symbolGraph.find( "404 Not Found" ) != std::string::npos );
    CHECK( symbolGraph.find( "\"ok\":false" ) != std::string::npos );
    CHECK( architecture.find( "200 OK" ) != std::string::npos );
    CHECK( architecture.find( "communities_xml" ) != std::string::npos );
    CHECK( dependencies.find( "200 OK" ) != std::string::npos );
    CHECK( dependencies.find( "deps_xml" ) != std::string::npos );
    CHECK( pathGraph.find( "200 OK" ) != std::string::npos );
    CHECK( pathGraph.find( "path_xml" ) != std::string::npos );
#endif
}


TEST_CASE( "developer console resolves installed UI assets beside executable prefix" )
{
#if defined(_WIN32)
    MESSAGE( "UI installed-layout smoke is POSIX-only" );
#else
    const int port = 17432;
    const auto base = std::filesystem::temp_directory_path() / "codecortex-ui-installed-test";
    const auto binDir = base / "bin";
    const auto uiDir = base / "share" / "codecortex" / "ui";
    std::error_code ec;
    std::filesystem::remove_all( base, ec );
    std::filesystem::create_directories( binDir, ec );
    std::filesystem::create_directories( uiDir, ec );
    REQUIRE_FALSE( ec );
    {
        std::ofstream out( uiDir / "index.html" );
        out << "<!doctype html><title>installed-layout-sentinel</title>";
    }

    const pid_t pid = ::fork();
    REQUIRE( pid >= 0 );
    if( pid == 0 )
    {
        unsetenv( "CODECORTEX_UI_ASSETS" );
        rw::ui::ServerConfig cfg;
        cfg.root = ( base / "repo" ).string();
        cfg.port = port;
        cfg.executable = ( binDir / "codecortex" ).string();
        _exit( rw::ui::runServer( cfg ) );
    }

    const std::string page = get( port, "/" );
    ::kill( pid, SIGTERM );
    int status = 0;
    ::waitpid( pid, &status, 0 );
    std::filesystem::remove_all( base, ec );

    CHECK( page.find( "200 OK" ) != std::string::npos );
    CHECK( page.find( "installed-layout-sentinel" ) != std::string::npos );
#endif
}
