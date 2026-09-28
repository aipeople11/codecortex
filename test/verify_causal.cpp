#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "causal/causal_graph.h"

TEST_CASE( "causal graph connects failure code history run patch and proof" )
{
    rw::causal::Graph g;
    const auto err = g.upsertNode( rw::causal::NodeType::RuntimeError, "error:refresh-882", "TokenRefreshException" );
    const auto sym = g.upsertNode( rw::causal::NodeType::Symbol, "symbol:src/session/cache.cpp:restore", "SessionCache.restore" );
    const auto commit = g.upsertNode( rw::causal::NodeType::Commit, "commit:a912", "a912" );
    const auto run = g.upsertNode( rw::causal::NodeType::AgentRun, "run:184", "Run #184" );
    const auto patch = g.upsertNode( rw::causal::NodeType::Patch, "patch:184:2", "Patch #2" );
    const auto test = g.upsertNode( rw::causal::NodeType::Test, "test:multi-device", "test_multi_device" );

    CHECK( g.addEdge( err, sym, rw::causal::EdgeType::FailedAt, "trace frame 4" ) );
    CHECK( g.addEdge( sym, commit, rw::causal::EdgeType::ChangedBy ) );
    CHECK( g.addEdge( commit, run, rw::causal::EdgeType::InspectedBy ) );
    CHECK( g.addEdge( run, patch, rw::causal::EdgeType::AttemptedBy ) );
    CHECK( g.addEdge( patch, test, rw::causal::EdgeType::RejectedBecause, "failed" ) );
    CHECK( g.addEdge( patch, test, rw::causal::EdgeType::RejectedBecause, "failed" ) ); // duplicate
    g.finalize();

    CHECK( g.nodes().size() == 6 );
    CHECK( g.edges().size() == 5 );
    REQUIRE( g.find( "symbol:src/session/cache.cpp:restore" ) != nullptr );
    CHECK( g.incoming( sym ).size() == 1 );
    CHECK( g.outgoing( patch ).size() == 1 );

    const std::string json = g.toJson();
    CHECK( json.find( "\"type\":\"FAILED_AT\"" ) != std::string::npos );
    CHECK( json.find( "SessionCache.restore" ) != std::string::npos );
    CHECK( json.find( "\"type\":\"REJECTED_BECAUSE\"" ) != std::string::npos );
}

TEST_CASE( "stable keys deduplicate nodes" )
{
    rw::causal::Graph g;
    const auto a = g.upsertNode( rw::causal::NodeType::Symbol, "symbol:x", "X" );
    const auto b = g.upsertNode( rw::causal::NodeType::Symbol, "symbol:x", "X second label" );
    CHECK( a == b );
    CHECK( g.nodes().size() == 1 );
}
