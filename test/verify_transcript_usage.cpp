#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "telemetry/transcript_usage.h"

#include <string>
#include <vector>

using rw::telemetry::transcript::summarize;
using rw::telemetry::transcript::completeLinesEnd;

namespace
{
// One API response written as two lines (two content blocks) sharing an id; the first carries a partial
// streamed output count, the last the final one. Plus a user line, a synthetic line, and a second message.
const std::string kTranscript =
    R"({"type":"user","message":{"role":"user","content":"please check usage stats"}})" "\n"
    R"({"type":"assistant","message":{"id":"msg_A","model":"claude-opus-5-5","content":[{"type":"text","text":"the \"usage\":{\"input_tokens\":999} trap"}],"usage":{"input_tokens":10,"output_tokens":1,"cache_read_input_tokens":5000,"cache_creation_input_tokens":200}}})" "\n"
    R"({"type":"assistant","message":{"id":"msg_A","model":"claude-opus-5-5","content":[{"type":"tool_use"}],"usage":{"input_tokens":10,"output_tokens":42,"cache_read_input_tokens":5000,"cache_creation_input_tokens":200,"cache_creation":{"ephemeral_5m_input_tokens":150,"ephemeral_1h_input_tokens":50}}}})" "\n"
    R"({"type":"assistant","message":{"id":"msg_S","model":"<synthetic>","usage":{"input_tokens":0,"output_tokens":0}}})" "\n"
    R"({"type":"assistant","message":{"id":"msg_B","model":"claude-haiku-5-5","usage":{"input_tokens":150000,"output_tokens":7}}})" "\n";
}

TEST_CASE( "usage is counted once per message id, from the last line" )
{
    const auto s = summarize( kTranscript, {}, 0 );
    REQUIRE( s.buckets.size() == 2 );
    const auto& a = s.buckets[0];
    CHECK( a.model == "claude-opus-5-5" );
    CHECK( a.input == 10 );
    CHECK( a.output == 42 );          // final streamed count, not 1 + 42
    CHECK( a.cacheRead == 5000 );
    CHECK( a.cacheWrite5m == 150 );
    CHECK( a.cacheWrite1h == 50 );
    CHECK( a.requests == 1 );
    CHECK( s.buckets[1].model == "claude-haiku-5-5" );
    CHECK( s.countedIds == std::vector<std::string>{ "msg_A", "msg_B" } );
}

TEST_CASE( "already-counted ids are skipped on the next incremental read" )
{
    const auto s = summarize( kTranscript, { "msg_A" }, 0 );
    REQUIRE( s.buckets.size() == 1 );
    CHECK( s.buckets[0].model == "claude-haiku-5-5" );
}

TEST_CASE( "prompt-size tier splits buckets at the threshold" )
{
    const auto s = summarize( kTranscript, {}, 100000 );
    REQUIRE( s.buckets.size() == 2 );
    CHECK_FALSE( s.buckets[0].longPrompt );   // 10 + 5000 + 200 tokens
    CHECK( s.buckets[1].longPrompt );         // 150000 tokens
}

TEST_CASE( "an unterminated last line is left for the next read" )
{
    const std::string partial = kTranscript + R"({"type":"assistant","message":{"id":"msg_C")";
    CHECK( completeLinesEnd( partial ) == kTranscript.size() );
    CHECK( summarize( partial, {}, 0 ).buckets.size() == 2 );
}

TEST_CASE( "a split-less cache write total is priced as the 5-minute write" )
{
    const std::string t = R"({"type":"assistant","message":{"id":"x","model":"m","usage":{"input_tokens":1,"output_tokens":1,"cache_creation_input_tokens":77}}})" "\n";
    const auto s = summarize( t, {}, 0 );
    REQUIRE( s.buckets.size() == 1 );
    CHECK( s.buckets[0].cacheWrite5m == 77 );
    CHECK( s.buckets[0].cacheWrite1h == 0 );
}
