// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <string>

namespace rw::ui
{

struct ServerConfig
{
    std::string root;
    std::string telemetryPath;
    std::string executable;
    std::string version;
    bool mcpActive = false;
    std::string mcpTransport = "none";   // none | stdio | streamable_http
    bool mcpLocalOnly = true;
    int port = 7331;
};

int runServer( const ServerConfig& config );

} // namespace rw::ui
