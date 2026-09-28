#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "evidence/evidence_service.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path fixturePath( const char* name )
{
    return std::filesystem::temp_directory_path() / name;
}
}

TEST_CASE( "evidence service aggregates multiple runs without inventing missing tokens" )
{
    const auto path = fixturePath( "codecortex-evidence-multi.jsonl" );
    {
        std::ofstream out( path );
        out << R"({"schema":1,"type":"run_started","run_id":"r1","host_name":"Codex","model":"m1","source":"host_reported","ts_ms":1,"success":true})" << '\n';
        out << R"({"schema":1,"type":"file_read","run_id":"r1","target":"a.cpp","source":"host_reported","ts_ms":2,"success":true})" << '\n';
        out << R"({"schema":1,"type":"file_read","run_id":"r1","target":"a.cpp","source":"host_reported","ts_ms":3,"success":true})" << '\n';
        out << R"({"schema":1,"type":"tool_call","run_id":"r1","target":"edit","source":"observed","ts_ms":4,"success":false})" << '\n';
        out << R"({"schema":1,"type":"memory_recall","run_id":"r1","target":"lesson-1","source":"observed","ts_ms":5,"success":true})" << '\n';
        out << R"({"schema":1,"type":"test_selection","run_id":"r1","target":"test_a","source":"structural","ts_ms":6,"success":true})" << '\n';
        out << R"({"schema":1,"type":"test","run_id":"r1","target":"test_a","source":"host_reported","ts_ms":7,"success":true})" << '\n';
        out << R"({"schema":1,"type":"proof_check","run_id":"r1","target":"prove_change","source":"observed","ts_ms":8,"success":true})" << '\n';
        out << R"({"schema":1,"type":"run_finished","run_id":"r1","source":"host_reported","ts_ms":9,"duration_ms":8,"success":true})" << '\n';
        out << R"({"schema":1,"type":"run_started","run_id":"r2","host_name":"Claude","model":"m2","source":"host_reported","ts_ms":10,"success":true})" << '\n';
        out << R"({"schema":1,"type":"token_usage","run_id":"r2","target":"provider-x","source":"provider_reported","ts_ms":11,"input_tokens":100,"output_tokens":20,"success":true})" << '\n';
        out << R"({"schema":1,"type":"run_finished","run_id":"r2","source":"host_reported","ts_ms":12,"duration_ms":2,"success":true})" << '\n';
    }

    rw::evidence::EvidenceService service( path.string() );
    REQUIRE( service.refresh() );
    CHECK( service.runCount() == 2 );
    CHECK( service.eventCount() == 12 );

    const auto p1 = service.productivityJson( "r1" );
    CHECK( p1.find( "\"repeated_reads\":{\"available\":true,\"value\":1" ) != std::string::npos );
    CHECK( p1.find( "\"failed_actions\":{\"available\":true,\"value\":1" ) != std::string::npos );
    CHECK( p1.find( "\"memory_recalls\":{\"available\":true,\"value\":1" ) != std::string::npos );
    CHECK( p1.find( "\"tokens_used\":{\"available\":false,\"value\":null,\"provenance\":\"NOT AVAILABLE\"}" ) != std::string::npos );
    CHECK( p1.find( "\"tests_executed\":{\"available\":true,\"value\":1,\"provenance\":\"HOST-REPORTED\"}" ) != std::string::npos );

    const auto p2 = service.productivityJson( "r2" );
    CHECK( p2.find( "\"tokens_used\":{\"available\":true,\"value\":120,\"provenance\":\"PROVIDER-REPORTED\"}" ) != std::string::npos );
    CHECK( p2.find( "\"host\":\"Claude\"" ) != std::string::npos );
    CHECK( p2.find( "\"model\":\"m2\"" ) != std::string::npos );

    const auto causal = service.causalJson( "r1" );
    CHECK( causal.find( "\"type\":\"model\"" ) != std::string::npos );
    CHECK( causal.find( "RUNS_MODEL" ) != std::string::npos );
    CHECK( causal.find( "EXECUTED" ) != std::string::npos );
    CHECK( causal.find( "OBSERVED SEQUENCE; NOT CAUSAL PROOF" ) != std::string::npos );

    const auto efficiency = service.efficiencyJson( "r1" );
    CHECK( efficiency.find( "\"ok\":true" ) != std::string::npos );
    CHECK( efficiency.find( "\"repeated_read_pct\":" ) != std::string::npos );

    std::error_code ec;
    std::filesystem::remove( path, ec );
}

TEST_CASE( "evidence pagination reaches records beyond the legacy eight megabyte telemetry tail" )
{
    const auto path = fixturePath( "codecortex-evidence-large.jsonl" );
    const std::string padding( 1200, 'x' );
    {
        std::ofstream out( path );
        out << R"({"schema":1,"type":"run_started","run_id":"old-run","ts_ms":1,"success":true})" << '\n';
        for( int i = 0; i < 7600; ++i )
            out << "{\"schema\":1,\"type\":\"search\",\"run_id\":\"bulk\",\"target\":\"q" << i << "\",\"detail\":\"" << padding << "\",\"ts_ms\":" << ( i + 2 ) << ",\"success\":true}\n";
        out << R"({"schema":1,"type":"run_finished","run_id":"new-run","ts_ms":9000,"success":true})" << '\n';
    }
    REQUIRE( std::filesystem::file_size( path ) > 8u * 1024u * 1024u );

    rw::evidence::EvidenceService service( path.string() );
    REQUIRE( service.refresh() );
    CHECK( service.eventCount() == 7602 );
    const auto first = service.evidenceJson( 1, 0 );
    CHECK( first.find( "old-run" ) != std::string::npos );
    CHECK( first.find( "\"next_cursor\":1" ) != std::string::npos );
    const auto runs = service.runsJson( 10, 0 );
    CHECK( runs.find( "old-run" ) != std::string::npos );
    CHECK( runs.find( "bulk" ) != std::string::npos );

    std::error_code ec;
    std::filesystem::remove( path, ec );
}

TEST_CASE( "evidence service retains an incomplete JSONL record across refreshes" )
{
    const auto path = fixturePath( "codecortex-evidence-partial.jsonl" );
    {
        std::ofstream out( path );
        out << R"({"schema":1,"type":"run_started","run_id":"partial","ts_ms":1,"success":true})";
    }

    rw::evidence::EvidenceService service( path.string() );
    REQUIRE( service.refresh() );
    CHECK( service.eventCount() == 0 );

    {
        std::ofstream out( path, std::ios::app );
        out << '\n';
    }
    REQUIRE( service.refresh() );
    CHECK( service.eventCount() == 1 );
    CHECK( service.runCount() == 1 );

    std::error_code ec;
    std::filesystem::remove( path, ec );
}
