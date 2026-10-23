// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Turns one parsed OTLP record into a CodeCortex telemetry Event. Pure (no sockets, no files) so it is unit
// tested in test/verify_otlp_json.cpp. First slice: only claude_code.api_request is mapped (token counts +
// the cost Claude Code itself reports). Everything else is ignored.

#include "otlp_json.h"
#include "events.h"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_set>

namespace rw::telemetry::otlp
{

inline std::int64_t toInt( const std::string& s, std::int64_t fallback = -1 )
{
    if( s.empty() ) return fallback;
    char* end = nullptr;
    const long long v = std::strtoll( s.c_str(), &end, 10 );
    if( end == s.c_str() ) return fallback;
    return v < 0 ? fallback : static_cast<std::int64_t>( v );
}

// Remembers recent request ids so a re-sent batch (or the same request seen twice) is counted once.
class Dedupe
{
  public:
    // true = first time seen. An empty id is never deduplicated.
    bool firstTime( const std::string& id )
    {
        if( id.empty() ) return true;
        if( seen_.count( id ) ) return false;
        if( seen_.size() >= kMax ) seen_.clear();   // bounded; ids repeat only within seconds
        seen_.insert( id );
        return true;
    }

  private:
    static constexpr std::size_t kMax = 4096;
    std::unordered_set<std::string> seen_;
};

inline std::optional<Event> mapRecord( const Record& r )
{
    if( r.name != "api_request" ) return std::nullopt;
    Event ev;
    ev.type = EventType::TokenUsage;
    ev.source = "host_reported";
    ev.hostName = "claude-code";
    ev.model = r.get( "model" );
    ev.target = ev.model.empty() ? std::string( "model not reported" ) : ev.model;
    ev.detail = "claude_code_otel";
    ev.sessionId = r.get( "session.id" );
    ev.promptId = r.get( "prompt.id" );
    ev.requestId = r.get( "request_id" );
    ev.inputTokens = toInt( r.get( "input_tokens" ) );
    ev.outputTokens = toInt( r.get( "output_tokens" ) );
    ev.cacheReadTokens = toInt( r.get( "cache_read_tokens" ) );
    // Claude Code does not say which cache-write tier; the 5-minute bucket is the default tier.
    ev.cacheWrite5mTokens = toInt( r.get( "cache_creation_tokens" ) );
    ev.durationMs = toInt( r.get( "duration_ms" ) );
    ev.requestCount = 1;
    ev.timestampMs = r.tsMs > 0 ? r.tsMs : unixTimeMs();

    ev.costUsdMicros = toInt( r.get( "cost_usd_micros" ) );
    if( ev.costUsdMicros < 0 )
    {
        const std::string c = r.get( "cost_usd" );
        if( !c.empty() )
        {
            const double usd = std::strtod( c.c_str(), nullptr );
            if( usd >= 0.0 ) ev.costUsdMicros = static_cast<std::int64_t>( std::llround( usd * 1e6 ) );
        }
    }
    // One run per prompt (falls back to the session) so per-prompt totals add up on the dashboard.
    const std::string group = !ev.promptId.empty() ? ev.promptId : ev.sessionId;
    ev.runId = "otel-" + ( group.empty() ? std::string( "unknown" ) : group );
    return ev;
}

} // namespace rw::telemetry::otlp
