// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#pragma once

// Reads an OTLP/HTTP JSON "logs" request (what Claude Code sends when OTEL_LOGS_EXPORTER=otlp and
// OTEL_EXPORTER_OTLP_PROTOCOL=http/json) into small Record values.
//
// PRIVACY: this is the single place where received data is filtered. Only the attribute keys in kAllowed
// survive parsing; everything else (prompt text, replies, tool input, file contents, e-mail, account ids, ...)
// is dropped while reading and is never stored, logged or forwarded. See docs/OTEL_INTEGRATION_DESIGN.md section 5.
//
// Self-contained on purpose (C++20, standard library only) so it is unit-testable in isolation
// (test/verify_otlp_json.cpp) and cannot pull a platform header into the telemetry core.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rw::telemetry::otlp
{

// Attribute keys we keep. Anything not listed is discarded on read.
inline constexpr std::string_view kAllowed[] = {
    "event.name", "session.id", "prompt.id", "request_id",
    "model", "query_source", "speed",
    "input_tokens", "output_tokens", "cache_read_tokens", "cache_creation_tokens",
    "cost_usd", "cost_usd_micros", "duration_ms",
    "tool_name", "tool_use_id", "success", "error_type", "tool_result_size_bytes", "mcp_server_scope",
};

inline bool isAllowed( std::string_view key ) noexcept
{
    for( const std::string_view k : kAllowed ) if( k == key ) return true;
    return false;
}

struct Record
{
    std::string name;            // "api_request", "tool_result", "user_prompt", ... ("claude_code." prefix removed)
    std::int64_t tsMs = 0;       // 0 when the report carried no time
    std::unordered_map<std::string, std::string> attrs;   // allow-listed keys only

    [[nodiscard]] std::string get( std::string_view key ) const
    {
        const auto it = attrs.find( std::string( key ) );
        return it == attrs.end() ? std::string() : it->second;
    }
};

namespace detail
{

struct JVal
{
    enum class T : std::uint8_t { Null, Bool, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b = false;
    std::string s;                    // string value, or the original text of a number
    std::vector<JVal> a;              // array items
    std::vector<std::string> keys;    // object keys (parallel to vals)
    std::vector<JVal> vals;           // object values

    [[nodiscard]] const JVal* get( std::string_view k ) const
    {
        if( t != T::Obj ) return nullptr;
        for( std::size_t i = 0; i < keys.size(); ++i ) if( keys[i] == k ) return &vals[i];
        return nullptr;
    }
};

class Parser
{
  public:
    explicit Parser( std::string_view s ) : s_( s ) {}

    bool parse( JVal& out )
    {
        ws();
        if( !value( out, 0 ) ) return false;
        ws();
        return i_ == s_.size();
    }

  private:
    std::string_view s_;
    std::size_t i_ = 0;

    void ws()
    {
        while( i_ < s_.size() && ( s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n' ) ) ++i_;
    }

    bool lit( std::string_view w )
    {
        if( s_.substr( i_, w.size() ) != w ) return false;
        i_ += w.size();
        return true;
    }

    bool value( JVal& v, int depth )
    {
        if( depth > 24 || i_ >= s_.size() ) return false;
        const char c = s_[i_];
        if( c == '{' ) return object( v, depth );
        if( c == '[' ) return array( v, depth );
        if( c == '"' ) { v.t = JVal::T::Str; return str( v.s ); }
        if( c == 't' ) { if( !lit( "true" ) ) return false;  v.t = JVal::T::Bool; v.b = true;  return true; }
        if( c == 'f' ) { if( !lit( "false" ) ) return false; v.t = JVal::T::Bool; v.b = false; return true; }
        if( c == 'n' ) { if( !lit( "null" ) ) return false;  v.t = JVal::T::Null; return true; }
        return number( v );
    }

    bool number( JVal& v )
    {
        const std::size_t st = i_;
        while( i_ < s_.size() )
        {
            const char c = s_[i_];
            if( ( c >= '0' && c <= '9' ) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' ) ++i_;
            else break;
        }
        if( i_ == st ) return false;
        v.t = JVal::T::Num;
        v.s.assign( s_.substr( st, i_ - st ) );
        return true;
    }

    static void appendUtf8( std::string& out, unsigned cp )
    {
        if( cp < 0x80 ) out.push_back( static_cast<char>( cp ) );
        else if( cp < 0x800 ) { out.push_back( static_cast<char>( 0xC0 | ( cp >> 6 ) ) ); out.push_back( static_cast<char>( 0x80 | ( cp & 0x3F ) ) ); }
        else if( cp < 0x10000 )
        {
            out.push_back( static_cast<char>( 0xE0 | ( cp >> 12 ) ) );
            out.push_back( static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) ) );
            out.push_back( static_cast<char>( 0x80 | ( cp & 0x3F ) ) );
        }
        else
        {
            out.push_back( static_cast<char>( 0xF0 | ( cp >> 18 ) ) );
            out.push_back( static_cast<char>( 0x80 | ( ( cp >> 12 ) & 0x3F ) ) );
            out.push_back( static_cast<char>( 0x80 | ( ( cp >> 6 ) & 0x3F ) ) );
            out.push_back( static_cast<char>( 0x80 | ( cp & 0x3F ) ) );
        }
    }

    bool hex4( unsigned& cp )
    {
        if( i_ + 4 > s_.size() ) return false;
        cp = 0;
        for( int k = 0; k < 4; ++k )
        {
            const char c = s_[i_++];
            cp <<= 4;
            if( c >= '0' && c <= '9' ) cp |= static_cast<unsigned>( c - '0' );
            else if( c >= 'a' && c <= 'f' ) cp |= static_cast<unsigned>( c - 'a' + 10 );
            else if( c >= 'A' && c <= 'F' ) cp |= static_cast<unsigned>( c - 'A' + 10 );
            else return false;
        }
        return true;
    }

    bool str( std::string& out )
    {
        if( i_ >= s_.size() || s_[i_] != '"' ) return false;
        ++i_;
        out.clear();
        while( i_ < s_.size() )
        {
            const char c = s_[i_++];
            if( c == '"' ) return true;
            if( c != '\\' ) { out.push_back( c ); continue; }
            if( i_ >= s_.size() ) return false;
            const char e = s_[i_++];
            switch( e )
            {
                case '"': out.push_back( '"' ); break;
                case '\\': out.push_back( '\\' ); break;
                case '/': out.push_back( '/' ); break;
                case 'b': out.push_back( '\b' ); break;
                case 'f': out.push_back( '\f' ); break;
                case 'n': out.push_back( '\n' ); break;
                case 'r': out.push_back( '\r' ); break;
                case 't': out.push_back( '\t' ); break;
                case 'u':
                {
                    unsigned cp = 0;
                    if( !hex4( cp ) ) return false;
                    if( cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u' )
                    {
                        const std::size_t save = i_;
                        i_ += 2;
                        unsigned lo = 0;
                        if( hex4( lo ) && lo >= 0xDC00 && lo <= 0xDFFF ) cp = 0x10000 + ( ( cp - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
                        else i_ = save;
                    }
                    appendUtf8( out, cp );
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool array( JVal& v, int depth )
    {
        v.t = JVal::T::Arr;
        ++i_;   // '['
        ws();
        if( i_ < s_.size() && s_[i_] == ']' ) { ++i_; return true; }
        for( ;; )
        {
            JVal item;
            ws();
            if( !value( item, depth + 1 ) ) return false;
            v.a.push_back( std::move( item ) );
            ws();
            if( i_ >= s_.size() ) return false;
            if( s_[i_] == ',' ) { ++i_; continue; }
            if( s_[i_] == ']' ) { ++i_; return true; }
            return false;
        }
    }

    bool object( JVal& v, int depth )
    {
        v.t = JVal::T::Obj;
        ++i_;   // '{'
        ws();
        if( i_ < s_.size() && s_[i_] == '}' ) { ++i_; return true; }
        for( ;; )
        {
            ws();
            std::string key;
            if( !str( key ) ) return false;
            ws();
            if( i_ >= s_.size() || s_[i_] != ':' ) return false;
            ++i_;
            ws();
            JVal item;
            if( !value( item, depth + 1 ) ) return false;
            v.keys.push_back( std::move( key ) );
            v.vals.push_back( std::move( item ) );
            ws();
            if( i_ >= s_.size() ) return false;
            if( s_[i_] == ',' ) { ++i_; continue; }
            if( s_[i_] == '}' ) { ++i_; return true; }
            return false;
        }
    }
};

// OTLP AnyValue -> text. int64 arrives as a JSON string ("123") or number; both give the digits.
inline std::string anyToString( const JVal& v )
{
    if( const JVal* x = v.get( "stringValue" ) ) return x->t == JVal::T::Str ? x->s : std::string();
    if( const JVal* x = v.get( "intValue" ) )    return ( x->t == JVal::T::Str || x->t == JVal::T::Num ) ? x->s : std::string();
    if( const JVal* x = v.get( "doubleValue" ) ) return ( x->t == JVal::T::Str || x->t == JVal::T::Num ) ? x->s : std::string();
    if( const JVal* x = v.get( "boolValue" ) )   return x->t == JVal::T::Bool ? ( x->b ? "true" : "false" ) : std::string();
    return std::string();
}

// Copies only allow-listed attributes from an OTLP "attributes" array into out.
inline void collectAttrs( const JVal* attrs, std::unordered_map<std::string, std::string>& out )
{
    if( !attrs || attrs->t != JVal::T::Arr ) return;
    for( const JVal& kv : attrs->a )
    {
        const JVal* k = kv.get( "key" );
        const JVal* v = kv.get( "value" );
        if( !k || k->t != JVal::T::Str || !v || !isAllowed( k->s ) ) continue;
        std::string text = anyToString( *v );
        if( text.size() > 256 ) text.resize( 256 );   // ids and numbers only; bounds anything unexpected
        out[k->s] = std::move( text );
    }
}

inline std::int64_t nanosToMs( const std::string& text )
{
    if( text.empty() ) return 0;
    std::uint64_t n = 0;
    for( const char c : text )
    {
        if( c < '0' || c > '9' ) return 0;
        n = n * 10 + static_cast<std::uint64_t>( c - '0' );
    }
    return static_cast<std::int64_t>( n / 1000000ULL );
}

} // namespace detail

// Parses an OTLP/JSON logs request. Returns false when the body is not valid JSON of the expected shape.
inline bool parseLogs( std::string_view body, std::vector<Record>& out, std::size_t maxRecords = 2000 )
{
    detail::JVal root;
    detail::Parser p( body );
    if( !p.parse( root ) || root.t != detail::JVal::T::Obj ) return false;
    const detail::JVal* resourceLogs = root.get( "resourceLogs" );
    if( !resourceLogs || resourceLogs->t != detail::JVal::T::Arr ) return true;   // valid, but nothing to read

    for( const detail::JVal& rl : resourceLogs->a )
    {
        std::unordered_map<std::string, std::string> base;
        if( const detail::JVal* res = rl.get( "resource" ) ) detail::collectAttrs( res->get( "attributes" ), base );
        const detail::JVal* scopes = rl.get( "scopeLogs" );
        if( !scopes || scopes->t != detail::JVal::T::Arr ) continue;
        for( const detail::JVal& sl : scopes->a )
        {
            const detail::JVal* recs = sl.get( "logRecords" );
            if( !recs || recs->t != detail::JVal::T::Arr ) continue;
            for( const detail::JVal& lr : recs->a )
            {
                if( out.size() >= maxRecords ) return true;
                Record r;
                r.attrs = base;
                detail::collectAttrs( lr.get( "attributes" ), r.attrs );
                std::string name = r.get( "event.name" );
                if( name.empty() )
                {
                    if( const detail::JVal* body2 = lr.get( "body" ) ) name = detail::anyToString( *body2 );
                }
                constexpr std::string_view kPrefix = "claude_code.";
                if( name.rfind( kPrefix, 0 ) == 0 ) name.erase( 0, kPrefix.size() );
                r.name = std::move( name );
                std::string ts;
                if( const detail::JVal* t = lr.get( "timeUnixNano" ) ) ts = t->s;
                if( ts.empty() ) if( const detail::JVal* t = lr.get( "observedTimeUnixNano" ) ) ts = t->s;
                r.tsMs = detail::nanosToMs( ts );
                out.push_back( std::move( r ) );
            }
        }
    }
    return true;
}

} // namespace rw::telemetry::otlp
