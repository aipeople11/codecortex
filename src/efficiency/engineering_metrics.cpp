// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "engineering_metrics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace rw::efficiency
{
namespace
{

double percent( double part, double whole ) noexcept
{
    return whole > 0.0 ? ( part * 100.0 / whole ) : -1.0;
}

double deltaPercent( double before, double after ) noexcept
{
    if( before < 0.0 || after < 0.0 ) return -1.0;
    if( before == 0.0 ) return after == 0.0 ? 0.0 : -1.0;
    return ( after - before ) * 100.0 / before;
}

Signal thresholdLowGood( double value, double goodMax, double watchMax ) noexcept
{
    if( value < 0.0 ) return Signal::Unknown;
    if( value <= goodMax ) return Signal::Good;
    if( value <= watchMax ) return Signal::Watch;
    return Signal::Poor;
}

Signal worst( Signal a, Signal b ) noexcept
{
    auto rank = []( Signal s )
    {
        switch( s ) { case Signal::Poor: return 3; case Signal::Watch: return 2; case Signal::Good: return 1; case Signal::Unknown: return 0; }
        return 0;
    };
    if( a == Signal::Unknown ) return b;
    if( b == Signal::Unknown ) return a;
    return rank( a ) >= rank( b ) ? a : b;
}

void jsonNumber( std::string& out, const char* name, double value, bool comma )
{
    if( comma ) out += ',';
    out += '"'; out += name; out += "\":";
    if( value < 0.0 || !std::isfinite( value ) ) out += "null";
    else
    {
        char buf[64];
        std::snprintf( buf, sizeof( buf ), "%.3f", value );
        out += buf;
    }
}

void jsonInt( std::string& out, const char* name, std::int64_t value, bool comma )
{
    if( comma ) out += ',';
    out += '"'; out += name; out += "\":";
    if( value < 0 ) out += "null"; else out += std::to_string( value );
}

void jsonSignal( std::string& out, const char* name, Signal value )
{
    out += ",\""; out += name; out += "\":\""; out += signalName( value ); out += '"';
}

} // namespace

EngineeringMetrics computeEngineeringMetrics( const RunMeasurements& m ) noexcept
{
    EngineeringMetrics out;
    if( m.inputTokens >= 0 && m.outputTokens >= 0 ) out.totalTokens = m.inputTokens + m.outputTokens;
    if( m.locAdded >= 0 && m.locDeleted >= 0 )
    {
        out.codeChurn = m.locAdded + m.locDeleted;
        out.netLoc = m.locAdded - m.locDeleted;
    }

    if( m.fileReads >= 0 && m.uniqueFileReads >= 0 && m.fileReads > 0 )
    {
        const auto repeated = std::max<std::int64_t>( 0, m.fileReads - m.uniqueFileReads );
        out.repeatedReadPct = percent( static_cast<double>( repeated ), static_cast<double>( m.fileReads ) );
    }
    if( m.searches > 0 && m.repeatedSearches >= 0 )
        out.repeatedSearchPct = percent( static_cast<double>( m.repeatedSearches ), static_cast<double>( m.searches ) );

    if( out.repeatedReadPct >= 0.0 && out.repeatedSearchPct >= 0.0 )
        out.discoveryRedundancyPct = ( out.repeatedReadPct + out.repeatedSearchPct ) / 2.0;
    else if( out.repeatedReadPct >= 0.0 ) out.discoveryRedundancyPct = out.repeatedReadPct;
    else if( out.repeatedSearchPct >= 0.0 ) out.discoveryRedundancyPct = out.repeatedSearchPct;

    if( out.codeChurn > 0 && m.finalNetLoc >= 0 )
    {
        const auto discarded = std::max<std::int64_t>( 0, out.codeChurn - m.finalNetLoc );
        out.reworkPct = percent( static_cast<double>( discarded ), static_cast<double>( out.codeChurn ) );
    }
    if( m.filesTouched >= 0 && m.relevantFiles > 0 )
        out.changeAmplification = static_cast<double>( m.filesTouched ) / static_cast<double>( m.relevantFiles );

    out.complexityDeltaPct = deltaPercent( m.complexityBefore, m.complexityAfter );
    if( m.duplicationBeforePct >= 0.0 && m.duplicationAfterPct >= 0.0 )
        out.duplicationDeltaPct = m.duplicationAfterPct - m.duplicationBeforePct;
    if( m.dependenciesBefore >= 0 && m.dependenciesAfter >= 0 )
        out.dependencyDelta = m.dependenciesAfter - m.dependenciesBefore;

    if( m.affectedTests > 0 && m.executedTests >= 0 )
        out.testImpactCoveragePct = percent( static_cast<double>( std::min( m.executedTests, m.affectedTests ) ), static_cast<double>( m.affectedTests ) );
    if( m.executedTests > 0 && m.passedTests >= 0 )
        out.testPassPct = percent( static_cast<double>( m.passedTests ), static_cast<double>( m.executedTests ) );

    if( out.totalTokens >= 0 && m.finalNetLoc > 0 )
        out.tokensPerNetLoc = static_cast<double>( out.totalTokens ) / static_cast<double>( m.finalNetLoc );

    // These are display bands, not a universal score. Raw measurements remain the authority.
    out.discoveryEfficiency = thresholdLowGood( out.discoveryRedundancyPct, 10.0, 25.0 );
    const Signal amp = out.changeAmplification < 0 ? Signal::Unknown : ( out.changeAmplification <= 1.5 ? Signal::Good : out.changeAmplification <= 3.0 ? Signal::Watch : Signal::Poor );
    const Signal rework = thresholdLowGood( out.reworkPct, 25.0, 50.0 );
    out.changeEfficiency = worst( amp, rework );

    Signal complexity = Signal::Unknown;
    if( out.complexityDeltaPct >= 0.0 ) complexity = thresholdLowGood( out.complexityDeltaPct, 2.0, 10.0 );
    Signal duplication = Signal::Unknown;
    if( out.duplicationDeltaPct >= 0.0 ) duplication = thresholdLowGood( out.duplicationDeltaPct, 0.25, 1.0 );
    Signal floating = Signal::Unknown;
    if( m.floatingNodes >= 0 ) floating = m.floatingNodes == 0 ? Signal::Good : m.floatingNodes <= 2 ? Signal::Watch : Signal::Poor;
    out.codeHealth = worst( worst( complexity, duplication ), floating );

    if( out.testImpactCoveragePct >= 0.0 || out.testPassPct >= 0.0 )
    {
        const Signal coverage = out.testImpactCoveragePct < 0 ? Signal::Unknown : ( out.testImpactCoveragePct >= 100.0 ? Signal::Good : out.testImpactCoveragePct >= 80.0 ? Signal::Watch : Signal::Poor );
        const Signal pass = out.testPassPct < 0 ? Signal::Unknown : ( out.testPassPct >= 100.0 ? Signal::Good : out.testPassPct >= 95.0 ? Signal::Watch : Signal::Poor );
        out.verification = worst( coverage, pass );
    }

    if( out.tokensPerNetLoc >= 0.0 ) out.tokenEfficiency = thresholdLowGood( out.tokensPerNetLoc, 1000.0, 3000.0 );
    return out;
}

std::string toJson( const EngineeringMetrics& m )
{
    std::string out = "{";
    jsonInt( out, "total_tokens", m.totalTokens, false );
    jsonInt( out, "code_churn", m.codeChurn, true );
    jsonInt( out, "net_loc", m.netLoc, true );
    jsonNumber( out, "repeated_read_pct", m.repeatedReadPct, true );
    jsonNumber( out, "repeated_search_pct", m.repeatedSearchPct, true );
    jsonNumber( out, "discovery_redundancy_pct", m.discoveryRedundancyPct, true );
    jsonNumber( out, "rework_pct", m.reworkPct, true );
    jsonNumber( out, "change_amplification", m.changeAmplification, true );
    jsonNumber( out, "complexity_delta_pct", m.complexityDeltaPct, true );
    jsonNumber( out, "duplication_delta_pct", m.duplicationDeltaPct, true );
    jsonInt( out, "dependency_delta", m.dependencyDelta, true );
    jsonNumber( out, "test_impact_coverage_pct", m.testImpactCoveragePct, true );
    jsonNumber( out, "test_pass_pct", m.testPassPct, true );
    jsonNumber( out, "tokens_per_net_loc", m.tokensPerNetLoc, true );
    jsonSignal( out, "token_efficiency", m.tokenEfficiency );
    jsonSignal( out, "discovery_efficiency", m.discoveryEfficiency );
    jsonSignal( out, "change_efficiency", m.changeEfficiency );
    jsonSignal( out, "code_health", m.codeHealth );
    jsonSignal( out, "verification", m.verification );
    out += '}';
    return out;
}

} // namespace rw::efficiency
