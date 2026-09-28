#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "commands/safe_commands.h"

TEST_CASE( "safe command plans never invoke a shell" )
{
    const auto p = rw::commands::buildSafePlan( "/bin/echo", "/repo", "impact", "Foo;rm -rf /" );
    REQUIRE( p.ok );
    REQUIRE( p.argv.size() == 4 );
    CHECK( p.argv[0] == "/bin/echo" );
    CHECK( p.argv[1] == "/repo" );
    CHECK( p.argv[2] == "--impact=Foo;rm -rf /" );
    CHECK( p.argv[3] == "--json" );

    const auto x = rw::commands::execute( p, 1000 );
    CHECK( x.ok );
    CHECK( x.stdoutText.find( "--impact=Foo;rm -rf /" ) != std::string::npos );
}

TEST_CASE( "unknown and missing-target commands are refused" )
{
    CHECK_FALSE( rw::commands::buildSafePlan( "/bin/echo", ".", "shell", "ls" ).ok );
    CHECK_FALSE( rw::commands::buildSafePlan( "/bin/echo", ".", "impact", "" ).ok );
    CHECK( rw::commands::buildSafePlan( "/bin/echo", ".", "doctor" ).ok );
}

TEST_CASE( "graph intelligence safe plans are typed" )
{
    const auto tests = rw::commands::buildSafePlan( "/bin/echo", "/repo", "tests_for_file", "src/graph.h" );
    REQUIRE( tests.ok );
    CHECK( tests.argv[2] == "--test-gate=src/graph.h" );
    CHECK( tests.argv[3] == "--json" );

    const auto community = rw::commands::buildSafePlan( "/bin/echo", "/repo", "community", "42" );
    REQUIRE( community.ok );
    CHECK( community.argv[2] == "--community=42" );
    CHECK_FALSE( rw::commands::buildSafePlan( "/bin/echo", "/repo", "community", "42;rm" ).ok );

    const auto path = rw::commands::buildSafePlan( "/bin/echo", "/repo", "path", "a,b" );
    REQUIRE( path.ok );
    CHECK( path.argv[2] == "--path=a,b" );

    const auto git = rw::commands::buildGitHistoryPlan( "/repo", "src/graph.h", 8 );
    REQUIRE( git.ok );
    CHECK( git.argv[0] == "git" );
    CHECK( git.argv[1] == "-C" );
    CHECK( git.argv[2] == "/repo" );
    CHECK( git.argv[git.argv.size()-2] == "--" );
    CHECK( git.argv.back() == "src/graph.h" );
}
