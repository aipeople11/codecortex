// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Reads token usage out of a Claude Code session transcript (the JSONL file whose path every Claude Code
// hook payload carries as `transcript_path`). This is HOST-REPORTED data — the exact counts the API
// returned in each assistant message's `usage` object — not an estimate, which is what keeps it inside
// docs/HOST_OBSERVABILITY.md's rule that CodeCortex never invents model token counts.
//
// Shape read, per line (only `type:"assistant"` lines carry usage):
//   {"type":"assistant", "message":{"id":"msg_…","model":"claude-…","usage":{
//       "input_tokens":N,"output_tokens":N,"cache_read_input_tokens":N,"cache_creation_input_tokens":N,
//       "cache_creation":{"ephemeral_5m_input_tokens":N,"ephemeral_1h_input_tokens":N}}}}
//
// ONE API response is written as SEVERAL lines (one per content block) that repeat the same message id,
// and the streamed usage on earlier lines can be partial. So usage is counted ONCE per message id, from
// the LAST line carrying that id — summing every line would double/triple count.
//
// Self-contained on purpose (C++20, standard library only): no project headers, so it is unit-testable in
// isolation (test/verify_transcript_usage.cpp) and cannot drag a platform header into host_observer.h.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rw::telemetry::transcript
{

// Token counts for one (model, prompt-size tier) bucket. Prompt size = input + cache read + cache write of
// ONE request; the tier exists because some models price prompts above a threshold differently (Anthropic's
// published table, e.g. 100k tokens). The caller passes the threshold; 0 disables tiering.
struct Bucket
{
    std::string  model;
    bool         longPrompt   = false;
    std::int64_t input        = 0;
    std::int64_t output       = 0;
    std::int64_t cacheRead    = 0;
    std::int64_t cacheWrite5m = 0;
    std::int64_t cacheWrite1h = 0;
    std::int64_t requests     = 0;
};

struct Summary
{
    std::vector<Bucket>      buckets;
    std::vector<std::string> countedIds;   // message ids counted by THIS call, in order
};

namespace detail
{

inline std::size_t skipWs( std::string_view s, std::size_t i )
{
    while( i < s.size() && ( s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' ) ) ++i;
    return i;
}

// End (one past) of the JSON value starting at i, or npos when malformed/truncated.
inline std::size_t valueEnd( std::string_view s, std::size_t i )
{
    if( i >= s.size() ) return std::string_view::npos;
    const char c = s[i];
    if( c == '"' )
    {
        for( std::size_t j = i + 1; j < s.size(); ++j )
        {
            if( s[j] == '\\' ) { ++j; continue; }
            if( s[j] == '"' ) return j + 1;
        }
        return std::string_view::npos;
    }
    if( c == '{' || c == '[' )
    {
        int depth = 0;
        for( std::size_t j = i; j < s.size(); ++j )
        {
            const char d = s[j];
            if( d == '"' )
            {
                const std::size_t e = valueEnd( s, j );
                if( e == std::string_view::npos ) return e;
                j = e - 1;
                continue;
            }
            if( d == '{' || d == '[' ) ++depth;
            else if( d == '}' || d == ']' ) { if( --depth == 0 ) return j + 1; }
        }
        return std::string_view::npos;
    }
    std::size_t j = i;
    while( j < s.size() && s[j] != ',' && s[j] != '}' && s[j] != ']' && s[j] != ' ' && s[j] != '\n' && s[j] != '\r' && s[j] != '\t' ) ++j;
    return j;
}

// Raw text of the TOP-LEVEL member `key` of the object `obj` (keys inside nested values and inside string
// contents are never matched — a transcript's message text can contain the word "usage" anywhere).
inline std::string_view member( std::string_view obj, std::string_view key )
{
    std::size_t i = skipWs( obj, 0 );
    if( i >= obj.size() || obj[i] != '{' ) return {};
    ++i;
    while( true )
    {
        i = skipWs( obj, i );
        if( i >= obj.size() || obj[i] == '}' ) return {};
        if( obj[i] != '"' ) return {};
        const std::size_t kEnd = valueEnd( obj, i );
        if( kEnd == std::string_view::npos ) return {};
        const std::string_view k = obj.substr( i + 1, kEnd - i - 2 );
        i = skipWs( obj, kEnd );
        if( i >= obj.size() || obj[i] != ':' ) return {};
        i = skipWs( obj, i + 1 );
        const std::size_t vEnd = valueEnd( obj, i );
        if( vEnd == std::string_view::npos ) return {};
        if( k == key ) return obj.substr( i, vEnd - i );
        i = skipWs( obj, vEnd );
        if( i < obj.size() && obj[i] == ',' ) ++i;
    }
}

inline std::string_view stringValue( std::string_view raw )
{
    if( raw.size() < 2 || raw.front() != '"' || raw.back() != '"' ) return {};
    return raw.substr( 1, raw.size() - 2 );   // ids and model names never carry escapes
}

inline std::int64_t intValue( std::string_view raw )
{
    if( raw.empty() || raw.front() < '0' || raw.front() > '9' ) return 0;
    std::int64_t v = 0;
    for( const char c : raw )
    {
        if( c < '0' || c > '9' ) break;
        v = v * 10 + ( c - '0' );
        if( v > ( std::int64_t( 1 ) << 50 ) ) return 0;   // absurd value: treat as unreadable, never overflow
    }
    return v;
}

struct Usage
{
    std::string  id;
    std::string  model;
    std::int64_t input = 0, output = 0, cacheRead = 0, cacheWrite5m = 0, cacheWrite1h = 0;
};

inline bool parseLine( std::string_view line, Usage& out )
{
    if( stringValue( member( line, "type" ) ) != "assistant" ) return false;
    const std::string_view msg = member( line, "message" );
    if( msg.empty() ) return false;
    const std::string_view usage = member( msg, "usage" );
    if( usage.empty() || usage.front() != '{' ) return false;
    out.id    = std::string( stringValue( member( msg, "id" ) ) );
    out.model = std::string( stringValue( member( msg, "model" ) ) );
    if( out.model == "<synthetic>" ) return false;   // locally generated placeholder, never billed
    out.input     = intValue( member( usage, "input_tokens" ) );
    out.output    = intValue( member( usage, "output_tokens" ) );
    out.cacheRead = intValue( member( usage, "cache_read_input_tokens" ) );
    const std::int64_t writeTotal = intValue( member( usage, "cache_creation_input_tokens" ) );
    const std::string_view split = member( usage, "cache_creation" );
    if( !split.empty() && split.front() == '{' )
    {
        out.cacheWrite5m = intValue( member( split, "ephemeral_5m_input_tokens" ) );
        out.cacheWrite1h = intValue( member( split, "ephemeral_1h_input_tokens" ) );
        // A total the split does not account for is priced as the (cheaper, default) 5-minute write.
        if( writeTotal > out.cacheWrite5m + out.cacheWrite1h ) out.cacheWrite5m = writeTotal - out.cacheWrite1h;
    }
    else
    {
        out.cacheWrite5m = writeTotal;
    }
    return true;
}

} // namespace detail

// Summarize every complete line of `text`. Message ids listed in `alreadyCounted` (from a previous call on
// the same transcript) are skipped. `longPromptThreshold` = 0 puts everything in the normal tier.
inline Summary summarize( std::string_view text, const std::vector<std::string>& alreadyCounted,
                          std::int64_t longPromptThreshold = 0 )
{
    // Pass 1: last usage per message id (lines without an id are each their own message).
    std::vector<detail::Usage> perMessage;
    std::size_t pos = 0;
    while( pos < text.size() )
    {
        const std::size_t nl = text.find( '\n', pos );
        if( nl == std::string_view::npos ) break;   // an unterminated tail line may still be being written
        const std::string_view line = text.substr( pos, nl - pos );
        pos = nl + 1;
        detail::Usage u;
        if( !detail::parseLine( line, u ) ) continue;
        bool skip = false;
        for( const auto& seen : alreadyCounted ) if( !u.id.empty() && seen == u.id ) { skip = true; break; }
        if( skip ) continue;
        bool replaced = false;
        if( !u.id.empty() )
            for( auto& m : perMessage ) if( m.id == u.id ) { m = u; replaced = true; break; }
        if( !replaced ) perMessage.push_back( std::move( u ) );
    }

    // Pass 2: fold into (model, tier) buckets.
    Summary out;
    for( const auto& m : perMessage )
    {
        const std::int64_t prompt = m.input + m.cacheRead + m.cacheWrite5m + m.cacheWrite1h;
        const bool longPrompt = longPromptThreshold > 0 && prompt > longPromptThreshold;
        Bucket* b = nullptr;
        for( auto& x : out.buckets ) if( x.model == m.model && x.longPrompt == longPrompt ) { b = &x; break; }
        if( !b ) { out.buckets.push_back( Bucket{ m.model, longPrompt } ); b = &out.buckets.back(); }
        b->input += m.input; b->output += m.output; b->cacheRead += m.cacheRead;
        b->cacheWrite5m += m.cacheWrite5m; b->cacheWrite1h += m.cacheWrite1h; ++b->requests;
        if( !m.id.empty() ) out.countedIds.push_back( m.id );
    }
    return out;
}

// Byte offset just past the last '\n' in `text` — where the next incremental read should resume, so a
// half-written final line is re-read whole next time instead of being lost.
inline std::size_t completeLinesEnd( std::string_view text )
{
    const std::size_t nl = text.rfind( '\n' );
    return nl == std::string_view::npos ? 0 : nl + 1;
}

} // namespace rw::telemetry::transcript
