#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "efficiency/engineering_metrics.h"

TEST_CASE( "engineering efficiency exposes waste and proof without opaque score" )
{
    rw::efficiency::RunMeasurements m;
    m.inputTokens = 80000;
    m.outputTokens = 2000;
    m.toolCalls = 61;
    m.fileReads = 42;
    m.uniqueFileReads = 24;
    m.searches = 20;
    m.repeatedSearches = 8;
    m.filesTouched = 14;
    m.relevantFiles = 4;
    m.edits = 8;
    m.locAdded = 412;
    m.locDeleted = 120;
    m.finalNetLoc = 292;
    m.complexityBefore = 100.0;
    m.complexityAfter = 106.0;
    m.duplicationBeforePct = 7.2;
    m.duplicationAfterPct = 8.4;
    m.dependenciesBefore = 84;
    m.dependenciesAfter = 86;
    m.floatingNodes = 2;
    m.affectedTests = 9;
    m.executedTests = 7;
    m.passedTests = 6;
    m.failedTests = 1;

    const auto x = rw::efficiency::computeEngineeringMetrics( m );
    CHECK( x.totalTokens == 82000 );
    CHECK( x.codeChurn == 532 );
    CHECK( x.netLoc == 292 );
    CHECK( x.repeatedReadPct == doctest::Approx( 42.857 ).epsilon( 0.001 ) );
    CHECK( x.repeatedSearchPct == doctest::Approx( 40.0 ) );
    CHECK( x.changeAmplification == doctest::Approx( 3.5 ) );
    CHECK( x.complexityDeltaPct == doctest::Approx( 6.0 ) );
    CHECK( x.duplicationDeltaPct == doctest::Approx( 1.2 ) );
    CHECK( x.dependencyDelta == 2 );
    CHECK( x.testImpactCoveragePct == doctest::Approx( 77.777 ).epsilon( 0.001 ) );
    CHECK( x.testPassPct == doctest::Approx( 85.714 ).epsilon( 0.001 ) );
    CHECK( x.discoveryEfficiency == rw::efficiency::Signal::Poor );
    CHECK( x.changeEfficiency == rw::efficiency::Signal::Poor );
    CHECK( x.codeHealth == rw::efficiency::Signal::Poor );
    CHECK( x.verification == rw::efficiency::Signal::Poor );

    const std::string json = rw::efficiency::toJson( x );
    CHECK( json.find( "\"total_tokens\":82000" ) != std::string::npos );
    CHECK( json.find( "\"code_health\":\"poor\"" ) != std::string::npos );
    CHECK( json.find( "score" ) == std::string::npos );
}

TEST_CASE( "unknown observations remain unknown instead of becoming fake zeros" )
{
    const auto x = rw::efficiency::computeEngineeringMetrics( {} );
    CHECK( x.totalTokens == -1 );
    CHECK( x.discoveryRedundancyPct < 0.0 );
    CHECK( x.tokenEfficiency == rw::efficiency::Signal::Unknown );
    CHECK( x.codeHealth == rw::efficiency::Signal::Unknown );
    const std::string json = rw::efficiency::toJson( x );
    CHECK( json.find( "\"total_tokens\":null" ) != std::string::npos );
}
