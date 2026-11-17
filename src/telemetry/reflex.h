// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Runtime reflexes for CodeCortex's own tool calls (benchmark pack: bench/benchpack, layer "runtime").
//
// After each call it looks at what just happened and, only when a known waste pattern shows up, returns one short
// corrective note that the bridge appends to that tool result. It never blocks a call and never changes the answer:
// advice only, so a wrong guess cannot hurt the agent.
//
//   retry_storm   B38 / B16   the same tool failed several times in a row
//   no_progress   B39 / B42   the same question keeps returning the same answer (the repeat guard fired again)
//   huge_output   B35         one answer was very large
//
// Self-contained (standard library only) so it is unit tested on its own (test/verify_reflex.cpp).

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rw::telemetry::reflex
{

struct Note
{
    std::string code;   // retry_storm | no_progress | huge_output
    std::string text;   // one short sentence the agent can act on
};

struct Limits
{
    int failureStreak = 3;                     // consecutive failures of one tool before the first note
    int failureRepeatEvery = 5;                // then again every N further failures
    int repeatStreak = 3;                      // consecutive repeat-suppressed answers before a note
    std::int64_t hugeAnswerTokens = 8000;      // same threshold the dashboard's "Large answer" alert uses
    int hugeCooldownCalls = 3;                 // at most one huge_output note per this many calls
};

class Monitor
{
  public:
    explicit Monitor( Limits limits = {} ) : limits_( limits ) {}

    // Call once per finished tools/call. `repeatSuppressed` = the repeat guard replaced the answer with a pointer.
    std::optional<Note> afterCall( std::string_view tool, bool ok, std::int64_t answerTokens, bool repeatSuppressed )
    {
        std::lock_guard<std::mutex> lock( mu_ );
        ++calls_;
        const std::string name( tool );

        // 1. failure streak per tool
        int& fails = failStreak_[name];
        if( ok ) fails = 0; else ++fails;

        // 2. consecutive repeats (any tool): progress of any other kind resets it
        if( repeatSuppressed ) ++repeatStreak_; else repeatStreak_ = 0;

        if( !ok && fails >= limits_.failureStreak
            && ( fails == limits_.failureStreak || ( fails - limits_.failureStreak ) % limits_.failureRepeatEvery == 0 ) )
        {
            return Note{ "retry_storm",
                "CodeCortex: " + name + " has failed " + std::to_string( fails ) + " times in a row. Repeating it unchanged "
                "will not help. Read the error, change an argument (path, symbol name, scope) or try a different tool, "
                "or check that the path and symbol exist before asking again." };
        }

        if( repeatSuppressed && repeatStreak_ >= limits_.repeatStreak
            && ( repeatStreak_ == limits_.repeatStreak || repeatStreak_ % limits_.repeatStreak == 0 ) )
        {
            return Note{ "no_progress",
                "CodeCortex: the same question has now returned the same answer " + std::to_string( repeatStreak_ ) +
                " times and nothing in the project changed. The earlier answer still applies. Use it, or ask something new." };
        }

        if( ok && answerTokens >= limits_.hugeAnswerTokens
            && ( lastHugeCall_ == 0 || calls_ - lastHugeCall_ >= limits_.hugeCooldownCalls ) )
        {
            lastHugeCall_ = calls_;
            return Note{ "huge_output",
                "CodeCortex: that answer was large (about " + std::to_string( answerTokens ) + " tokens). Next time pass a "
                "smaller budget_tokens or narrow the task to one symbol or folder so less goes into context." };
        }
        return std::nullopt;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock( mu_ );
        failStreak_.clear(); repeatStreak_ = 0; calls_ = 0; lastHugeCall_ = 0;
    }

  private:
    Limits limits_;
    std::mutex mu_;
    std::unordered_map<std::string, int> failStreak_;
    int repeatStreak_ = 0;
    std::int64_t calls_ = 0;
    std::int64_t lastHugeCall_ = 0;
};

inline Monitor& sessionMonitor()
{
    static Monitor m;
    return m;
}

} // namespace rw::telemetry::reflex
