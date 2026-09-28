// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include "causal/causal_graph.h"
#include "efficiency/engineering_metrics.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rw::evidence
{

struct EventRecord
{
    std::string raw;
    std::string type;
    std::string runId;
    std::string projectId;
    std::string projectName;
    std::string repoId;
    std::string sessionId;
    std::string model;
    std::string hostName;
    std::string source;
    std::string target;
    std::string detail;
    std::int64_t timestampMs = 0;
    std::int64_t durationMs = -1;
    std::int64_t inputTokens = -1;
    std::int64_t outputTokens = -1;
    std::int64_t numericValue = -1;
    bool success = true;
};

struct RunSummary
{
    std::string runId;
    std::string projectId;
    std::string projectName;
    std::string sessionId;
    std::string hostName;
    std::string model;
    std::int64_t startedMs = 0;
    std::int64_t finishedMs = 0;
    std::int64_t durationMs = -1;
    std::int64_t eventCount = 0;
    std::int64_t toolCalls = 0;
    std::int64_t fileReads = 0;
    std::int64_t searches = 0;
    std::int64_t edits = 0;
    std::int64_t tests = 0;
    std::int64_t failedActions = 0;
    std::int64_t memoryRecalls = 0;
    std::int64_t inputTokens = -1;
    std::int64_t outputTokens = -1;
    bool finished = false;
    bool success = true;
};

class EvidenceService
{
  public:
    explicit EvidenceService( std::string telemetryPath = {} );

    void setTelemetryPath( std::string path );
    bool refresh();

    [[nodiscard]] bool available() const noexcept { return available_; }
    [[nodiscard]] std::size_t eventCount() const noexcept { return events_.size(); }
    [[nodiscard]] std::size_t runCount() const noexcept { return runs_.size(); }

    std::string evidenceJson( std::size_t limit, std::size_t cursor ) const;
    std::string runsJson( std::size_t limit, std::size_t cursor ) const;
    std::string runJson( std::string_view runId ) const;
    std::string productivityJson( std::string_view runId = {} ) const;
    std::string efficiencyJson( std::string_view runId ) const;
    std::string causalJson( std::string_view runId ) const;

  private:
    std::string telemetryPath_;
    std::uintmax_t indexedBytes_ = 0;
    // The telemetry writer can be between writes when refresh() runs. Keep the
    // incomplete final JSONL record so it is not skipped on the next refresh.
    std::string partialLine_;
    std::vector<EventRecord> events_;
    std::vector<RunSummary> runs_;
    bool available_ = false;

    void reset();
    void appendChunk( std::string_view bytes );
    void rebuildRuns();
    [[nodiscard]] const RunSummary* findRun( std::string_view runId ) const noexcept;
    [[nodiscard]] std::vector<const EventRecord*> eventsForRun( std::string_view runId ) const;
    [[nodiscard]] std::string latestRunId() const;
};

} // namespace rw::evidence
