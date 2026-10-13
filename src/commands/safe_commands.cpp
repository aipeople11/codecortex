// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Saurabh Verma
#include "safe_commands.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#if defined(_WIN32)
// Windows has no fork/exec/poll.h. The Windows execute() below runs on the project's os:: process layer
// (src/infra/os.h: spawn_sh = Git for Windows bash in a Job Object, pipe/poll/read/waitpid/kill), the same
// primitives verbs_change.h's runCommandCapture uses on every platform.
#include "infra/os.h"
#include <cerrno>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace rw::commands
{
namespace
{
bool validTarget( std::string_view target ) noexcept
{
    return !target.empty() && target.size() <= 1024 && target.find( '\0' ) == std::string_view::npos
        && target.find( '\n' ) == std::string_view::npos && target.find( '\r' ) == std::string_view::npos;
}

void appendTargetFlag( std::vector<std::string>& argv, std::string_view flag, std::string_view target )
{
    std::string arg( flag );
    arg.append( target );
    argv.push_back( std::move( arg ) );
}

#if !defined(_WIN32)
void drain( int fd, std::string& out, std::size_t cap )
{
    std::array<char, 8192> buf{};
    for( ;; )
    {
        const ssize_t n = ::read( fd, buf.data(), buf.size() );
        if( n > 0 )
        {
            const std::size_t room = cap > out.size() ? cap - out.size() : 0;
            out.append( buf.data(), std::min<std::size_t>( room, static_cast<std::size_t>( n ) ) );
            continue;
        }
        break;
    }
}
#endif
}

Plan buildSafePlan( std::string_view executable, std::string_view root,
                    std::string_view command, std::string_view target )
{
    Plan plan;
    if( executable.empty() ) { plan.error = "CodeCortex executable path is unavailable"; return plan; }
    plan.argv.emplace_back( executable );
    plan.argv.emplace_back( root.empty() ? "." : root );

    if( command == "analyze" ) plan.argv.emplace_back( "--report" );
    else if( command == "doctor" ) plan.argv.emplace_back( "--doctor" );
    else if( command == "tests" ) { plan.argv.emplace_back( "--test-gate" ); plan.argv.emplace_back( "--json" ); }
    else if( command == "tests_for_file" )
    {
        if( !validTarget( target ) ) { plan.error = "tests_for_file requires a repository-relative file"; return plan; }
        appendTargetFlag( plan.argv, "--test-gate=", target );
        plan.argv.emplace_back( "--json" );
    }
    else if( command == "quality" ) plan.argv.emplace_back( "--quality-panel" );
    else if( command == "communities" ) { plan.argv.emplace_back( "--communities" ); plan.argv.emplace_back( "--limit=24" ); }
    else if( command == "deps" ) { plan.argv.emplace_back( "--deps" ); plan.argv.emplace_back( "--limit=80" ); }
    else if( command == "community" )
    {
        if( !validTarget( target ) || !std::all_of( target.begin(), target.end(), []( unsigned char c ){ return std::isdigit( c ) != 0; } ) )
        { plan.error = "community requires a numeric module id"; return plan; }
        appendTargetFlag( plan.argv, "--community=", target );
        plan.argv.emplace_back( "--limit=80" );
    }
    else if( command == "path" )
    {
        if( !validTarget( target ) || target.find( ',' ) == std::string_view::npos ) { plan.error = "path requires SRC,DST"; return plan; }
        appendTargetFlag( plan.argv, "--path=", target );
    }
    else if( command == "edit_check" )
    {
        if( !validTarget( target ) ) { plan.error = "edit_check requires a symbol"; return plan; }
        appendTargetFlag( plan.argv, "--edit-check=", target );
        plan.argv.emplace_back( "--legend=compact" );
    }
    else if( command == "impact" || command == "callers" || command == "callees" || command == "around" )
    {
        if( !validTarget( target ) ) { plan.error = "command requires a non-empty target up to 1024 bytes"; return plan; }
        if( command == "impact" ) appendTargetFlag( plan.argv, "--impact=", target );
        else if( command == "callers" ) appendTargetFlag( plan.argv, "--callers=", target );
        else if( command == "callees" ) appendTargetFlag( plan.argv, "--callees=", target );
        else appendTargetFlag( plan.argv, "--around=", target );
        if( command != "around" ) plan.argv.emplace_back( "--json" );
    }
    else
    {
        plan.error = "unsupported safe command";
        return plan;
    }
    plan.ok = true;
    return plan;
}


Plan buildGitHistoryPlan( std::string_view root, std::string_view file, int limit )
{
    Plan plan;
    if( !validTarget( file ) ) { plan.error = "git history requires a repository-relative file"; return plan; }
    limit = std::clamp( limit, 1, 32 );
    plan.argv = {
        "git", "-C", std::string( root.empty() ? "." : root ), "--no-pager", "log",
        "-n", std::to_string( limit ), "--format=%H%x1f%h%x1f%an%x1f%at%x1f%s", "--", std::string( file )
    };
    plan.ok = true;
    return plan;
}

#if defined(_WIN32)
namespace
{
// POSIX-shell single-quote quoting: the Windows spawn layer runs `bash -c <command>`.
std::string shellQuote( std::string_view arg )
{
    std::string out = "'";
    for( const char c : arg ) { if( c == '\'' ) out += "'\\''"; else out.push_back( c ); }
    out.push_back( '\'' );
    return out;
}
}

Execution execute( const Plan& plan, int timeoutMs, std::size_t outputCap )
{
    Execution result;
    if( !plan.ok || plan.argv.empty() ) { result.error = plan.error.empty() ? "invalid command plan" : plan.error; return result; }
    std::string cmd;
    for( const auto& a : plan.argv ) { if( !cmd.empty() ) cmd.push_back( ' ' ); cmd += shellQuote( a ); }

    int fds[2];
    if( rw::os::pipe( fds ) != 0 ) { result.error = "pipe() failed"; return result; }
    const rw::os::pid_t pid = rw::os::spawn_sh( cmd, fds );
    if( pid < 0 )
    {
        rw::os::close( fds[0] ); rw::os::close( fds[1] );
        result.error = "could not start the command (Git for Windows bash is required)";
        return result;
    }
    rw::os::close( fds[1] );

    // stdout and stderr arrive interleaved on one pipe on Windows; both land in stdoutText.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( std::max( 1, timeoutMs ) );
    char buf[8192];
    for( ;; )
    {
        if( std::chrono::steady_clock::now() >= deadline )
        {
            result.timedOut = true;
            rw::os::kill( -pid, SIGKILL );
            rw::os::kill( pid, SIGKILL );
            break;
        }
        rw::os::pollfd pfd{ fds[0], POLLIN, 0 };
        const int ready = rw::os::poll( &pfd, 1, 25 );
        if( ready > 0 )
        {
            const rw::os::ssize_t n = rw::os::read( fds[0], buf, sizeof buf );
            if( n <= 0 ) break;   // EOF: the command closed its output
            if( result.stdoutText.size() < outputCap )
                result.stdoutText.append( buf, std::min<std::size_t>( std::size_t( n ), outputCap - result.stdoutText.size() ) );
        }
        else if( ready < 0 && errno != EINTR ) break;
    }
    rw::os::close( fds[0] );

    int status = 0;
    for( ;; )
    {
        const rw::os::pid_t done = rw::os::waitpid( pid, &status, result.timedOut ? 0 : WNOHANG );
        if( done == pid || done < 0 ) break;
        if( std::chrono::steady_clock::now() >= deadline )
        {
            result.timedOut = true;
            rw::os::kill( -pid, SIGKILL );
            rw::os::kill( pid, SIGKILL );
            continue;
        }
        rw::os::poll( nullptr, 0, 20 );
    }
    if( WIFEXITED( status ) ) result.exitCode = WEXITSTATUS( status );
    else if( WIFSIGNALED( status ) ) result.exitCode = 128 + WTERMSIG( status );
    result.ok = !result.timedOut && result.exitCode == 0;
    if( result.timedOut ) result.error = "safe command timed out";
    return result;
}
#else
Execution execute( const Plan& plan, int timeoutMs, std::size_t outputCap )
{
    Execution result;
    if( !plan.ok || plan.argv.empty() ) { result.error = plan.error.empty() ? "invalid command plan" : plan.error; return result; }

    int outPipe[2]{ -1, -1 }, errPipe[2]{ -1, -1 };
    if( ::pipe( outPipe ) != 0 || ::pipe( errPipe ) != 0 )
    {
        result.error = "pipe() failed";
        if( outPipe[0] >= 0 ) { ::close( outPipe[0] ); ::close( outPipe[1] ); }
        if( errPipe[0] >= 0 ) { ::close( errPipe[0] ); ::close( errPipe[1] ); }
        return result;
    }

    const pid_t pid = ::fork();
    if( pid < 0 )
    {
        result.error = "fork() failed";
        ::close( outPipe[0] ); ::close( outPipe[1] ); ::close( errPipe[0] ); ::close( errPipe[1] );
        return result;
    }
    if( pid == 0 )
    {
        ::dup2( outPipe[1], STDOUT_FILENO );
        ::dup2( errPipe[1], STDERR_FILENO );
        ::close( outPipe[0] ); ::close( outPipe[1] ); ::close( errPipe[0] ); ::close( errPipe[1] );
        std::vector<char*> argv;
        argv.reserve( plan.argv.size() + 1 );
        for( const auto& arg : plan.argv ) argv.push_back( const_cast<char*>( arg.c_str() ) );
        argv.push_back( nullptr );
        if( std::strchr( argv[0], '/' ) ) ::execv( argv[0], argv.data() );
        else ::execvp( argv[0], argv.data() );
        _exit( 127 );
    }

    ::close( outPipe[1] ); ::close( errPipe[1] );
    ::fcntl( outPipe[0], F_SETFL, ::fcntl( outPipe[0], F_GETFL ) | O_NONBLOCK );
    ::fcntl( errPipe[0], F_SETFL, ::fcntl( errPipe[0], F_GETFL ) | O_NONBLOCK );
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( std::max( 1, timeoutMs ) );
    int status = 0;
    for( ;; )
    {
        drain( outPipe[0], result.stdoutText, outputCap );
        drain( errPipe[0], result.stderrText, outputCap );
        const pid_t done = ::waitpid( pid, &status, WNOHANG );
        if( done == pid ) break;
        if( std::chrono::steady_clock::now() >= deadline )
        {
            result.timedOut = true;
            ::kill( pid, SIGKILL );
            ::waitpid( pid, &status, 0 );
            break;
        }
        pollfd fds[2] = { { outPipe[0], POLLIN, 0 }, { errPipe[0], POLLIN, 0 } };
        ::poll( fds, 2, 25 );
    }
    drain( outPipe[0], result.stdoutText, outputCap );
    drain( errPipe[0], result.stderrText, outputCap );
    ::close( outPipe[0] ); ::close( errPipe[0] );

    if( WIFEXITED( status ) ) result.exitCode = WEXITSTATUS( status );
    else if( WIFSIGNALED( status ) ) result.exitCode = 128 + WTERMSIG( status );
    result.ok = !result.timedOut && result.exitCode == 0;
    if( result.timedOut ) result.error = "safe command timed out";
    return result;
}
#endif

} // namespace rw::commands
