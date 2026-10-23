#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "telemetry/otlp_json.h"
#include "telemetry/otlp_map.h"

#include <string>
#include <vector>

using namespace rw::telemetry;

namespace
{
// Shaped like Claude Code's OTLP/JSON log export. The prompt/email/tool-input attributes must NEVER survive.
const char* kBody = R"({
 "resourceLogs":[{
  "resource":{"attributes":[{"key":"user.email","value":{"stringValue":"someone@example.com"}},
                            {"key":"service.name","value":{"stringValue":"claude-code"}}]},
  "scopeLogs":[{"logRecords":[
   {"timeUnixNano":"1760000000123000000","body":{"stringValue":"claude_code.api_request"},
    "attributes":[
      {"key":"event.name","value":{"stringValue":"api_request"}},
      {"key":"session.id","value":{"stringValue":"sess-1"}},
      {"key":"prompt.id","value":{"stringValue":"p-1"}},
      {"key":"request_id","value":{"stringValue":"req-1"}},
      {"key":"model","value":{"stringValue":"claude-sonnet-5-5"}},
      {"key":"input_tokens","value":{"intValue":"1200"}},
      {"key":"output_tokens","value":{"intValue":350}},
      {"key":"cache_read_tokens","value":{"intValue":"9000"}},
      {"key":"cache_creation_tokens","value":{"intValue":"400"}},
      {"key":"cost_usd","value":{"doubleValue":0.0123}},
      {"key":"duration_ms","value":{"intValue":"2100"}},
      {"key":"prompt","value":{"stringValue":"SECRET PROMPT TEXT"}},
      {"key":"user.email","value":{"stringValue":"someone@example.com"}},
      {"key":"tool_input","value":{"stringValue":"SECRET TOOL INPUT"}}
    ]},
   {"timeUnixNano":"1760000001000000000","attributes":[{"key":"event.name","value":{"stringValue":"claude_code.user_prompt"}}]}
  ]}]
 }]})";
}

TEST_CASE( "parses api_request and keeps only allow-listed attributes" )
{
    std::vector<otlp::Record> recs;
    REQUIRE( otlp::parseLogs( kBody, recs ) );
    REQUIRE( recs.size() == 2 );
    const auto& r = recs[0];
    CHECK( r.name == "api_request" );
    CHECK( r.tsMs == 1760000000123LL );
    CHECK( r.get( "model" ) == "claude-sonnet-5-5" );
    CHECK( r.get( "input_tokens" ) == "1200" );
    CHECK( r.get( "output_tokens" ) == "350" );
    // privacy: nothing outside the allow-list is ever kept
    CHECK( r.get( "prompt" ).empty() );
    CHECK( r.get( "tool_input" ).empty() );
    CHECK( r.get( "user.email" ).empty() );
    CHECK( r.get( "service.name" ).empty() );
    for( const auto& kv : r.attrs ) CHECK( otlp::isAllowed( kv.first ) );
    CHECK( recs[1].name == "user_prompt" );
}

TEST_CASE( "maps api_request to a host_reported TokenUsage event" )
{
    std::vector<otlp::Record> recs;
    REQUIRE( otlp::parseLogs( kBody, recs ) );
    CHECK_FALSE( otlp::mapRecord( recs[1] ).has_value() );   // user_prompt: not mapped in this slice
    const auto ev = otlp::mapRecord( recs[0] );
    REQUIRE( ev.has_value() );
    CHECK( ev->type == EventType::TokenUsage );
    CHECK( ev->source == "host_reported" );
    CHECK( ev->inputTokens == 1200 );
    CHECK( ev->outputTokens == 350 );
    CHECK( ev->cacheReadTokens == 9000 );
    CHECK( ev->cacheWrite5mTokens == 400 );
    CHECK( ev->costUsdMicros == 12300 );
    CHECK( ev->requestId == "req-1" );
    CHECK( ev->runId == "otel-p-1" );
}

TEST_CASE( "dedupes by request id" )
{
    otlp::Dedupe d;
    CHECK( d.firstTime( "a" ) );
    CHECK_FALSE( d.firstTime( "a" ) );
    CHECK( d.firstTime( "" ) );
    CHECK( d.firstTime( "" ) );
}

TEST_CASE( "rejects malformed input safely" )
{
    std::vector<otlp::Record> recs;
    CHECK_FALSE( otlp::parseLogs( "{not json", recs ) );
    CHECK_FALSE( otlp::parseLogs( "[1,2", recs ) );
    CHECK( otlp::parseLogs( "{}", recs ) );
    CHECK( recs.empty() );
    std::string deep( 100, '[' );
    CHECK_FALSE( otlp::parseLogs( deep, recs ) );
}
