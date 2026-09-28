// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace rw::project
{

struct Identity
{
    std::string root;
    std::string projectId;
    std::string projectName;
    std::string repoId;
    std::string sessionId;
    std::string agentId;
};

inline std::string envValue( const char* name )
{
    if( const char* value = std::getenv( name ); value && *value ) return value;
    return {};
}

inline std::string trim( std::string value )
{
    const auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = value.find_last_not_of( " \t\r\n" );
    return value.substr( first, last - first + 1 );
}

inline std::filesystem::path canonicalRoot( std::string_view root )
{
    std::error_code ec;
    std::filesystem::path p( root.empty() ? "." : std::string( root ) );
    const auto canonical = std::filesystem::weakly_canonical( p, ec );
    return ec ? std::filesystem::absolute( p, ec ) : canonical;
}

inline std::filesystem::path gitDirFor( const std::filesystem::path& root )
{
    std::error_code ec;
    const auto dotGit = root / ".git";
    if( std::filesystem::is_directory( dotGit, ec ) && !ec ) return dotGit;
    ec.clear();
    if( !std::filesystem::is_regular_file( dotGit, ec ) || ec ) return {};

    std::ifstream in( dotGit );
    std::string line;
    if( !std::getline( in, line ) ) return {};
    constexpr std::string_view prefix = "gitdir:";
    if( line.size() < prefix.size() || line.compare( 0, prefix.size(), prefix ) != 0 ) return {};
    std::filesystem::path gitDir = trim( line.substr( prefix.size() ) );
    if( gitDir.is_relative() ) gitDir = root / gitDir;
    return std::filesystem::weakly_canonical( gitDir, ec );
}

inline std::string originUrlFor( const std::filesystem::path& root )
{
    const auto gitDir = gitDirFor( root );
    if( gitDir.empty() ) return {};
    std::ifstream in( gitDir / "config" );
    if( !in ) return {};

    bool inOrigin = false;
    std::string line;
    while( std::getline( in, line ) )
    {
        const std::string t = trim( line );
        if( t.empty() || t[0] == '#' || t[0] == ';' ) continue;
        if( t.front() == '[' )
        {
            inOrigin = t == "[remote \"origin\"]";
            continue;
        }
        if( !inOrigin ) continue;
        const auto eq = t.find( '=' );
        if( eq == std::string::npos ) continue;
        if( trim( t.substr( 0, eq ) ) == "url" ) return trim( t.substr( eq + 1 ) );
    }
    return {};
}

inline std::string nameFromRemote( std::string remote )
{
    if( remote.empty() ) return {};
    while( !remote.empty() && remote.back() == '/' ) remote.pop_back();
    const auto slash = remote.find_last_of( "/:" );
    std::string name = slash == std::string::npos ? remote : remote.substr( slash + 1 );
    if( name.size() > 4 && name.compare( name.size() - 4, 4, ".git" ) == 0 ) name.resize( name.size() - 4 );
    return name;
}

inline Identity identify( std::string_view root )
{
    Identity id;
    const auto canonical = canonicalRoot( root );
    id.root = canonical.string();

    id.repoId = envValue( "CODECORTEX_REPO_ID" );
    if( id.repoId.empty() ) id.repoId = originUrlFor( canonical );

    id.projectName = envValue( "CODECORTEX_PROJECT_NAME" );
    if( id.projectName.empty() ) id.projectName = nameFromRemote( id.repoId );
    if( id.projectName.empty() ) id.projectName = canonical.filename().string();
    if( id.projectName.empty() ) id.projectName = "workspace";

    id.projectId = envValue( "CODECORTEX_PROJECT_ID" );
    if( id.projectId.empty() )
    {
        // CodeCortex Memory expects a stable canonical project identifier, not an absolute local path.
        // Prefer the repository remote because it survives machine/path changes; fall back to the
        // repository name for local/non-git workspaces. Callers can override the fallback explicitly.
        id.projectId = !id.repoId.empty() ? "repo:" + id.repoId : "local:" + id.projectName;
    }

    id.sessionId = envValue( "CODECORTEX_SESSION_ID" );
    id.agentId = envValue( "CODECORTEX_AGENT_ID" );
    return id;
}

} // namespace rw::project
