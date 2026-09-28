// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace rw::memory
{

struct Result
{
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

struct RecallQuery
{
    std::string query;
    std::string project;
    std::string agentId;
    std::string sessionId;
    bool includeLessons = true;
    std::size_t limit = 5;
};

struct RememberRequest
{
    std::string content;
    std::string project;
    std::string agentId;
    std::string type = "fact";
};

struct CheckpointRequest
{
    std::string name;
    std::string description;
    std::string type = "codecortex-run";
};

class MemoryProvider
{
  public:
    virtual ~MemoryProvider() = default;
    virtual Result health() = 0;
    virtual Result recall( const RecallQuery& query ) = 0;
    virtual Result remember( const RememberRequest& request ) = 0;
    virtual Result checkpoint( const CheckpointRequest& request ) = 0;
};

class NoopMemoryProvider final : public MemoryProvider
{
  public:
    Result health() override { return { false, 0, {}, "memory provider disabled" }; }
    Result recall( const RecallQuery& ) override { return health(); }
    Result remember( const RememberRequest& ) override { return health(); }
    Result checkpoint( const CheckpointRequest& ) override { return health(); }
};

} // namespace rw::memory
