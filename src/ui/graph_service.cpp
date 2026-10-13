// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "graph_service.h"

#include "commands/safe_commands.h"
#include "infra/jsonesc.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace rw::ui
{
namespace
{
std::string jsonString( std::string_view value )
{
    std::string out;
    out.push_back( '"' );
    rw::jsonesc::escapeInto( value, out, true, true, true );
    out.push_back( '"' );
    return out;
}

std::string trimObject( std::string text )
{
    const auto first = text.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = text.find_last_not_of( " \t\r\n" );
    text = text.substr( first, last - first + 1 );
    if( text.size() < 2 || text.front() != '{' || text.back() != '}' ) return {};
    return text;
}

std::string xmlUnescape( std::string s )
{
    struct R { const char* from; const char* to; };
    static constexpr R rules[] = {
        { "&quot;", "\"" }, { "&apos;", "'" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&amp;", "&" }
    };
    for( const auto& r : rules )
    {
        std::size_t pos = 0;
        while( ( pos = s.find( r.from, pos ) ) != std::string::npos )
        {
            s.replace( pos, std::char_traits<char>::length( r.from ), r.to );
            pos += std::char_traits<char>::length( r.to );
        }
    }
    return s;
}

std::string attr( std::string_view xml, std::string_view name )
{
    const std::string needle = std::string( name ) + "=\"";
    const auto p = xml.find( needle );
    if( p == std::string_view::npos ) return {};
    const auto start = p + needle.size();
    const auto end = xml.find( '"', start );
    if( end == std::string_view::npos ) return {};
    return xmlUnescape( std::string( xml.substr( start, end - start ) ) );
}

std::string stripLine( std::string path )
{
    // CodeCortex paths commonly use path:line. Only remove a numeric terminal suffix.
    const auto colon = path.rfind( ':' );
    if( colon == std::string::npos || colon + 1 >= path.size() ) return path;
    if( std::all_of( path.begin() + static_cast<std::ptrdiff_t>( colon + 1 ), path.end(),
                     []( unsigned char c ) { return std::isdigit( c ) != 0; } ) )
        path.resize( colon );
    if( path.rfind( "./", 0 ) == 0 ) path.erase( 0, 2 );
    return path;
}

std::string resolveSymbolFile( const GraphServiceConfig& cfg, std::string_view target )
{
    const auto plan = rw::commands::buildSafePlan( cfg.executable, cfg.root, "edit_check", target );
    if( !plan.ok ) return {};
    const auto ex = rw::commands::execute( plan, 15000, 512 * 1024 );
    if( ex.stdoutText.empty() ) return {};
    return stripLine( attr( ex.stdoutText, "p" ) );
}

std::string runJsonCommand( const GraphServiceConfig& cfg, std::string_view name, std::string_view target )
{
    const auto plan = rw::commands::buildSafePlan( cfg.executable, cfg.root, name, target );
    if( !plan.ok ) return {};
    const auto ex = rw::commands::execute( plan, 30000, 2 * 1024 * 1024 );
    // test-gate intentionally exits 4 when obligations exist; its JSON is still the authoritative answer.
    return trimObject( ex.stdoutText );
}

std::string runTextCommand( const GraphServiceConfig& cfg, std::string_view name, std::string_view target = {} )
{
    const auto plan = rw::commands::buildSafePlan( cfg.executable, cfg.root, name, target );
    if( !plan.ok ) return {};
    const auto ex = rw::commands::execute( plan, 30000, 2 * 1024 * 1024 );
    return ex.stdoutText;
}

std::string commitsJson( const GraphServiceConfig& cfg, std::string_view file )
{
    if( file.empty() ) return "[]";
    const auto plan = rw::commands::buildGitHistoryPlan( cfg.root, file, 8 );
    if( !plan.ok ) return "[]";
    const auto ex = rw::commands::execute( plan, 10000, 512 * 1024 );
    if( !ex.ok && ex.stdoutText.empty() ) return "[]";

    std::string out = "[";
    bool first = true;
    std::size_t pos = 0;
    while( pos < ex.stdoutText.size() )
    {
        const auto nl = ex.stdoutText.find( '\n', pos );
        const auto end = nl == std::string::npos ? ex.stdoutText.size() : nl;
        std::string_view line( ex.stdoutText.data() + pos, end - pos );
        std::string_view parts[5];
        std::size_t p = 0;
        bool good = true;
        for( int i = 0; i < 4; ++i )
        {
            const auto sep = line.find( '\x1f', p );
            if( sep == std::string_view::npos ) { good = false; break; }
            parts[i] = line.substr( p, sep - p ); p = sep + 1;
        }
        if( good )
        {
            parts[4] = line.substr( p );
            if( !first ) out.push_back( ',' );
            first = false;
            out += "{\"sha\":" + jsonString( parts[0] )
                + ",\"short\":" + jsonString( parts[1] )
                + ",\"author\":" + jsonString( parts[2] )
                + ",\"time\":" + jsonString( parts[3] )
                + ",\"subject\":" + jsonString( parts[4] ) + "}";
        }
        if( nl == std::string::npos ) break;
        pos = nl + 1;
    }
    out += "]";
    return out;
}

std::string commandPayload( std::string_view raw )
{
    return jsonString( raw );
}

// Reads a bare top-level JSON scalar field, e.g. "shown":40 or "capped":true, out of the
// already-trimmed { ... } object `--callers=`/`--callees= --json` emit. Not a general JSON
// parser: it only needs to pull four known, always-top-level, always-scalar fields
// (capped/shown/total/has_more) back out of runJsonCommand's untouched passthrough so the
// dashboard can disclose a paginated/truncated caller or callee list instead of silently
// showing fewer rows than the CLI itself reports exist. Returns "" when the field is absent,
// matching attr()'s own not-found convention.
std::string jsonScalar( std::string_view obj, std::string_view name )
{
    const std::string needle = std::string( "\"" ) + std::string( name ) + "\":";
    const auto p = obj.find( needle );
    if( p == std::string_view::npos ) return {};
    auto start = p + needle.size();
    auto end = start;
    while( end < obj.size() && obj[end] != ',' && obj[end] != '}' ) ++end;
    // Trim any stray whitespace defensively; the emitter doesn't pad, but nothing guarantees
    // that forever, and a bare substring copy costs nothing extra here.
    while( start < end && ( obj[start] == ' ' || obj[start] == '\t' ) ) ++start;
    while( end > start && ( obj[end - 1] == ' ' || obj[end - 1] == '\t' ) ) --end;
    return std::string( obj.substr( start, end - start ) );
}

// Builds the disclosure object the dashboard reads to show "N of M shown" instead of
// silently rendering a truncated page as if it were the complete caller/callee list.
// Absent (not merely false/zero) when the underlying verb reported no pagination fields at
// all, so a caller/callee list from a CLI version that never capped keeps costing 0 bytes —
// the same "absence is meaningful" convention the CLI's own JSON already follows for its
// bodyless_defs/hop_tested-style fields (see verbs_navigate.h).
std::string truncationJson( std::string_view rowsObj )
{
    const std::string capped   = jsonScalar( rowsObj, "capped" );
    const std::string shown    = jsonScalar( rowsObj, "shown" );
    const std::string total    = jsonScalar( rowsObj, "total" );
    const std::string hasMore  = jsonScalar( rowsObj, "has_more" );
    if( capped.empty() && shown.empty() && total.empty() && hasMore.empty() ) return {};
    std::string out = "{";
    bool first = true;
    const auto add = [ & ]( std::string_view key, const std::string& value )
    {
        if( value.empty() ) return;
        if( !first ) out += ",";
        out += "\"" + std::string( key ) + "\":" + value;
        first = false;
    };
    add( "capped", capped );
    add( "shown", shown );
    add( "total", total );
    add( "has_more", hasMore );
    out += "}";
    return out;
}
}

std::string GraphService::symbol( std::string_view target ) const
{
    if( target.empty() ) return "{\"ok\":false,\"error\":\"symbol is required\"}";
    const std::string callers = runJsonCommand( cfg_, "callers", target );
    const std::string callees = runJsonCommand( cfg_, "callees", target );
    if( callers.empty() && callees.empty() ) return "{\"ok\":false,\"error\":\"symbol graph unavailable\"}";

    const std::string file = resolveSymbolFile( cfg_, target );
    const std::string tests = file.empty() ? std::string() : runJsonCommand( cfg_, "tests_for_file", file );
    const std::string commits = commitsJson( cfg_, file );

    // The underlying CLI verbs page/cap their rows and SAY SO (capped=/shown=/total=/has_more=
    // on the same object callers/callees already are) — but until now nothing between that
    // verb and the browser read those fields, so a symbol with more callers/callees than one
    // page holds rendered as if the page were the whole answer, with no sign anything was cut.
    // Surfacing it here, once, keeps the fix at the one seam that already sees both the raw
    // verb output and the shaped API response, rather than asking the frontend to reach back
    // into callers/callees' own internals to find it.
    const std::string callersTruncation = truncationJson( callers );
    const std::string calleesTruncation = truncationJson( callees );

    std::string out = "{\"ok\":true,\"target\":" + jsonString( target );
    out += ",\"file\":" + ( file.empty() ? std::string( "null" ) : jsonString( file ) );
    out += ",\"callers\":" + ( callers.empty() ? std::string( "null" ) : callers );
    out += ",\"callees\":" + ( callees.empty() ? std::string( "null" ) : callees );
    if( !callersTruncation.empty() ) out += ",\"callers_truncation\":" + callersTruncation;
    if( !calleesTruncation.empty() ) out += ",\"callees_truncation\":" + calleesTruncation;
    out += ",\"tests\":" + ( tests.empty() ? std::string( "null" ) : tests );
    out += ",\"commits\":" + commits + "}";
    return out;
}

std::string GraphService::architecture() const
{
    const std::string communities = runTextCommand( cfg_, "communities" );
    return "{\"ok\":" + std::string( communities.empty() ? "false" : "true" )
         + ",\"communities_xml\":" + commandPayload( communities ) + "}";
}

std::string GraphService::community( std::string_view id ) const
{
    const std::string raw = runTextCommand( cfg_, "community", id );
    return "{\"ok\":" + std::string( raw.empty() ? "false" : "true" )
         + ",\"community_xml\":" + commandPayload( raw ) + "}";
}

std::string GraphService::path( std::string_view from, std::string_view to ) const
{
    if( from.empty() || to.empty() ) return "{\"ok\":false,\"error\":\"from and to are required\"}";
    const std::string raw = runTextCommand( cfg_, "path", std::string( from ) + "," + std::string( to ) );
    return "{\"ok\":" + std::string( raw.empty() ? "false" : "true" )
         + ",\"path_xml\":" + commandPayload( raw ) + "}";
}

std::string GraphService::dependencies() const
{
    const std::string raw = runTextCommand( cfg_, "deps" );
    return "{\"ok\":" + std::string( raw.empty() ? "false" : "true" )
         + ",\"deps_xml\":" + commandPayload( raw ) + "}";
}
} // namespace rw::ui
