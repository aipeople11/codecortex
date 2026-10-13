// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace rw::telemetry
{

enum class EventType : std::uint8_t
{
    RunStarted,
    RunFinished,
    FileRead,
    Search,
    ToolCall,
    Edit,
    Test,
    TestSelection,
    ProofCheck,
    TokenUsage,
    MemoryRecall,
    Checkpoint,
    ContextReturned   // what one CodeCortex MCP answer put into the agent's context (size, estimate, savings)
};

inline constexpr std::string_view eventTypeName( EventType type ) noexcept
{
    switch( type )
    {
        case EventType::RunStarted:  return "run_started";
        case EventType::RunFinished: return "run_finished";
        case EventType::FileRead:    return "file_read";
        case EventType::Search:      return "search";
        case EventType::ToolCall:    return "tool_call";
        case EventType::Edit:        return "edit";
        case EventType::Test:        return "test";
        case EventType::TestSelection:return "test_selection";
        case EventType::ProofCheck:  return "proof_check";
        case EventType::TokenUsage:  return "token_usage";
        case EventType::MemoryRecall:return "memory_recall";
        case EventType::Checkpoint:  return "checkpoint";
        case EventType::ContextReturned: return "context_returned";
    }
    return "unknown";
}

struct Event
{
    EventType type = EventType::ToolCall;
    std::string runId;
    std::string projectId;
    std::string projectName;
    std::string repoId;
    std::string sessionId;
    std::string turnId;
    std::string toolCallId;
    std::string model;
    std::string agentId;
    std::string hostName;
    std::string hostVersion;
    std::string connectionId;
    std::string source; // observed | structural | host_reported | provider_reported | derived
    std::string workspaceRoot;
    std::string target;
    std::string detail;
    std::int64_t timestampMs = 0;
    std::int64_t durationMs = -1;
    std::int64_t inputTokens = -1;
    std::int64_t outputTokens = -1;
    std::int64_t numericValue = -1;
    // Host-reported prompt-cache usage (TokenUsage from a Claude Code transcript). -1 = not reported.
    std::int64_t cacheReadTokens = -1;
    std::int64_t cacheWrite5mTokens = -1;
    std::int64_t cacheWrite1hTokens = -1;
    std::int64_t requestCount = -1;          // API requests folded into this TokenUsage event
    // ContextReturned: CodeCortex's OWN answer. estTokens is an ESTIMATE (bytes / rate, or the answer's own
    // est_tokens= header); avoidedTokensEst is the estimated size of the files the answer covers, i.e. what
    // reading them whole would have cost. Both are labelled "derived" — never mixed with host-reported counts.
    std::int64_t responseBytes = -1;
    std::int64_t estTokens = -1;
    std::int64_t avoidedTokensEst = -1;
    std::int64_t coveredFiles = -1;
    // Retry chain: 0 = not tracked; 1 = first attempt; N = the (N-1)th retry of retryOfToolCallId's call.
    std::string retryOfToolCallId;
    std::int64_t attemptNumber = 0;
    bool success = true;
};

inline std::int64_t unixTimeMs() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch() )
        .count();
}

} // namespace rw::telemetry
