// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rw::commands
{

struct Plan
{
    bool ok = false;
    std::vector<std::string> argv;
    std::string error;
};

struct Execution
{
    bool ok = false;
    int exitCode = -1;
    bool timedOut = false;
    std::string stdoutText;
    std::string stderrText;
    std::string error;
};

Plan buildSafePlan( std::string_view executable, std::string_view root,
                    std::string_view command, std::string_view target = {} );
Plan buildGitHistoryPlan( std::string_view root, std::string_view file, int limit = 8 );
Execution execute( const Plan& plan, int timeoutMs = 15000, std::size_t outputCap = 8 * 1024 * 1024 );

} // namespace rw::commands
