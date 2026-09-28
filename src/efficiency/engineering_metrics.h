// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstdint>
#include <string>

namespace rw::efficiency
{

enum class Signal : std::uint8_t { Good, Watch, Poor, Unknown };

inline constexpr const char* signalName( Signal s ) noexcept
{
    switch( s )
    {
        case Signal::Good: return "good";
        case Signal::Watch: return "watch";
        case Signal::Poor: return "poor";
        case Signal::Unknown: return "unknown";
    }
    return "unknown";
}

// Raw observations only. Callers should populate what they can measure; -1 means unavailable.
struct RunMeasurements
{
    std::int64_t inputTokens = -1;
    std::int64_t outputTokens = -1;
    std::int64_t toolCalls = -1;

    std::int64_t fileReads = -1;
    std::int64_t uniqueFileReads = -1;
    std::int64_t searches = -1;
    std::int64_t repeatedSearches = -1;

    std::int64_t filesTouched = -1;
    std::int64_t relevantFiles = -1;
    std::int64_t edits = -1;
    std::int64_t locAdded = -1;
    std::int64_t locDeleted = -1;
    std::int64_t finalNetLoc = -1; // absolute magnitude of final net delta; caller may pass abs(+/-)

    double complexityBefore = -1.0;
    double complexityAfter = -1.0;
    double duplicationBeforePct = -1.0;
    double duplicationAfterPct = -1.0;
    std::int64_t dependenciesBefore = -1;
    std::int64_t dependenciesAfter = -1;
    std::int64_t floatingNodes = -1;

    std::int64_t affectedTests = -1;
    std::int64_t executedTests = -1;
    std::int64_t passedTests = -1;
    std::int64_t failedTests = -1;
};

struct EngineeringMetrics
{
    std::int64_t totalTokens = -1;
    std::int64_t codeChurn = -1;       // added + deleted
    std::int64_t netLoc = -1;          // added - deleted

    double repeatedReadPct = -1.0;
    double repeatedSearchPct = -1.0;
    double discoveryRedundancyPct = -1.0;
    double reworkPct = -1.0;
    double changeAmplification = -1.0;
    double complexityDeltaPct = -1.0;
    double duplicationDeltaPct = -1.0;
    std::int64_t dependencyDelta = -1;
    double testImpactCoveragePct = -1.0;
    double testPassPct = -1.0;
    double tokensPerNetLoc = -1.0;

    Signal tokenEfficiency = Signal::Unknown;
    Signal discoveryEfficiency = Signal::Unknown;
    Signal changeEfficiency = Signal::Unknown;
    Signal codeHealth = Signal::Unknown;
    Signal verification = Signal::Unknown;
};

EngineeringMetrics computeEngineeringMetrics( const RunMeasurements& m ) noexcept;
std::string toJson( const EngineeringMetrics& m );

} // namespace rw::efficiency
