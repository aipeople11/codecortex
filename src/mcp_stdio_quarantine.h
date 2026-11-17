#pragma once

#include "infra/os.h"

#include <cstddef>
#include <cstdio>
#include <iostream>

namespace rw
{

// stdout is the MCP stdio transport. During a dispatch, any legacy CLI/helper write to stdout is a
// protocol violation because strict JSON-RPC clients will try to decode that byte stream as the next frame.
// This guard redirects stdout to a temporary sink while a handler runs, restores it before the caller emits
// the authoritative JSON-RPC response, and reports only the suppressed byte count on stderr.
//
// Failure to establish the guard is fail-open for compatibility: the dispatch still runs. The stdio MCP
// loops are single-request-at-a-time, so this process-wide fd redirect never overlaps a second dispatch.
class McpStdoutQuarantine
{
public:
    McpStdoutQuarantine() noexcept
    {
        std::fflush( stdout );
        std::cout.flush();
        capture_ = std::tmpfile();
        if( !capture_ ) return;

        saved_ = rw::os::dup( STDOUT_FILENO );
        if( saved_ < 0 )
        {
            std::fclose( capture_ );
            capture_ = nullptr;
            return;
        }

        if( rw::os::dup2( rw::os::fileno( capture_ ), STDOUT_FILENO ) < 0 )
        {
            rw::os::close( saved_ );
            saved_ = -1;
            std::fclose( capture_ );
            capture_ = nullptr;
            return;
        }
        active_ = true;
    }

    McpStdoutQuarantine( const McpStdoutQuarantine& ) = delete;
    McpStdoutQuarantine& operator=( const McpStdoutQuarantine& ) = delete;

    ~McpStdoutQuarantine() noexcept { finish(); }

    std::size_t finish() noexcept
    {
        if( finished_ ) return strayBytes_;
        finished_ = true;
        if( !active_ ) return 0;

        std::fflush( stdout );
        std::cout.flush();
        if( std::fseek( capture_, 0, SEEK_END ) == 0 )
        {
            const long n = std::ftell( capture_ );
            if( n > 0 ) strayBytes_ = static_cast<std::size_t>( n );
        }

        (void)rw::os::dup2( saved_, STDOUT_FILENO );
        rw::os::close( saved_ );
        saved_ = -1;
        std::fclose( capture_ );
        capture_ = nullptr;
        active_ = false;

        if( strayBytes_ != 0 )
        {
            std::fprintf( stderr,
                          "codecortex: suppressed %zu stray stdout byte(s) during MCP dispatch; stdout is reserved for JSON-RPC\n",
                          strayBytes_ );
            std::fflush( stderr );
        }
        return strayBytes_;
    }

private:
    FILE*       capture_    = nullptr;
    int         saved_      = -1;
    bool        active_     = false;
    bool        finished_   = false;
    std::size_t strayBytes_ = 0;
};

} // namespace rw
