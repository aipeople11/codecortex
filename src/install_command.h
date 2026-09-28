// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace rw
{
namespace installcmd
{

inline std::string shellQuote( std::string_view value )
{
    std::string out = "'";
    for( const char c : value )
    {
        if( c == '\'' ) out += "'\\''";
        else out.push_back( c );
    }
    out += "'";
    return out;
}

inline std::filesystem::path findOnboardScript( std::string_view executablePath )
{
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path( ec );
    if( !ec )
    {
        const auto local = cwd / "scripts" / "onboard.sh";
        if( std::filesystem::is_regular_file( local, ec ) && !ec ) return local;
    }
    ec.clear();
    const std::filesystem::path exe( executablePath );
    if( !exe.empty() )
    {
        const auto staged = exe.parent_path().parent_path() / "share" / "codecortex" / "scripts" / "onboard.sh";
        if( std::filesystem::is_regular_file( staged, ec ) && !ec ) return staged;
    }
    return {};
}

inline int run( int argc, char** argv, std::string_view executablePath )
{
#if defined(_WIN32)
    (void)argc; (void)argv; (void)executablePath;
    std::fputs( "codecortex install: automated host onboarding currently supports macOS/Linux; use `codecortex wrap <agent>` on Windows.\n", stderr );
    return 2;
#else
    const auto script = findOnboardScript( executablePath );
    if( script.empty() )
    {
        std::fputs( "codecortex install: onboarding assets are missing. Reinstall CodeCortex or run from the source checkout.\n", stderr );
        return 1;
    }
    std::string cmd = "/bin/bash " + shellQuote( script.string() ) + " --binary " + shellQuote( executablePath );
    for( int i = 2; i < argc; ++i )
    {
        if( argv[i] ) cmd += " " + shellQuote( argv[i] );
    }
    return std::system( cmd.c_str() );
#endif
}

} // namespace installcmd
} // namespace rw
