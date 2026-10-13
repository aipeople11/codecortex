#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "telemetry/session_meter.h"

#include <string>
#include <vector>

using namespace rw::telemetry::meter;

TEST_CASE( "retry chain: failures chain per tool, a success ends the chain" )
{
    SessionMeter m;
    auto a = m.beginCall( "replace_symbol_body" );
    CHECK( a.attempt == 1 ); CHECK( a.retryOf.empty() );
    m.endCall( "replace_symbol_body", "7", false, a );

    auto b = m.beginCall( "replace_symbol_body" );
    CHECK( b.attempt == 2 ); CHECK( b.retryOf == "7" );
    m.endCall( "replace_symbol_body", "8", false, b );

    auto c = m.beginCall( "replace_symbol_body" );
    CHECK( c.attempt == 3 ); CHECK( c.retryOf == "8" );
    CHECK( m.beginCall( "explore" ).attempt == 1 );   // other tools are unaffected
    m.endCall( "replace_symbol_body", "9", true, c );

    CHECK( m.beginCall( "replace_symbol_body" ).attempt == 1 );
}

TEST_CASE( "repeat guard: 2nd identical answer is suppressed, 3rd is sent in full" )
{
    SessionMeter m;
    CHECK_FALSE( m.checkRepeat( "explore", "{task:x}", "BODY", "1", 900 ).suppress );
    const auto second = m.checkRepeat( "explore", "{task:x}", "BODY", "2", 900 );
    CHECK( second.suppress ); CHECK( second.earlierCallId == "1" ); CHECK( second.tokensNotResent == 900 );
    CHECK_FALSE( m.checkRepeat( "explore", "{task:x}", "BODY", "3", 900 ).suppress );
    CHECK( m.checkRepeat( "explore", "{task:x}", "BODY", "4", 900 ).suppress );   // pointer now names call 3
}

TEST_CASE( "repeat guard: changed answer, changed question, or mutating tool is never suppressed" )
{
    SessionMeter m;
    m.checkRepeat( "explore", "{task:x}", "BODY", "1", 10 );
    CHECK_FALSE( m.checkRepeat( "explore", "{task:x}", "BODY2", "2", 10 ).suppress );
    CHECK_FALSE( m.checkRepeat( "explore", "{task:y}", "BODY", "3", 10 ).suppress );
    m.checkRepeat( "replace_symbol_body", "{s:a}", "ok", "4", 10 );
    CHECK_FALSE( m.checkRepeat( "replace_symbol_body", "{s:a}", "ok", "5", 10 ).suppress );
    CHECK( isMutatingTool( "memory_checkpoint" ) );
    CHECK_FALSE( isMutatingTool( "find_symbol" ) );
}

TEST_CASE( "answer tokens prefer the renderer's own est_tokens header" )
{
    CHECK( estimateAnswerTokens( R"(<map est_tokens=\"1234\" root=\".\">)" ) == 1234 );
    CHECK( estimateAnswerTokens( "<map est_tokens=\"77\">" ) == 77 );
    CHECK( estimateAnswerTokens( std::string( 300, 'x' ) ) == 100 );
}

TEST_CASE( "path candidates: relative paths with an extension, de-duplicated" )
{
    std::vector<std::string> got;
    forEachPathCandidate( "see src/ui/server.cpp:120 and ui/app.js, again src/ui/server.cpp; not /etc/x nor v1.2 nor ../a/b.c",
                          50, [&]( const std::string& p ) { got.push_back( p ); } );
    CHECK( got == std::vector<std::string>{ "src/ui/server.cpp", "ui/app.js" } );
}
