// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Per-MCP-session bookkeeping for the CodeCortex bridge (src/codecortex_mcp_bridge.h):
//
//   1. CONTEXT METER  — how many tokens each CodeCortex answer put into the agent's context (estimated),
//      and how many tokens reading the files it covers would have cost instead (estimated). Both are
//      DERIVED numbers and are labelled so; they are never mixed with host-reported token counts.
//   2. REPEAT GUARD   — the second time an agent asks a read-only tool the exact same question and gets a
//      byte-identical answer, return a one-line pointer to the earlier call instead of resending it. The
//      third identical ask gets the full text again (the agent evidently lost it, e.g. to compaction).
//   3. RETRY CHAIN    — a call to a tool whose previous call FAILED (with no success in between) is that
//      call's retry: attempt N, linked to the failed call's id. A success ends the chain.
//
// The MCP server is one long-lived process per client session, so in-memory state is the right lifetime.
// Self-contained (C++20, standard library only) so it is unit-tested in isolation
// (test/verify_session_meter.cpp).

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rw::telemetry::meter
{

inline std::uint64_t fnv1a( std::string_view v, std::uint64_t h = 1469598103934665603ULL ) noexcept
{
    for( const unsigned char c : v ) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Bytes-per-token rates. kAnswerBytesPerToken matches the conservative markup-heavy default the CodeCortex
// renderer prices its own output with (serialize.h kBytesPerTokenDefault = 2.5, rounded up for JSON
// escaping overhead); kSourceBytesPerToken matches its source-body rate (serialize.h kBytesPerTokenBody).
inline constexpr double kAnswerBytesPerToken = 3.0;
inline constexpr double kSourceBytesPerToken = 3.8;

inline std::int64_t tokensForBytes( std::uint64_t bytes, double rate ) noexcept
{
    return static_cast<std::int64_t>( static_cast<double>( bytes ) / rate + 0.5 );
}

// The answer's own `est_tokens="N"` header when present (JSON-escaped or not) — the renderer's estimate is
// better than ours — otherwise bytes / kAnswerBytesPerToken.
inline std::int64_t estimateAnswerTokens( std::string_view resp ) noexcept
{
    for( const std::string_view key : { std::string_view( "est_tokens=\\\"" ), std::string_view( "est_tokens=\"" ) } )
    {
        const auto p = resp.find( key );
        if( p == std::string_view::npos ) continue;
        std::int64_t v = 0; std::size_t i = p + key.size(); bool any = false;
        while( i < resp.size() && resp[i] >= '0' && resp[i] <= '9' ) { v = v * 10 + ( resp[i] - '0' ); ++i; any = true; }
        if( any && v > 0 ) return v;
    }
    return tokensForBytes( resp.size(), kAnswerBytesPerToken );
}

// Relative-path-looking tokens in an answer (`src/a/b.cpp`, `ui/app.js:120`), de-duplicated, at most
// `cap`. The caller keeps only those that exist under the project root, so false positives cost nothing.
template<class Fn> inline void forEachPathCandidate( std::string_view text, std::size_t cap, Fn&& fn )
{
    auto isPathChar = []( char c ) {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' )
            || c == '_' || c == '-' || c == '.' || c == '/';
    };
    std::unordered_map<std::string, bool> seen;
    std::size_t i = 0;
    while( i < text.size() && seen.size() < cap )
    {
        while( i < text.size() && !isPathChar( text[i] ) ) ++i;
        const std::size_t start = i;
        while( i < text.size() && isPathChar( text[i] ) ) ++i;
        std::string_view tok = text.substr( start, i - start );
        if( tok.starts_with( ".." ) ) continue;                                                           // outside the root
        while( !tok.empty() && ( tok.back() == '.' || tok.back() == '/' ) ) tok.remove_suffix( 1 );
        while( !tok.empty() && ( tok.front() == '/' || tok.front() == '.' ) ) tok.remove_prefix( 1 );   // never absolute / dot-relative
        const auto slash = tok.find( '/' );
        const auto dot = tok.rfind( '.' );
        if( tok.size() < 4 || slash == std::string_view::npos || dot == std::string_view::npos || dot < tok.rfind( '/' ) ) continue;
        if( tok.size() - dot - 1 < 1 || tok.size() - dot - 1 > 8 || tok.find( ".." ) != std::string_view::npos ) continue;
        std::string key( tok );
        if( seen.emplace( key, true ).second ) fn( key );
    }
}

// Tools that change state are never repeat-guarded, whatever their output looks like.
inline bool isMutatingTool( std::string_view name ) noexcept
{
    for( const std::string_view needle : { "replace", "insert", "rename", "write", "edit", "delete", "checkpoint",
                                           "remember", "prove_change", "status", "apply", "move" } )
        if( name.find( needle ) != std::string_view::npos ) return true;
    return false;
}

struct RetryInfo
{
    std::int64_t attempt = 1;
    std::string  retryOf;   // empty = not a retry
};

struct RepeatHit
{
    bool         suppress = false;
    std::string  earlierCallId;
    std::int64_t tokensNotResent = 0;
};

class SessionMeter
{
  public:
    // Before dispatch: is this call a retry of a failed call of the same tool?
    RetryInfo beginCall( std::string_view tool ) const
    {
        const auto it = failures_.find( std::string( tool ) );
        if( it == failures_.end() ) return {};
        return { it->second.attempt + 1, it->second.lastFailedId };
    }

    // After dispatch: a failure extends (or starts) the tool's chain; a success ends it.
    void endCall( std::string_view tool, std::string_view callId, bool ok, const RetryInfo& info )
    {
        if( ok ) { failures_.erase( std::string( tool ) ); return; }
        failures_[std::string( tool )] = { std::string( callId ), info.attempt };
    }

    // After a successful read-only call: decide whether to replace the answer with a pointer. `questionKey`
    // identifies tool + arguments; `answerKey` the answer body (excluding the per-call JSON-RPC id).
    RepeatHit checkRepeat( std::string_view tool, std::string_view questionKey, std::string_view answerBody,
                           std::string_view callId, std::int64_t answerTokens )
    {
        if( isMutatingTool( tool ) ) return {};
        const std::uint64_t q = fnv1a( answerBody, fnv1a( questionKey, fnv1a( tool ) ) );
        auto& e = answers_[q];
        if( e.timesSeen == 0 ) { e.firstCallId = std::string( callId ); e.timesSeen = 1; return {}; }
        ++e.timesSeen;
        if( e.timesSeen == 2 ) return { true, e.firstCallId, answerTokens };
        e.timesSeen = 1;               // third identical ask: send it in full, and restart the count
        e.firstCallId = std::string( callId );
        return {};
    }

    // Running totals for this session (shown by codecortex_status).
    std::int64_t answersTokens = 0;
    std::int64_t avoidedTokens = 0;
    std::int64_t repeatTokensSaved = 0;
    std::int64_t repeatsSuppressed = 0;

  private:
    struct Failure { std::string lastFailedId; std::int64_t attempt = 1; };
    struct Answer { std::string firstCallId; int timesSeen = 0; };
    std::unordered_map<std::string, Failure> failures_;
    std::unordered_map<std::uint64_t, Answer> answers_;
};

inline SessionMeter& sessionMeter()
{
    static SessionMeter m;
    return m;
}

} // namespace rw::telemetry::meter
