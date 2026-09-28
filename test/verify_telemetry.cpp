#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "telemetry/collector.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
std::string readAll( const std::filesystem::path& p )
{
    std::ifstream in( p, std::ios::binary );
    return { std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() };
}
}

TEST_CASE( "telemetry lifecycle is opt-in, append-only JSONL" )
{
    const auto path = std::filesystem::temp_directory_path() / "codecortex-telemetry-test.jsonl";
    std::error_code ec;
    std::filesystem::remove( path, ec );

#if defined(_WIN32)
    _putenv_s( "CODECORTEX_RUN_ID", "unit-run" );
    _putenv_s( "CODECORTEX_REPO_ID", "repo:test" );
    _putenv_s( "CODECORTEX_SESSION_ID", "session:test" );
    _putenv_s( "CODECORTEX_AGENT_ID", "agent:test" );
#else
    setenv( "CODECORTEX_RUN_ID", "unit-run", 1 );
    setenv( "CODECORTEX_REPO_ID", "repo:test", 1 );
    setenv( "CODECORTEX_SESSION_ID", "session:test", 1 );
    setenv( "CODECORTEX_AGENT_ID", "agent:test", 1 );
#endif

    std::vector<std::string> args{ "codecortex", ".", "--for=fix auth", "--mcp-token=do-not-store" };
    std::vector<char*> argv;
    for( auto& a : args ) argv.push_back( a.data() );

    {
        rw::telemetry::RunSession session( path.string(), static_cast<int>( argv.size() ), argv.data(), "." );
        CHECK( session.enabled() );
        CHECK( session.runId() == "unit-run" );
        CHECK( rw::telemetry::appendTokenUsage( path.string(), session.runId(), 123, 45, "test-provider" ) );
        CHECK( rw::telemetry::appendToolCall( path.string(), session.runId(), "code_search", 17, true, "stdio" ) );
        rw::telemetry::Collector collector( path.string() );
        rw::telemetry::Event selected;
        selected.type = rw::telemetry::EventType::TestSelection;
        selected.runId = session.runId();
        selected.hostName = "codex";
        selected.hostVersion = "test";
        selected.connectionId = "conn-test";
        selected.source = "observed";
        selected.target = "required tests";
        selected.detail = "selection_only_not_execution";
        selected.timestampMs = rw::telemetry::unixTimeMs();
        CHECK( collector.record( selected ) );
        rw::telemetry::Event proof = selected;
        proof.type = rw::telemetry::EventType::ProofCheck;
        proof.target = "change proof";
        CHECK( collector.record( proof ) );
        session.finish( 0 );
    }

    const std::string data = readAll( path );
    CHECK( data.find( "\"type\":\"run_started\"" ) != std::string::npos );
    CHECK( data.find( "\"type\":\"token_usage\"" ) != std::string::npos );
    CHECK( data.find( "\"type\":\"tool_call\"" ) != std::string::npos );
    CHECK( data.find( "\"type\":\"test_selection\"" ) != std::string::npos );
    CHECK( data.find( "\"type\":\"proof_check\"" ) != std::string::npos );
    CHECK( data.find( "\"host_name\":\"codex\"" ) != std::string::npos );
    CHECK( data.find( "\"connection_id\":\"conn-test\"" ) != std::string::npos );
    CHECK( data.find( "\"source\":\"observed\"" ) != std::string::npos );
    CHECK( data.find( "\"target\":\"code_search\"" ) != std::string::npos );
    CHECK( data.find( "\"duration_ms\":17" ) != std::string::npos );
    CHECK( data.find( "\"type\":\"run_finished\"" ) != std::string::npos );
    CHECK( data.find( "\"run_id\":\"unit-run\"" ) != std::string::npos );
    CHECK( data.find( "\"project_id\":" ) != std::string::npos );
    CHECK( data.find( "\"project_name\":" ) != std::string::npos );
    CHECK( data.find( "\"repo_id\":" ) != std::string::npos );
    CHECK( data.find( "\"session_id\":" ) != std::string::npos );
    CHECK( data.find( "\"agent_id\":" ) != std::string::npos );
    CHECK( data.find( "\"workspace_root\":" ) != std::string::npos );
    CHECK( data.find( "\"input_tokens\":123" ) != std::string::npos );
    CHECK( data.find( "\"output_tokens\":45" ) != std::string::npos );
    CHECK( data.find( "do-not-store" ) == std::string::npos );
    CHECK( data.find( "--mcp-token=" ) != std::string::npos );
    CHECK( data.find( "redacted" ) != std::string::npos );

    std::filesystem::remove( path, ec );
#if defined(_WIN32)
    _putenv_s( "CODECORTEX_RUN_ID", "" );
    _putenv_s( "CODECORTEX_REPO_ID", "" );
    _putenv_s( "CODECORTEX_SESSION_ID", "" );
    _putenv_s( "CODECORTEX_AGENT_ID", "" );
#else
    unsetenv( "CODECORTEX_RUN_ID" );
    unsetenv( "CODECORTEX_REPO_ID" );
    unsetenv( "CODECORTEX_SESSION_ID" );
    unsetenv( "CODECORTEX_AGENT_ID" );
#endif
}

TEST_CASE( "disabled collector performs no IO" )
{
    rw::telemetry::Collector collector;
    CHECK_FALSE( collector.enabled() );
    rw::telemetry::Event event;
    event.type = rw::telemetry::EventType::Search;
    CHECK_FALSE( collector.record( event ) );
}

TEST_CASE( "integration run ids are namespaced" )
{
    const std::string id = rw::telemetry::makeIntegrationRunId( "mcp" );
    CHECK( id.rfind( "mcp-", 0 ) == 0 );
    CHECK( id.size() > 8 );
}
