// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <atomic>
#include <string>
#include <vector>

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
    std::vector<std::string> workspaceRoots;   // 2+ when started as a multi-project workspace (shown in the header)
};

int runServer( const ServerConfig& config );

// The port the dashboard actually bound (0 = not bound yet). runServer tries cfg.port first and, when another
// process already holds it (an older CodeCortex still running), the next free port up to cfg.port + 9 — so a
// new session never silently shows an old session's dashboard. codecortex_status reports this port.
inline std::atomic<int>& activePort()
{
    static std::atomic<int> p{ 0 };
    return p;
}

} // namespace rw::ui
