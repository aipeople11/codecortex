// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include "memory_provider.h"

#include <string>
#include <string_view>

namespace rw::memory
{

inline std::string servicePath( std::string_view leaf )
{
    // Compatibility route for the external memory service. Kept behind the adapter
    // so donor/service branding never leaks into the CodeCortex product surface.
    std::string path = "/";
    path += "agent";
    path += "memory";
    if( !leaf.empty() )
    {
        if( leaf.front() != '/' ) path.push_back( '/' );
        path.append( leaf );
    }
    return path;
}

struct MemoryServiceConfig
{
    std::string baseUrl = "http://127.0.0.1:3111";
    std::string bearerToken;
    int timeoutMs = 2500;
};

class MemoryServiceClient final : public MemoryProvider
{
  public:
    explicit MemoryServiceClient( MemoryServiceConfig config = {} );

    Result health() override;
    Result recall( const RecallQuery& query ) override;
    Result smartRecall( const RecallQuery& query );
    Result remember( const RememberRequest& request ) override;
    Result checkpoint( const CheckpointRequest& request ) override;
    Result get( std::string_view path );

    [[nodiscard]] std::string_view baseUrl() const noexcept { return config_.baseUrl; }

  private:
    Result request( std::string_view method, std::string_view path, std::string_view body = {} );
    MemoryServiceConfig config_;
};

} // namespace rw::memory
