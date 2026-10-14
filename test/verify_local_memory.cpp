#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "memory/local_memory.h"

#include <filesystem>

namespace lm = rw::memory::local;

TEST_CASE( "save, reload and recall project memory" )
{
    const auto root = std::filesystem::temp_directory_path() / "codecortex-local-memory-test";
    std::error_code ec; std::filesystem::remove_all( root, ec );
    CHECK( lm::append( root, { 100, "architecture", "Payments retries", "Retries live in src/pay/retry.cpp, not the client", "demo" } ) );
    CHECK( lm::append( root, { 200, "bug", "Login \"timeout\"", "Session cookie expires after 5 min\nfixed in auth.ts", "demo" } ) );
    CHECK( lm::append( root, { 300, "action", "Add retry test", "", "demo" } ) );

    const auto all = lm::load( root );
    REQUIRE( all.size() == 3 );
    CHECK( all[1].title == "Login \"timeout\"" );
    CHECK( all[1].content == "Session cookie expires after 5 min\nfixed in auth.ts" );

    const auto hit = lm::recall( all, "where are payment retries handled", 5 );
    REQUIRE( hit.size() == 1 );                     // whole-word match: "retries" hits entry 1 only
    CHECK( hit[0].title == "Payments retries" );
    CHECK( lm::recall( all, "nothing matches xyz", 5 ).empty() );
    CHECK( lm::recall( all, "", 2 ).size() == 2 );  // empty query = newest
    CHECK( lm::recall( all, "", 2 )[0].tsMs == 300 );
    std::filesystem::remove_all( root, ec );
}
