// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Built-in project memory: what memory_checkpoint saves and memory_recall finds when no external memory service
// is configured (CODECORTEX_MEMORY_URL unset or unreachable). One JSON object per line in
// <project>/.codecortex/memory.jsonl, next to runs.jsonl, so it travels with the project, needs nothing installed,
// and the dashboard's Memories / Lessons / Actions pages can read it.
//
// Recall is deliberately simple and deterministic: score = number of distinct query words (3+ letters, lower-
// cased) found in the entry's title + content, ties broken newest first. It is a notebook, not a vector store.
//
// Self-contained (C++20, standard library only) so it is unit-tested in isolation (test/verify_local_memory.cpp).

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rw::memory::local
{

struct Entry
{
    std::int64_t tsMs = 0;
    std::string  type;      // fact | pattern | preference | architecture | bug | workflow | lesson | action
    std::string  title;
    std::string  content;
    std::string  project;   // project name/id at save time (informational)
};

inline std::filesystem::path memoryFile( const std::filesystem::path& projectRoot )
{
    return projectRoot / ".codecortex" / "memory.jsonl";
}

namespace detail
{
inline std::string escape( std::string_view s )
{
    std::string o;
    o.reserve( s.size() + 8 );
    for( const unsigned char c : s )
    {
        switch( c )
        {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if( c < 0x20 ) { static const char* h = "0123456789abcdef"; o += "\\u00"; o += h[c >> 4]; o += h[c & 15]; }
                else o.push_back( static_cast<char>( c ) );
        }
    }
    return o;
}

// Value of a top-level string field in one flat JSON object line (the shape this file writes).
inline std::string field( std::string_view line, std::string_view key )
{
    const std::string needle = "\"" + std::string( key ) + "\":\"";
    const auto p = line.find( needle );
    if( p == std::string_view::npos ) return {};
    std::string out;
    for( std::size_t i = p + needle.size(); i < line.size(); ++i )
    {
        const char c = line[i];
        if( c == '"' ) return out;
        if( c == '\\' && i + 1 < line.size() )
        {
            const char n = line[++i];
            if( n == 'n' ) out.push_back( '\n' );
            else if( n == 't' ) out.push_back( '\t' );
            else if( n == 'r' ) out.push_back( '\r' );
            else if( n == 'u' && i + 4 < line.size() ) { out.push_back( '?' ); i += 4; }
            else out.push_back( n );
            continue;
        }
        out.push_back( c );
    }
    return {};
}

inline std::int64_t number( std::string_view line, std::string_view key )
{
    const std::string needle = "\"" + std::string( key ) + "\":";
    const auto p = line.find( needle );
    if( p == std::string_view::npos ) return 0;
    std::int64_t v = 0;
    for( std::size_t i = p + needle.size(); i < line.size() && line[i] >= '0' && line[i] <= '9'; ++i ) v = v * 10 + ( line[i] - '0' );
    return v;
}

inline std::vector<std::string> words( std::string_view text )
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if( cur.size() >= 3 && std::find( out.begin(), out.end(), cur ) == out.end() ) out.push_back( cur );
        cur.clear();
    };
    for( const unsigned char c : text )
    {
        if( std::isalnum( c ) || c == '_' ) cur.push_back( static_cast<char>( std::tolower( c ) ) );
        else flush();
    }
    flush();
    return out;
}
} // namespace detail

inline std::string toJson( const Entry& e )
{
    return "{\"ts_ms\":" + std::to_string( e.tsMs ) + ",\"type\":\"" + detail::escape( e.type ) + "\",\"title\":\""
         + detail::escape( e.title ) + "\",\"content\":\"" + detail::escape( e.content ) + "\",\"project\":\""
         + detail::escape( e.project ) + "\",\"source\":\"local\"}";
}

inline bool append( const std::filesystem::path& projectRoot, const Entry& e )
{
    std::error_code ec;
    const auto file = memoryFile( projectRoot );
    std::filesystem::create_directories( file.parent_path(), ec );
    std::ofstream out( file, std::ios::binary | std::ios::app );
    if( !out ) return false;
    out << toJson( e ) << '\n';
    return static_cast<bool>( out );
}

inline std::vector<Entry> load( const std::filesystem::path& projectRoot, std::size_t maxEntries = 5000 )
{
    std::vector<Entry> all;
    std::ifstream in( memoryFile( projectRoot ), std::ios::binary );
    std::string line;
    while( std::getline( in, line ) )
    {
        if( line.size() < 2 || line.front() != '{' ) continue;
        Entry e;
        e.tsMs = detail::number( line, "ts_ms" );
        e.type = detail::field( line, "type" );
        e.title = detail::field( line, "title" );
        e.content = detail::field( line, "content" );
        e.project = detail::field( line, "project" );
        if( e.title.empty() && e.content.empty() ) continue;
        all.push_back( std::move( e ) );
    }
    if( all.size() > maxEntries ) all.erase( all.begin(), all.end() - static_cast<std::ptrdiff_t>( maxEntries ) );
    return all;
}

// Best `limit` entries for `query`; entries matching no query word are left out. An empty query returns the
// newest entries.
inline std::vector<Entry> recall( const std::vector<Entry>& entries, std::string_view query, std::size_t limit = 5 )
{
    const auto q = detail::words( query );
    std::vector<std::pair<int, std::size_t>> scored;   // (score, index)
    for( std::size_t i = 0; i < entries.size(); ++i )
    {
        int score = 0;
        if( !q.empty() )
        {
            const auto w = detail::words( entries[i].title + " " + entries[i].content + " " + entries[i].type );
            for( const auto& t : q ) if( std::find( w.begin(), w.end(), t ) != w.end() ) ++score;
            if( score == 0 ) continue;
        }
        scored.emplace_back( score, i );
    }
    std::stable_sort( scored.begin(), scored.end(), [&]( const auto& a, const auto& b ) {
        if( a.first != b.first ) return a.first > b.first;
        return entries[a.second].tsMs > entries[b.second].tsMs;
    } );
    std::vector<Entry> out;
    for( std::size_t k = 0; k < scored.size() && out.size() < limit; ++k ) out.push_back( entries[scored[k].second] );
    return out;
}

inline std::string recallText( const std::vector<Entry>& found )
{
    if( found.empty() ) return "No saved project memory matches this yet.";
    std::ostringstream o;
    for( const auto& e : found )
        o << "- [" << ( e.type.empty() ? "fact" : e.type ) << "] " << e.title << ( e.content.empty() ? "" : ": " ) << e.content << "\n";
    return o.str();
}

} // namespace rw::memory::local
