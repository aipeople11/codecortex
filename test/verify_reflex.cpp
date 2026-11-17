#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "telemetry/reflex.h"

using rw::telemetry::reflex::Monitor;

TEST_CASE( "retry storm: note on the third failure in a row, then every fifth after" )
{
    Monitor m;
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 1
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 2
    const auto n3 = m.afterCall( "find_symbol", false, 10, false );              // 3
    REQUIRE( n3.has_value() );
    CHECK( n3->code == "retry_storm" );
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 4
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 5
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 6
    CHECK_FALSE( m.afterCall( "find_symbol", false, 10, false ).has_value() );   // 7
    CHECK( m.afterCall( "find_symbol", false, 10, false ).has_value() );         // 8 = 3 + 5
}

TEST_CASE( "a success resets the failure streak and other tools are counted separately" )
{
    Monitor m;
    m.afterCall( "grep", false, 1, false );
    m.afterCall( "grep", false, 1, false );
    CHECK_FALSE( m.afterCall( "impact", false, 1, false ).has_value() );          // different tool
    m.afterCall( "grep", true, 1, false );                                        // reset
    CHECK_FALSE( m.afterCall( "grep", false, 1, false ).has_value() );
    CHECK_FALSE( m.afterCall( "grep", false, 1, false ).has_value() );
    CHECK( m.afterCall( "grep", false, 1, false ).has_value() );                  // three fresh failures
}

TEST_CASE( "no progress: repeated identical answers" )
{
    Monitor m;
    CHECK_FALSE( m.afterCall( "uses", true, 50, true ).has_value() );
    CHECK_FALSE( m.afterCall( "uses", true, 50, true ).has_value() );
    const auto n = m.afterCall( "uses", true, 50, true );
    REQUIRE( n.has_value() );
    CHECK( n->code == "no_progress" );
    m.afterCall( "uses", true, 50, false );                                       // a fresh answer resets it
    CHECK_FALSE( m.afterCall( "uses", true, 50, true ).has_value() );
}

TEST_CASE( "huge output: note once, then a cooldown" )
{
    Monitor m;
    const auto n = m.afterCall( "grep", true, 20000, false );
    REQUIRE( n.has_value() );
    CHECK( n->code == "huge_output" );
    CHECK_FALSE( m.afterCall( "grep", true, 20000, false ).has_value() );         // cooldown
    CHECK_FALSE( m.afterCall( "grep", true, 20000, false ).has_value() );
    CHECK( m.afterCall( "grep", true, 20000, false ).has_value() );               // after 3 calls
}

TEST_CASE( "small successful answers never produce a note (negative control)" )
{
    Monitor m;
    for( int i = 0; i < 50; ++i ) CHECK_FALSE( m.afterCall( "find_symbol", true, 400, false ).has_value() );
}
