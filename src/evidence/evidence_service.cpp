// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "evidence_service.h"

#include "infra/jsonesc.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

namespace rw::evidence
{
namespace
{
std::string jsonString( std::string_view value )
{
    std::string out;
    out.push_back( '"' );
    rw::jsonesc::escapeInto( value, out, true, true, true );
    out.push_back( '"' );
    return out;
}

std::size_t keyValueStart( std::string_view line, std::string_view key )
{
    int depth = 0;
    for( std::size_t i = 0; i < line.size(); ++i )
    {
        const char c = line[i];
        if( c == '{' ) { ++depth; continue; }
        if( c == '}' ) { --depth; continue; }
        if( c != '"' || depth != 1 ) continue;

        const std::size_t begin = ++i;
        bool escaped = false;
        for( ; i < line.size(); ++i )
        {
            const char q = line[i];
            if( escaped ) { escaped = false; continue; }
            if( q == '\\' ) { escaped = true; continue; }
            if( q == '"' ) break;
        }
        if( i >= line.size() ) return std::string_view::npos;
        if( line.substr( begin, i - begin ) != key ) continue;
        std::size_t p = i + 1;
        while( p < line.size() && std::isspace( static_cast<unsigned char>( line[p] ) ) ) ++p;
        if( p >= line.size() || line[p] != ':' ) continue;
        ++p;
        while( p < line.size() && std::isspace( static_cast<unsigned char>( line[p] ) ) ) ++p;
        return p;
    }
    return std::string_view::npos;
}

std::string stringField( std::string_view line, std::string_view key )
{
    std::size_t p = keyValueStart( line, key );
    if( p == std::string_view::npos || p >= line.size() || line[p] != '"' ) return {};
    ++p;
    std::string out;
    for( ; p < line.size(); ++p )
    {
        char c = line[p];
        if( c == '"' ) break;
        if( c != '\\' ) { out.push_back( c ); continue; }
        if( ++p >= line.size() ) break;
        switch( line[p] )
        {
            case '"': out.push_back( '"' ); break;
            case '\\': out.push_back( '\\' ); break;
            case '/': out.push_back( '/' ); break;
            case 'b': out.push_back( '\b' ); break;
            case 'f': out.push_back( '\f' ); break;
            case 'n': out.push_back( '\n' ); break;
            case 'r': out.push_back( '\r' ); break;
            case 't': out.push_back( '\t' ); break;
            default: out.push_back( line[p] ); break;
        }
    }
    return out;
}

std::int64_t intField( std::string_view line, std::string_view key, std::int64_t fallback = -1 )
{
    std::size_t p = keyValueStart( line, key );
    if( p == std::string_view::npos || p >= line.size() ) return fallback;
    bool neg = false;
    if( line[p] == '-' ) { neg = true; ++p; }
    if( p >= line.size() || !std::isdigit( static_cast<unsigned char>( line[p] ) ) ) return fallback;
    std::int64_t v = 0;
    while( p < line.size() && std::isdigit( static_cast<unsigned char>( line[p] ) ) )
    {
        v = v * 10 + ( line[p] - '0' );
        ++p;
    }
    return neg ? -v : v;
}

bool boolField( std::string_view line, std::string_view key, bool fallback = true )
{
    std::size_t p = keyValueStart( line, key );
    if( p == std::string_view::npos ) return fallback;
    if( line.substr( p, 4 ) == "true" ) return true;
    if( line.substr( p, 5 ) == "false" ) return false;
    return fallback;
}

EventRecord parseEvent( std::string_view line )
{
    EventRecord e;
    e.raw.assign( line );
    e.type = stringField( line, "type" );
    e.runId = stringField( line, "run_id" );
    e.projectId = stringField( line, "project_id" );
    e.projectName = stringField( line, "project_name" );
    e.repoId = stringField( line, "repo_id" );
    e.sessionId = stringField( line, "session_id" );
    e.model = stringField( line, "model" );
    e.hostName = stringField( line, "host_name" );
    e.source = stringField( line, "source" );
    e.target = stringField( line, "target" );
    e.detail = stringField( line, "detail" );
    e.timestampMs = intField( line, "ts_ms", 0 );
    e.durationMs = intField( line, "duration_ms", -1 );
    e.inputTokens = intField( line, "input_tokens", -1 );
    e.outputTokens = intField( line, "output_tokens", -1 );
    e.numericValue = intField( line, "value", -1 );
    e.success = boolField( line, "success", true );
    return e;
}

std::string provenance( const EventRecord* e, std::string_view fallback = "OBSERVED" )
{
    if( !e ) return "NOT AVAILABLE";
    if( e->source == "host_reported" ) return "HOST-REPORTED";
    if( e->source == "provider_reported" ) return "PROVIDER-REPORTED";
    if( e->source == "structural" ) return "STRUCTURAL";
    if( e->source == "derived" ) return "DERIVED";
    if( e->source == "observed" ) return "OBSERVED";
    return std::string( fallback );
}

std::string metricJson( std::int64_t value, bool available, std::string_view prov )
{
    return std::string( "{\"available\":" ) + ( available ? "true" : "false" )
        + ",\"value\":" + ( available ? std::to_string( value ) : "null" )
        + ",\"provenance\":" + jsonString( available ? prov : "NOT AVAILABLE" ) + "}";
}

std::int64_t repeatedCount( const std::vector<const EventRecord*>& events, std::string_view type )
{
    std::map<std::string, std::int64_t> counts;
    for( auto* e : events ) if( e->type == type && !e->target.empty() ) ++counts[e->target];
    std::int64_t out = 0;
    for( const auto& [_, n] : counts ) if( n > 1 ) out += n - 1;
    return out;
}

std::string runSummaryJson( const RunSummary& r )
{
    std::string out = "{\"run_id\":" + jsonString( r.runId );
    if( !r.projectId.empty() ) out += ",\"project_id\":" + jsonString( r.projectId );
    if( !r.projectName.empty() ) out += ",\"project_name\":" + jsonString( r.projectName );
    if( !r.sessionId.empty() ) out += ",\"session_id\":" + jsonString( r.sessionId );
    if( !r.hostName.empty() ) out += ",\"host_name\":" + jsonString( r.hostName );
    if( !r.model.empty() ) out += ",\"model\":" + jsonString( r.model );
    out += ",\"started_ms\":" + std::to_string( r.startedMs )
        + ",\"finished_ms\":" + std::to_string( r.finishedMs )
        + ",\"duration_ms\":" + std::to_string( r.durationMs )
        + ",\"event_count\":" + std::to_string( r.eventCount )
        + ",\"tool_calls\":" + std::to_string( r.toolCalls )
        + ",\"file_reads\":" + std::to_string( r.fileReads )
        + ",\"searches\":" + std::to_string( r.searches )
        + ",\"edits\":" + std::to_string( r.edits )
        + ",\"tests\":" + std::to_string( r.tests )
        + ",\"failed_actions\":" + std::to_string( r.failedActions )
        + ",\"memory_recalls\":" + std::to_string( r.memoryRecalls )
        + ",\"input_tokens\":" + ( r.inputTokens >= 0 ? std::to_string( r.inputTokens ) : "null" )
        + ",\"output_tokens\":" + ( r.outputTokens >= 0 ? std::to_string( r.outputTokens ) : "null" )
        + ",\"finished\":" + ( r.finished ? "true" : "false" )
        + ",\"success\":" + ( r.success ? "true" : "false" ) + "}";
    return out;
}
}

EvidenceService::EvidenceService( std::string telemetryPath ) : telemetryPath_( std::move( telemetryPath ) ) {}

void EvidenceService::setTelemetryPath( std::string path )
{
    if( path == telemetryPath_ ) return;
    telemetryPath_ = std::move( path );
    reset();
}

void EvidenceService::reset()
{
    indexedBytes_ = 0;
    partialLine_.clear();
    events_.clear();
    runs_.clear();
    available_ = false;
}

bool EvidenceService::refresh()
{
    if( telemetryPath_.empty() ) { reset(); return false; }
    std::error_code ec;
    const auto size = std::filesystem::file_size( telemetryPath_, ec );
    if( ec ) { available_ = false; return false; }
    available_ = true;
    if( size < indexedBytes_ ) { reset(); available_ = true; }
    if( size == indexedBytes_ ) return true;

    std::ifstream in( telemetryPath_, std::ios::binary );
    if( !in ) { available_ = false; return false; }
    in.seekg( static_cast<std::streamoff>( indexedBytes_ ) );
    std::string bytes( std::istreambuf_iterator<char>( in ), {} );
    // Prefix any record left incomplete by the previous refresh. Only advance
    // indexedBytes_ through complete newline-delimited records; otherwise a
    // partial write would be permanently skipped.
    std::string chunk;
    chunk.reserve( partialLine_.size() + bytes.size() );
    chunk = std::move( partialLine_ );
    chunk += bytes;
    const auto lastNewline = chunk.find_last_of( '\n' );
    if( lastNewline == std::string::npos )
    {
        partialLine_ = std::move( chunk );
        indexedBytes_ += bytes.size();
        return true;
    }
    appendChunk( std::string_view( chunk ).substr( 0, lastNewline + 1 ) );
    partialLine_.assign( chunk.data() + lastNewline + 1, chunk.size() - lastNewline - 1 );
    indexedBytes_ += bytes.size();
    rebuildRuns();
    return true;
}

void EvidenceService::appendChunk( std::string_view bytes )
{
    std::size_t pos = 0;
    while( pos < bytes.size() )
    {
        const auto nl = bytes.find( '\n', pos );
        if( nl == std::string_view::npos ) break;
        auto line = bytes.substr( pos, nl - pos );
        if( !line.empty() && line.back() == '\r' ) line.remove_suffix( 1 );
        if( !line.empty() && line.front() == '{' && line.back() == '}' )
        {
            auto event = parseEvent( line );
            if( !event.type.empty() ) events_.push_back( std::move( event ) );
        }
        pos = nl + 1;
    }
}

void EvidenceService::rebuildRuns()
{
    std::map<std::string, RunSummary> byId;
    for( const auto& e : events_ )
    {
        if( e.runId.empty() ) continue;
        auto& r = byId[e.runId];
        r.runId = e.runId;
        ++r.eventCount;
        if( !e.projectId.empty() ) r.projectId = e.projectId;
        if( !e.projectName.empty() ) r.projectName = e.projectName;
        if( !e.sessionId.empty() ) r.sessionId = e.sessionId;
        if( !e.hostName.empty() ) r.hostName = e.hostName;
        if( !e.model.empty() ) r.model = e.model;
        if( e.type == "run_started" ) r.startedMs = e.timestampMs;
        if( e.type == "run_finished" ) { r.finished = true; r.finishedMs = e.timestampMs; r.durationMs = e.durationMs; r.success = e.success; }
        if( e.type == "tool_call" ) { ++r.toolCalls; if( !e.success ) ++r.failedActions; }
        else if( e.type == "file_read" ) ++r.fileReads;
        else if( e.type == "search" ) ++r.searches;
        else if( e.type == "edit" ) ++r.edits;
        else if( e.type == "test" ) ++r.tests;
        else if( e.type == "memory_recall" ) ++r.memoryRecalls;
        else if( e.type == "token_usage" )
        {
            if( e.inputTokens >= 0 ) r.inputTokens = std::max<std::int64_t>( 0, r.inputTokens ) + e.inputTokens;
            if( e.outputTokens >= 0 ) r.outputTokens = std::max<std::int64_t>( 0, r.outputTokens ) + e.outputTokens;
        }
    }
    runs_.clear();
    for( auto& [_, r] : byId ) runs_.push_back( std::move( r ) );
    std::sort( runs_.begin(), runs_.end(), []( const RunSummary& a, const RunSummary& b )
    {
        const auto at = a.startedMs ? a.startedMs : a.finishedMs;
        const auto bt = b.startedMs ? b.startedMs : b.finishedMs;
        return at > bt;
    } );
}

const RunSummary* EvidenceService::findRun( std::string_view runId ) const noexcept
{
    for( const auto& r : runs_ ) if( r.runId == runId ) return &r;
    return nullptr;
}

std::vector<const EventRecord*> EvidenceService::eventsForRun( std::string_view runId ) const
{
    std::vector<const EventRecord*> out;
    for( const auto& e : events_ ) if( e.runId == runId ) out.push_back( &e );
    std::sort( out.begin(), out.end(), []( auto* a, auto* b ){ return a->timestampMs < b->timestampMs; } );
    return out;
}

std::string EvidenceService::latestRunId() const { return runs_.empty() ? std::string{} : runs_.front().runId; }

std::string EvidenceService::evidenceJson( std::size_t limit, std::size_t cursor ) const
{
    if( limit == 0 ) limit = 200;
    limit = std::min<std::size_t>( limit, 5000 );
    cursor = std::min( cursor, events_.size() );
    const std::size_t end = std::min( events_.size(), cursor + limit );
    std::string out = "{\"ok\":true,\"available\":" + std::string( available_ ? "true" : "false" )
        + ",\"source\":\"indexed-jsonl\",\"total_events\":" + std::to_string( events_.size() )
        + ",\"total_runs\":" + std::to_string( runs_.size() ) + ",\"events\":[";
    for( std::size_t i = cursor; i < end; ++i ) { if( i != cursor ) out.push_back( ',' ); out += events_[i].raw; }
    out += "],\"next_cursor\":" + ( end < events_.size() ? std::to_string( end ) : "null" ) + "}";
    return out;
}

std::string EvidenceService::runsJson( std::size_t limit, std::size_t cursor ) const
{
    if( limit == 0 ) limit = 50;
    limit = std::min<std::size_t>( limit, 500 );
    cursor = std::min( cursor, runs_.size() );
    const auto end = std::min( runs_.size(), cursor + limit );
    std::string out = "{\"ok\":true,\"total\":" + std::to_string( runs_.size() ) + ",\"runs\":[";
    for( std::size_t i = cursor; i < end; ++i ) { if( i != cursor ) out.push_back( ',' ); out += runSummaryJson( runs_[i] ); }
    out += "],\"next_cursor\":" + ( end < runs_.size() ? std::to_string( end ) : "null" ) + "}";
    return out;
}

std::string EvidenceService::runJson( std::string_view runId ) const
{
    const auto* r = findRun( runId );
    if( !r ) return "{\"ok\":false,\"error\":\"run not found\"}";
    const auto ev = eventsForRun( runId );
    std::string out = "{\"ok\":true,\"run\":" + runSummaryJson( *r ) + ",\"events\":[";
    for( std::size_t i = 0; i < ev.size(); ++i ) { if( i ) out.push_back( ',' ); out += ev[i]->raw; }
    out += "]}";
    return out;
}

std::string EvidenceService::productivityJson( std::string_view requestedRun ) const
{
    const std::string runId = requestedRun.empty() ? latestRunId() : std::string( requestedRun );
    const auto* r = findRun( runId );
    if( !r ) return "{\"ok\":false,\"error\":\"run not found\"}";
    const auto ev = eventsForRun( runId );
    const auto repeatedReads = repeatedCount( ev, "file_read" );
    const auto repeatedSearches = repeatedCount( ev, "search" );
    std::set<std::string> changed;
    std::int64_t selected = 0, tests = 0, proofs = 0;
    const EventRecord* tokenEvent = nullptr;
    bool providerToken = false, hostToken = false;
    const EventRecord* testEvent = nullptr;
    const EventRecord* proofEvent = nullptr;
    for( auto* e : ev )
    {
        if( e->type == "edit" && !e->target.empty() ) changed.insert( e->target );
        if( e->type == "test_selection" ) ++selected;
        if( e->type == "test" ) { ++tests; testEvent = e; }
        if( e->type == "proof_check" ) { ++proofs; proofEvent = e; }
        if( e->type == "token_usage" )
        {
            tokenEvent = e;
            providerToken = providerToken || e->source == "provider_reported";
            hostToken = hostToken || e->source == "host_reported";
        }
    }
    const bool tokens = r->inputTokens >= 0 || r->outputTokens >= 0;
    const auto totalTokens = tokens ? std::max<std::int64_t>( 0, r->inputTokens ) + std::max<std::int64_t>( 0, r->outputTokens ) : -1;
    std::string out = "{\"ok\":true,\"run_id\":" + jsonString( runId );
    out += ",\"host\":" + ( r->hostName.empty() ? "null" : jsonString( r->hostName ) );
    out += ",\"model\":" + ( r->model.empty() ? "null" : jsonString( r->model ) );
    out += ",\"metrics\":{";
    const std::string tokenProvenance = providerToken ? "PROVIDER-REPORTED" : hostToken ? "HOST-REPORTED" : provenance( tokenEvent, "OBSERVED" );
    out += "\"tokens_used\":" + metricJson( totalTokens, tokens, tokenProvenance );
    out += ",\"tool_calls\":" + metricJson( r->toolCalls, r->toolCalls > 0, "OBSERVED" );
    out += ",\"repeated_reads\":" + metricJson( repeatedReads, r->fileReads > 0, "DERIVED" );
    out += ",\"repeated_searches\":" + metricJson( repeatedSearches, r->searches > 0, "DERIVED" );
    out += ",\"failed_actions\":" + metricJson( r->failedActions, r->toolCalls > 0, "OBSERVED" );
    out += ",\"memory_recalls\":" + metricJson( r->memoryRecalls, r->memoryRecalls > 0, "OBSERVED" );
    out += ",\"files_changed\":" + metricJson( static_cast<std::int64_t>( changed.size() ), r->edits > 0, "OBSERVED" );
    out += ",\"tests_executed\":" + metricJson( tests, tests > 0, provenance( testEvent ) );
    out += ",\"run_duration_ms\":" + metricJson( r->durationMs, r->durationMs >= 0, "OBSERVED" );
    out += "},\"verification\":{";
    out += "\"tests_selected\":" + metricJson( selected, selected > 0, "STRUCTURAL" );
    out += ",\"tests_executed\":" + metricJson( tests, tests > 0, provenance( testEvent ) );
    out += ",\"proof_checks\":" + metricJson( proofs, proofs > 0, provenance( proofEvent ) );
    out += ",\"proof_passed\":" + std::string( proofEvent ? ( proofEvent->success ? "true" : "false" ) : "null" );
    out += "}}";
    return out;
}

std::string EvidenceService::efficiencyJson( std::string_view runId ) const
{
    const auto ev = eventsForRun( runId );
    if( ev.empty() ) return "{\"ok\":false,\"error\":\"run not found\"}";
    rw::efficiency::RunMeasurements m;
    std::set<std::string> uniqueReads;
    std::set<std::string> touched;
    std::int64_t passed = 0, failed = 0, selected = 0;
    for( auto* e : ev )
    {
        if( e->type == "file_read" ) { m.fileReads = std::max<std::int64_t>( 0, m.fileReads ) + 1; if( !e->target.empty() ) uniqueReads.insert( e->target ); }
        else if( e->type == "search" ) m.searches = std::max<std::int64_t>( 0, m.searches ) + 1;
        else if( e->type == "edit" ) { m.edits = std::max<std::int64_t>( 0, m.edits ) + 1; if( !e->target.empty() ) touched.insert( e->target ); }
        else if( e->type == "tool_call" ) m.toolCalls = std::max<std::int64_t>( 0, m.toolCalls ) + 1;
        else if( e->type == "test_selection" ) ++selected;
        else if( e->type == "test" ) { if( e->success ) ++passed; else ++failed; }
        else if( e->type == "token_usage" )
        {
            if( e->inputTokens >= 0 ) m.inputTokens = std::max<std::int64_t>( 0, m.inputTokens ) + e->inputTokens;
            if( e->outputTokens >= 0 ) m.outputTokens = std::max<std::int64_t>( 0, m.outputTokens ) + e->outputTokens;
        }
    }
    if( m.fileReads >= 0 ) m.uniqueFileReads = static_cast<std::int64_t>( uniqueReads.size() );
    if( m.searches >= 0 ) m.repeatedSearches = repeatedCount( ev, "search" );
    if( !touched.empty() ) m.filesTouched = static_cast<std::int64_t>( touched.size() );
    if( selected > 0 ) m.affectedTests = selected;
    if( passed + failed > 0 ) { m.executedTests = passed + failed; m.passedTests = passed; m.failedTests = failed; }
    const auto metrics = rw::efficiency::computeEngineeringMetrics( m );
    return "{\"ok\":true,\"run_id\":" + jsonString( runId ) + ",\"metrics\":" + rw::efficiency::toJson( metrics ) + "}";
}

std::string EvidenceService::causalJson( std::string_view runId ) const
{
    const auto ev = eventsForRun( runId );
    if( ev.empty() ) return "{\"ok\":false,\"error\":\"run not found\"}";
    rw::causal::Graph graph;
    const auto runNode = graph.upsertNode( rw::causal::NodeType::AgentRun, "run:" + std::string( runId ), std::string( runId ), "OBSERVED" );
    rw::causal::NodeId previous = runNode;
    std::string lastModel;
    std::size_t seq = 0;
    for( auto* e : ev )
    {
        if( !e->model.empty() && e->model != lastModel )
        {
            const auto n = graph.upsertNode( rw::causal::NodeType::Model, "model:" + e->model, e->model, provenance( e ) );
            graph.addEdge( runNode, n, rw::causal::EdgeType::RunsModel, provenance( e ) );
            lastModel = e->model;
        }
        rw::causal::NodeType nt;
        rw::causal::EdgeType et;
        std::string key;
        std::string label = e->target.empty() ? e->type : e->target;
        if( e->type == "tool_call" ) { nt = rw::causal::NodeType::ToolCall; et = rw::causal::EdgeType::Invoked; key = "tool:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "file_read" ) { nt = rw::causal::NodeType::File; et = rw::causal::EdgeType::Read; key = "read:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "search" ) { nt = rw::causal::NodeType::Search; et = rw::causal::EdgeType::Searched; key = "search:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "edit" ) { nt = rw::causal::NodeType::Patch; et = rw::causal::EdgeType::Edited; key = "edit:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "test" ) { nt = rw::causal::NodeType::Test; et = rw::causal::EdgeType::Executed; key = "test:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "proof_check" ) { nt = rw::causal::NodeType::Proof; et = rw::causal::EdgeType::VerifiedBy; key = "proof:" + std::to_string( seq ) + ":" + label; }
        else if( e->type == "memory_recall" ) { nt = rw::causal::NodeType::Memory; et = rw::causal::EdgeType::RememberedAs; key = "memory:" + std::to_string( seq ) + ":" + label; }
        else { ++seq; continue; }
        const auto node = graph.upsertNode( nt, key, label, provenance( e ) + ( e->success ? "" : " · FAILED" ) );
        graph.addEdge( runNode, node, et, provenance( e ) );
        if( previous != runNode && previous != node ) graph.addEdge( previous, node, rw::causal::EdgeType::ObservedBefore, "OBSERVED SEQUENCE; NOT CAUSAL PROOF" );
        previous = node;
        ++seq;
    }
    graph.finalize();
    return "{\"ok\":true,\"run_id\":" + jsonString( runId ) + ",\"graph\":" + graph.toJson() + "}";
}

} // namespace rw::evidence
