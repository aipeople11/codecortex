// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include "events.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace rw::telemetry
{

class Collector
{
  public:
    Collector() = default;
    explicit Collector( std::string_view outputPath );
    ~Collector();

    Collector( const Collector& ) = delete;
    Collector& operator=( const Collector& ) = delete;
    Collector( Collector&& other ) noexcept;
    Collector& operator=( Collector&& other ) noexcept;

    [[nodiscard]] bool enabled() const noexcept { return file_ != nullptr; }
    [[nodiscard]] std::string_view path() const noexcept { return path_; }

    bool record( const Event& event ) noexcept;

  private:
    void close() noexcept;

    std::FILE* file_ = nullptr;
    std::string path_;
    std::mutex mutex_;
};

class RunSession
{
  public:
    RunSession( std::string_view outputPath, int argc, char** argv, std::string_view root );
    ~RunSession();

    RunSession( const RunSession& ) = delete;
    RunSession& operator=( const RunSession& ) = delete;

    [[nodiscard]] bool enabled() const noexcept { return collector_.enabled(); }
    [[nodiscard]] std::string_view runId() const noexcept { return runId_; }

    bool record( Event event ) noexcept;
    void finish( int exitCode ) noexcept;

  private:
    Collector collector_;
    std::string runId_;
    std::chrono::steady_clock::time_point started_{};
    bool finished_ = false;
};

// Explicit helper for integrations (MCP/hooks/UI) that know token usage independently of the
// CodeCortex process. This avoids pretending the CLI can infer provider token counts itself.
bool appendTokenUsage( std::string_view outputPath, std::string_view runId,
                       std::int64_t inputTokens, std::int64_t outputTokens,
                       std::string_view provider = {} ) noexcept;

// Shared helpers for long-lived integrations such as MCP. They deliberately expose only
// bounded, provider-neutral telemetry: CodeCortex never invents model token counts.
std::string makeIntegrationRunId( std::string_view prefix = "integration" );
bool appendToolCall( std::string_view outputPath, std::string_view runId,
                     std::string_view toolName, std::int64_t durationMs,
                     bool success, std::string_view transport = {} ) noexcept;

} // namespace rw::telemetry
