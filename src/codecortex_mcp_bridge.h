// SPDX-License-Identifier: MIT
// CodeCortex MCP integration layer over the full upstream CodeCortex MCP catalog.
#pragma once

#include "mcp.h"
#include "memory/memory_service_client.h"
#include "project_identity.h"
#include "telemetry/collector.h"
#include "telemetry/session_meter.h"
#include "ui/server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace rw
{
namespace codecortex_mcp
{
// Multi-project workspaces: started with 2+ folders, the engine's default root is a WORKSPACE KEY — the
// folders' real paths, each followed by '\x1f' (mcpindex.h mcpWorkspaceKey). Everything in this bridge that
// needs ONE real directory (project identity, telemetry file, memory scope) uses the first folder; everything
// that should cover the whole workspace (savings measurement, codecortex_status) uses every folder.
inline std::vector<std::string> workspaceFolders( std::string_view root )
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while( start < root.size() )
    {
        const auto sep = root.find( '\x1f', start );
        const auto end = sep == std::string_view::npos ? root.size() : sep;
        if( end > start ) out.emplace_back( root.substr( start, end - start ) );
        if( sep == std::string_view::npos ) break;
        start = sep + 1;
    }
    return out;
}

// The folder the dashboard was started on (the FIRST folder the user listed — main.cpp's rootPath). A workspace
// key orders folders by label instead, so without this the bridge would file events under a different project
// than the dashboard shows, and they would be hidden behind the dashboard's project filter.
inline std::string& workspacePrimaryFolder()
{
    static std::string folder;
    return folder;
}

inline std::string primaryRoot( std::string_view root )
{
    const auto sep = root.find( '\x1f' );
    if( sep == std::string_view::npos ) return std::string( root );
    if( !workspacePrimaryFolder().empty() ) return workspacePrimaryFolder();
    return std::string( root.substr( 0, sep ) );
}

inline std::string displayRoots( std::string_view root )
{
    const auto folders = workspaceFolders( root );
    std::string out;
    for( const auto& f : folders ) { if( !out.empty() ) out += " + "; out += f; }
    return out.empty() ? std::string( root ) : out;
}

inline std::string telemetryPathFor( std::string_view rootOrKey )
{
    const std::string root = primaryRoot( rootOrKey );
    if( const char* p = std::getenv( "CODECORTEX_TELEMETRY_FILE" ); p && *p ) return p;
    const std::filesystem::path base( root.empty() ? "." : std::string( root ) );
    return ( base / ".codecortex" / "runs.jsonl" ).string();
}

inline std::string dashboardUrl()
{
    if( const char* p = std::getenv( "CODECORTEX_DASHBOARD_URL" ); p && *p ) return p;
    // The port this process's dashboard actually bound (it moves past 7331 when an older CodeCortex still holds it).
    const int port = rw::ui::activePort().load();
    return "http://127.0.0.1:" + std::to_string( port > 0 ? port : 7331 );
}

inline std::size_t matchingClose( const std::string& s, std::size_t open, char l, char r )
{
    if( open >= s.size() || s[open] != l ) return std::string::npos;
    int depth = 0; bool inString = false; bool esc = false;
    for( std::size_t i = open; i < s.size(); ++i )
    {
        const char c = s[i];
        if( inString )
        {
            if( esc ) { esc = false; continue; }
            if( c == '\\' ) { esc = true; continue; }
            if( c == '"' ) inString = false;
            continue;
        }
        if( c == '"' ) { inString = true; continue; }
        if( c == l ) ++depth;
        else if( c == r && --depth == 0 ) return i;
    }
    return std::string::npos;
}

inline bool appendArrayItem( std::string& json, std::string_view key, std::string_view item )
{
    const std::string needle = "\"" + std::string( key ) + "\":[";
    const auto p = json.find( needle );
    if( p == std::string::npos ) return false;
    const auto open = p + needle.size() - 1;
    const auto close = matchingClose( json, open, '[', ']' );
    if( close == std::string::npos ) return false;
    const bool empty = close == open + 1;
    json.insert( close, std::string( empty ? "" : "," ) + std::string( item ) );
    return true;
}

inline std::string replaceToolName( std::string line, std::string_view from, std::string_view to )
{
    const std::string params = mcpdetail::findObject( line, "params" );
    if( params.empty() ) return line;
    const std::string quoted = "\"" + std::string( from ) + "\"";
    const std::string key = "\"name\"";
    const auto paramsPos = line.find( params );
    if( paramsPos == std::string::npos ) return line;
    auto keyPos = line.find( key, paramsPos );
    if( keyPos == std::string::npos ) return line;
    auto valuePos = line.find( quoted, keyPos + key.size() );
    if( valuePos == std::string::npos ) return line;
    line.replace( valuePos, quoted.size(), "\"" + std::string( to ) + "\"" );
    return line;
}

inline std::string toolResult( std::string_view id, std::string_view text, bool isError = false )
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::string( id )
         + ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\""
         + mcpdetail::jsonEscape( std::string( text ) ) + "\"}],\"isError\":" + ( isError ? "true" : "false" ) + "}}";
}

struct BridgeContext
{
    std::string connectionId;
    std::string hostName;
    std::string hostVersion;
    std::string activeRunId;
    std::string activeTask;
    std::string activeRoot;
    bool runStarted = false;
    // The tools/call being dispatched right now (set by the dispatch wrapper, read by recordTool) so the
    // ToolCall event carries the JSON-RPC id and the retry-chain fields without threading them through
    // every branch of the core dispatcher.
    std::string pendingCallId;
    std::int64_t pendingAttempt = 0;
    std::string pendingRetryOf;
};

inline BridgeContext& bridgeContext()
{
    static BridgeContext ctx;
    if( ctx.connectionId.empty() )
    {
        if( const char* p = std::getenv( "CODECORTEX_CONNECTION_ID" ); p && *p ) ctx.connectionId = p;
        else ctx.connectionId = rw::telemetry::makeIntegrationRunId( "conn" );
        if( const char* p = std::getenv( "CODECORTEX_HOST_NAME" ); p && *p ) ctx.hostName = p;
        if( const char* p = std::getenv( "CODECORTEX_HOST_VERSION" ); p && *p ) ctx.hostVersion = p;
    }
    return ctx;
}

inline void captureClientInfo( const std::string& line )
{
    auto& ctx = bridgeContext();
    const std::string params = mcpdetail::findObject( line, "params" );
    const std::string client = mcpdetail::findObject( params, "clientInfo" );
    if( !client.empty() )
    {
        const std::string name = mcpdetail::findString( client, "name" );
        const std::string version = mcpdetail::findString( client, "version" );
        if( !name.empty() ) ctx.hostName = name;
        if( !version.empty() ) ctx.hostVersion = version;
    }
}

inline void decorateEvent( rw::telemetry::Event& e, std::string_view root, std::string_view runId,
                           std::string_view source = "observed" )
{
    const auto id = rw::project::identify( primaryRoot( root ) );
    const auto& ctx = bridgeContext();
    e.runId.assign( runId );
    e.projectId = id.projectId;
    e.projectName = id.projectName;
    e.repoId = id.repoId;
    e.sessionId = !id.sessionId.empty() ? id.sessionId : ctx.connectionId;
    e.agentId = !id.agentId.empty() ? id.agentId : ctx.hostName;
    e.hostName = ctx.hostName;
    e.hostVersion = ctx.hostVersion;
    e.connectionId = ctx.connectionId;
    e.source.assign( source );
    e.workspaceRoot = id.root;
    if( e.timestampMs == 0 ) e.timestampMs = rw::telemetry::unixTimeMs();
}

inline void recordEvent( std::string_view telemetryPath, std::string_view root, std::string_view runId,
                         rw::telemetry::EventType type, std::string_view target = {},
                         std::string_view detail = {}, bool success = true,
                         std::string_view source = "observed" )
{
    if( telemetryPath.empty() ) return;
    rw::telemetry::Collector c( telemetryPath );
    if( !c.enabled() ) return;
    rw::telemetry::Event e;
    e.type = type;
    decorateEvent( e, root, runId, source );
    e.target.assign( target );
    e.detail.assign( detail );
    e.success = success;
    c.record( e );
}

inline void finishActiveRun( std::string_view telemetryPath, bool success, std::string_view reason = {} )
{
    auto& ctx = bridgeContext();
    if( ctx.activeRunId.empty() || !ctx.runStarted ) return;
    recordEvent( telemetryPath, ctx.activeRoot, ctx.activeRunId, rw::telemetry::EventType::RunFinished,
                 ctx.activeTask, reason, success );
    ctx.runStarted = false;
    ctx.activeTask.clear();
    ctx.activeRoot.clear();
    const char* explicitRun = std::getenv( "CODECORTEX_RUN_ID" );
    if( !( explicitRun && *explicitRun ) ) ctx.activeRunId.clear();
}

inline std::string ensureActiveRun( std::string_view telemetryPath, std::string_view root,
                                    std::string_view taskHint = {} )
{
    auto& ctx = bridgeContext();
    const std::string task( taskHint );
    const bool explicitRun = []{
        const char* p = std::getenv( "CODECORTEX_RUN_ID" );
        return p && *p;
    }();
    if( !task.empty() && !ctx.activeTask.empty() && task != ctx.activeTask && !explicitRun )
        finishActiveRun( telemetryPath, true, "superseded_by_new_task" );

    if( ctx.activeRunId.empty() )
    {
        if( const char* p = std::getenv( "CODECORTEX_RUN_ID" ); p && *p ) ctx.activeRunId = p;
        else ctx.activeRunId = rw::telemetry::makeIntegrationRunId( "run" );
    }
    if( !task.empty() ) ctx.activeTask = task;
    if( ctx.activeRoot.empty() ) ctx.activeRoot.assign( root );
    if( !ctx.runStarted )
    {
        recordEvent( telemetryPath, root, ctx.activeRunId, rw::telemetry::EventType::RunStarted,
                     displayRoots( root ), task.empty() ? "implicit_mcp_task" : task, true );
        ctx.runStarted = true;
    }
    return ctx.activeRunId;
}

inline void recordTool( std::string_view telemetryPath, std::string_view root, std::string_view runId,
                        std::string_view tool, std::int64_t durationMs, bool success,
                        std::string_view detail = {} )
{
    rw::telemetry::Collector c( telemetryPath );
    if( !c.enabled() ) return;
    rw::telemetry::Event e;
    e.type = rw::telemetry::EventType::ToolCall;
    decorateEvent( e, root, runId );
    e.target = std::string( tool );
    e.detail = std::string( detail );
    e.durationMs = durationMs;
    e.success = success;
    const auto& ctx = bridgeContext();
    e.toolCallId = ctx.pendingCallId;
    e.attemptNumber = ctx.pendingAttempt;
    e.retryOfToolCallId = ctx.pendingRetryOf;
    c.record( e );
}

inline void recordSemanticEvent( std::string_view telemetryPath, std::string_view root,
                                 std::string_view runId, std::string_view tool,
                                 const std::string& args, bool success )
{
    if( !success ) return;
    const std::string t( tool );
    if( t == "code_search" || t == "code_graph" || t == "explore" || t == "grep" || t == "find_symbol" ||
        t == "find_referencing_symbols" || t == "trace_explain" || t == "from_trace" ||
        t == "change_impact" || t == "impact" || t == "affected" || t == "uses" ||
        t == "path_between" || t == "connect" )
    {
        std::string target = mcpStringArg( args, "task" ).value;
        if( target.empty() ) target = mcpStringArg( args, "symbol" ).value;
        if( target.empty() ) target = mcpStringArg( args, "query" ).value;
        if( target.empty() ) target = t;
        recordEvent( telemetryPath, root, runId, rw::telemetry::EventType::Search, target,
                     "mcp_tool=" + t, true );
    }
    else if( t == "fetch_body" )
    {
        std::string target = mcpStringArg( args, "symbol" ).value;
        if( target.empty() ) target = "symbol body";
        recordEvent( telemetryPath, root, runId, rw::telemetry::EventType::FileRead, target,
                     "observed_body_retrieval", true );
    }
    else if( t == "replace_symbol_body" || t == "insert_before_symbol" || t == "insert_after_symbol" )
    {
        std::string target = mcpStringArg( args, "symbol" ).value;
        if( target.empty() ) target = t;
        recordEvent( telemetryPath, root, runId, rw::telemetry::EventType::Edit, target,
                     "mcp_mutation=" + t, true );
    }
    else if( t == "tests_required" || t == "situational_awareness" )
    {
        recordEvent( telemetryPath, root, runId, rw::telemetry::EventType::TestSelection,
                     "required tests", "selection_only_not_execution", true );
    }
}

// One-paragraph token summary for codecortex_status. Every number is an ESTIMATE of CodeCortex's own
// answers (the agent's full conversation usage is only known to the host; see the dashboard).
inline std::string sessionTokenSummary()
{
    const auto& m = rw::telemetry::meter::sessionMeter();
    std::string out = "Tokens (this session, CodeCortex answers only, estimated): "
        + std::to_string( m.answersTokens ) + " sent to the agent";
    if( m.avoidedTokens > 0 )
        out += "; the files those answers cover would have cost ~" + std::to_string( m.avoidedTokens ) + " to read whole";
    if( m.repeatsSuppressed > 0 )
        out += "; " + std::to_string( m.repeatsSuppressed ) + " repeated answer(s) not resent (~"
             + std::to_string( m.repeatTokensSaved ) + " tokens saved)";
    return out;
}

// Default answer budget for the orientation tools when the agent sets none. The upstream default (6000 for
// explore) is sized for one big orientation; most agent calls need far less, and every answer stays fully
// truncation-disclosed, so the agent can ask again with a larger budget_tokens. 0 = keep upstream defaults.
inline long long defaultBudgetTokens()
{
    if( const char* p = std::getenv( "CODECORTEX_DEFAULT_BUDGET_TOKENS" ); p && *p )
    {
        char* end = nullptr; const long long v = std::strtoll( p, &end, 10 );
        if( end != p && v >= 0 ) return v;
    }
    return 3000;
}

inline std::string withDefaultBudget( const std::string& line, std::string_view tool, const std::string& argsObj )
{
    if( tool != "code_search" && tool != "trace_explain" && tool != "explore" && tool != "pack_task" && tool != "from_trace" )
        return line;
    const long long budget = defaultBudgetTokens();
    if( budget <= 0 || argsObj.empty() || argsObj.front() != '{' ) return line;
    if( argsObj.find( "\"budget_tokens\"" ) != std::string::npos ) return line;   // the agent chose one
    const auto at = line.find( argsObj );
    if( at == std::string::npos ) return line;
    const bool empty = argsObj.find_first_not_of( " \t\r\n", 1 ) == argsObj.size() - 1;
    std::string out = line;
    out.insert( at + 1, "\"budget_tokens\":" + std::to_string( budget ) + ( empty ? "" : "," ) );
    return out;
}

inline std::string unquoteId( std::string_view raw )
{
    if( raw.size() >= 2 && raw.front() == '"' && raw.back() == '"' ) return std::string( raw.substr( 1, raw.size() - 2 ) );
    return std::string( raw );
}

inline McpDispatchResult dispatchCodeCortexMcpLineCore( const std::string& line, int topK, bool stable, bool noRedact,
                                                         const McpDispatchPolicy& policy,
                                                         std::string_view telemetryPath = {} )
{
    const std::string method = mcpdetail::findString( line, "method" );
    if( method == "initialize" )
    {
        captureClientInfo( line );
        McpDispatchResult r = dispatchMcpLine( line, topK, stable, noRedact, policy );
        if( r.isNotification || r.resp.empty() ) return r;
        const std::string marker = "\"instructions\":\"";
        const auto pos = r.resp.find( marker );
        if( pos != std::string::npos )
        {
            const std::string intro =
                "CodeCortex is connected. Keep using your coding agent normally. "
                "On the first CodeCortex use in this session, briefly tell the user that CodeCortex is active and offer the live dashboard at " + dashboardUrl() +
                "; do not repeat that onboarding message unless the user asks. "
                "Use codecortex_status any time to inspect the active project, repository, session, memory health, tool count and dashboard endpoint. ";
            r.resp.insert( pos + marker.size(), mcpdetail::jsonEscape( intro ) );
        }
        return r;
    }
    if( method == "tools/list" )
    {
        McpDispatchResult r = dispatchMcpLine( line, topK, stable, noRedact, policy );
        if( r.isNotification || r.resp.empty() ) return r;
        static constexpr std::string_view extras[] = {
            "{\"name\":\"codecortex_status\",\"description\":\"Show current CodeCortex connection, project/repository/session identity, dashboard URL, memory health, and MCP tool availability. Use when the user asks whether CodeCortex is active or wants to open/inspect the dashboard.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}",
            "{\"name\":\"code_search\",\"description\":\"CodeCortex fresh-eyes orientation. Use first for unfamiliar code or broad coding tasks. Uses the full CodeCortex explore engine and automatically appends bounded project-scoped CodeCortex Memory recall when available.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"task\":{\"type\":\"string\"},\"budget_tokens\":{\"type\":\"integer\"}},\"required\":[\"task\"]}}",
            "{\"name\":\"code_graph\",\"description\":\"CodeCortex symbol graph shortcut over CodeCortex find_symbol. Use for direct callers/callees and structure around a symbol.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"symbol\":{\"type\":\"string\"}},\"required\":[\"symbol\"]}}",
            "{\"name\":\"trace_explain\",\"description\":\"CodeCortex debugging shortcut over CodeCortex from_trace. Use first for stack traces, sanitizer reports, compiler errors, and failed-test traces.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"trace\":{\"type\":\"string\"},\"budget_tokens\":{\"type\":\"integer\"}},\"required\":[\"trace\"]}}",
            "{\"name\":\"change_impact\",\"description\":\"CodeCortex pre-edit shortcut over CodeCortex impact. Use before changing important symbols to inspect blast radius.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"symbol\":{\"type\":\"string\"}},\"required\":[\"symbol\"]}}",
            "{\"name\":\"tests_required\",\"description\":\"CodeCortex test-selection shortcut over CodeCortex situational_awareness. Use after edits or before completion.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"files\":{\"type\":\"string\"},\"diff\":{\"type\":\"string\"}}}}",
            "{\"name\":\"codecortex_memory_recall\",\"description\":\"Recall bounded durable coding memory from CodeCortex Memory using stable project/repository identity.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"task\":{\"type\":\"string\"},\"top_k\":{\"type\":\"integer\"}},\"required\":[\"task\"]}}",
            "{\"name\":\"memory_checkpoint\",\"description\":\"Persist a durable project-scoped coding fact/bug/workflow/architecture lesson to CodeCortex Memory.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"checkpoint_name\":{\"type\":\"string\"},\"description\":{\"type\":\"string\"},\"type\":{\"type\":\"string\",\"enum\":[\"pattern\",\"preference\",\"architecture\",\"bug\",\"workflow\",\"fact\"]}},\"required\":[\"checkpoint_name\"]}}",
            "{\"name\":\"prove_change\",\"description\":\"CodeCortex completion gate: combine CodeCortex quality_delta with situational_awareness/test evidence before claiming a coding task complete.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}"
        };
        for( const auto e : extras ) appendArrayItem( r.resp, "tools", e );
        return r;
    }

    if( method != "tools/call" ) return dispatchMcpLine( line, topK, stable, noRedact, policy );

    const std::string params = mcpdetail::findObject( line, "params" );
    const std::string argsObj = mcpdetail::findObject( params, "arguments" );
    const std::string args = argsObj.empty() ? params : argsObj;
    const std::string name = mcpStringArg( params, "name" ).value;
    const std::string pathArg = mcpStringArg( args, "path" ).value;
    const std::string effectiveRoot = !pathArg.empty() ? pathArg
        : !policy.pinnedRoot.empty() ? policy.pinnedRoot
        : !policy.defaultRoot.empty() ? policy.defaultRoot : policy.assumedRoot;
    const auto rawId = mcpdetail::findRawId( line );
    const std::string id = rawId.token;
    const auto started = std::chrono::steady_clock::now();
    const std::string taskHint = mcpStringArg( args, "task" ).value;
    const std::string runId = name == "codecortex_status" ? bridgeContext().activeRunId
                                                            : ensureActiveRun( telemetryPath, effectiveRoot, taskHint );

    auto finishTelemetry = [&]( bool ok, std::string_view detail = {} )
    {
        if( telemetryPath.empty() || runId.empty() ) return;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now() - started ).count();
        recordTool( telemetryPath, effectiveRoot, runId, name, ms, ok, detail );
        recordSemanticEvent( telemetryPath, effectiveRoot, runId, name, args, ok );
    };

    const auto alias = [&]( std::string_view upstream )
    {
        McpDispatchResult r = dispatchMcpLine( replaceToolName( line, name, upstream ), topK, stable, noRedact, policy );
        r.timingVerb = name;
        finishTelemetry( r.resp.find( "\"error\"" ) == std::string::npos, std::string( "upstream=" ) + std::string( upstream ) );
        return r;
    };

    if( name == "codecortex_status" )
    {
        const auto ident = rw::project::identify( primaryRoot( effectiveRoot ) );
        rw::memory::MemoryServiceClient mem;
        const auto mh = mem.health();
        const std::string body =
            "CodeCortex is active\n"
            "Project: " + ( ident.projectName.empty() ? ident.projectId : ident.projectName ) + "\n"
            "Repository: " + ( ident.repoId.empty() ? ident.root : ident.repoId ) + "\n"
            "Session: " + ( ident.sessionId.empty() ? bridgeContext().connectionId : ident.sessionId ) + "\n"
            "Host: " + ( bridgeContext().hostName.empty() ? std::string( "MCP client" ) : bridgeContext().hostName ) + "\n"
            "Agent: " + ( ident.agentId.empty() ? ( bridgeContext().hostName.empty() ? std::string( "not reported by host" ) : bridgeContext().hostName ) : ident.agentId ) + "\n"
            "Run: " + ( bridgeContext().activeRunId.empty() ? std::string( "no active coding run" ) : bridgeContext().activeRunId ) + "\n"
            "Dashboard: " + dashboardUrl() + "\n"
            + ( workspaceFolders( effectiveRoot ).size() > 1
                    ? "Projects (one combined workspace): " + displayRoots( effectiveRoot ) + "\n" : std::string() ) +
            "MCP tools: 42 available\n"
            "Memory: " + std::string( mh.ok ? "connected" : "unavailable" ) + "\n"
            + sessionTokenSummary();
        finishTelemetry( true, "status" );
        return { toolResult( id, body, false ), false, name };
    }

    if( name == "code_graph" ) return alias( "find_symbol" );
    if( name == "trace_explain" ) return alias( "from_trace" );
    if( name == "change_impact" ) return alias( "impact" );
    if( name == "tests_required" ) return alias( "situational_awareness" );

    if( name == "code_search" )
    {
        McpDispatchResult r = dispatchMcpLine( replaceToolName( line, name, "explore" ), topK, stable, noRedact, policy );
        r.timingVerb = name;
        const std::string task = mcpStringArg( args, "task" ).value;
        if( !task.empty() && r.resp.find( "\"error\"" ) == std::string::npos )
        {
            const auto ident = rw::project::identify( primaryRoot( effectiveRoot ) );
            rw::memory::MemoryServiceClient mem;
            rw::memory::RecallQuery q;
            q.query = task; q.project = ident.projectId; q.agentId = ident.agentId.empty() ? bridgeContext().hostName : ident.agentId; q.sessionId = ident.sessionId.empty() ? bridgeContext().connectionId : ident.sessionId; q.includeLessons = true; q.limit = 5;
            const auto mr = mem.smartRecall( q );
            if( mr.ok && !mr.body.empty() )
            {
                const std::string item = "{\"type\":\"text\",\"text\":\""
                    + mcpdetail::jsonEscape( "--- CODECORTEX PRIOR MEMORY ---\nproject=" + ident.projectId + "\n" + mr.body ) + "\"}";
                appendArrayItem( r.resp, "content", item );
                if( !telemetryPath.empty() )
                {
                    rw::telemetry::Collector c( telemetryPath );
                    rw::telemetry::Event e; e.type = rw::telemetry::EventType::MemoryRecall;
                    e.runId = runId; e.projectId = ident.projectId;
                    e.projectName = ident.projectName; e.repoId = ident.repoId; e.sessionId = ident.sessionId;
                    e.agentId = ident.agentId.empty() ? bridgeContext().hostName : ident.agentId; e.hostName = bridgeContext().hostName; e.hostVersion = bridgeContext().hostVersion; e.connectionId = bridgeContext().connectionId; e.source = "observed"; e.workspaceRoot = ident.root; e.target = task;
                    e.detail = "memory_service_recalled"; e.timestampMs = rw::telemetry::unixTimeMs(); e.success = true; c.record( e );
                }
            }
        }
        finishTelemetry( r.resp.find( "\"error\"" ) == std::string::npos, "upstream=explore memory=auto" );
        return r;
    }

    if( name == "codecortex_memory_recall" )
    {
        const std::string task = mcpStringArg( args, "task" ).value;
        if( task.empty() ) return { toolResult( id, "missing required field: task", true ), false, name };
        const auto ident = rw::project::identify( primaryRoot( effectiveRoot ) );
        rw::memory::RecallQuery q; q.query = task; q.project = ident.projectId; q.agentId = ident.agentId.empty() ? bridgeContext().hostName : ident.agentId; q.sessionId = ident.sessionId.empty() ? bridgeContext().connectionId : ident.sessionId; q.includeLessons = true; q.limit = 5;
        rw::memory::MemoryServiceClient mem; const auto mr = mem.smartRecall( q );
        if( mr.ok ) recordEvent( telemetryPath, effectiveRoot, runId, rw::telemetry::EventType::MemoryRecall,
                                task, "memory_service_recalled", true );
        finishTelemetry( mr.ok, "memory_service_recall" );
        return { toolResult( id, mr.ok ? mr.body : mr.error, !mr.ok ), false, name };
    }

    if( name == "memory_checkpoint" )
    {
        const std::string checkpoint = mcpStringArg( args, "checkpoint_name" ).value;
        const std::string description = mcpStringArg( args, "description" ).value;
        std::string type = mcpStringArg( args, "type" ).value; if( type.empty() ) type = "fact";
        if( checkpoint.empty() ) return { toolResult( id, "missing required field: checkpoint_name", true ), false, name };
        static constexpr std::string_view allowed[] = { "pattern", "preference", "architecture", "bug", "workflow", "fact" };
        bool okType = false; for( auto t : allowed ) if( type == t ) okType = true;
        if( !okType ) return { toolResult( id, "invalid memory type", true ), false, name };
        const auto ident = rw::project::identify( primaryRoot( effectiveRoot ) );
        rw::memory::RememberRequest req; req.project = ident.projectId; req.agentId = ident.agentId; req.type = type;
        req.content = checkpoint + ( description.empty() ? std::string() : ": " + description );
        rw::memory::MemoryServiceClient mem; const auto mr = mem.remember( req );
        if( mr.ok ) recordEvent( telemetryPath, effectiveRoot, runId, rw::telemetry::EventType::Checkpoint,
                                checkpoint, "memory_type=" + type, true );
        finishTelemetry( mr.ok, "memory_service_saved" );
        return { toolResult( id, mr.ok ? mr.body : mr.error, !mr.ok ), false, name };
    }

    if( name == "prove_change" )
    {
        McpDispatchResult q = dispatchMcpLine( replaceToolName( line, name, "quality_delta" ), topK, stable, noRedact, policy );
        McpDispatchResult s = dispatchMcpLine( replaceToolName( line, name, "situational_awareness" ), topK, stable, noRedact, policy );
        const std::string qt = mcpdetail::findString( q.resp, "text" );
        const std::string st = mcpdetail::findString( s.resp, "text" );
        const bool ok = q.resp.find( "\"error\"" ) == std::string::npos && s.resp.find( "\"error\"" ) == std::string::npos;
        recordEvent( telemetryPath, effectiveRoot, runId, rw::telemetry::EventType::ProofCheck,
                     "change proof", "analysis_gate_only; test execution must be separately observed", ok );
        finishTelemetry( ok, "quality_delta+situational_awareness" );
        const McpDispatchResult out{ toolResult( id, "QUALITY DELTA\n" + qt + "\n\nSITUATIONAL AWARENESS / TESTS\n" + st, !ok ), false, name };
        finishActiveRun( telemetryPath, ok, ok ? "proof_check_completed" : "proof_check_failed" );
        return out;
    }

    McpDispatchResult r = dispatchMcpLine( line, topK, stable, noRedact, policy );
    finishTelemetry( r.resp.find( "\"error\"" ) == std::string::npos, "codecortex_full_catalog" );
    return r;
}

// The tools/call entry point: wraps the core dispatcher with the session meter (retry chain, repeat guard,
// default answer budget, context/savings measurement). Everything else passes straight through.
inline McpDispatchResult dispatchCodeCortexMcpLine( const std::string& line, int topK, bool stable, bool noRedact,
                                                     const McpDispatchPolicy& policy,
                                                     std::string_view telemetryPath = {} )
{
    if( mcpdetail::findString( line, "method" ) != "tools/call" )
        return dispatchCodeCortexMcpLineCore( line, topK, stable, noRedact, policy, telemetryPath );

    namespace mt = rw::telemetry::meter;
    auto& meter = mt::sessionMeter();
    auto& ctx = bridgeContext();
    const std::string params = mcpdetail::findObject( line, "params" );
    const std::string argsObj = mcpdetail::findObject( params, "arguments" );
    const std::string name = mcpStringArg( params, "name" ).value;
    const std::string rawId = mcpdetail::findRawId( line ).token;
    const std::string callId = unquoteId( rawId );

    const mt::RetryInfo retry = meter.beginCall( name );
    ctx.pendingCallId = callId; ctx.pendingAttempt = retry.attempt; ctx.pendingRetryOf = retry.retryOf;
    McpDispatchResult r = dispatchCodeCortexMcpLineCore( withDefaultBudget( line, name, argsObj ),
                                                         topK, stable, noRedact, policy, telemetryPath );
    ctx.pendingCallId.clear(); ctx.pendingAttempt = 0; ctx.pendingRetryOf.clear();
    if( r.isNotification || r.resp.empty() ) return r;

    const bool ok = r.resp.find( "\"error\"" ) == std::string::npos && r.resp.find( "\"isError\":true" ) == std::string::npos;
    meter.endCall( name, callId, ok, retry );

    // What this answer costs the agent, and what reading the files it covers would have cost instead.
    std::int64_t answerTokens = mt::estimateAnswerTokens( r.resp );
    const std::string pathArg = mcpStringArg( argsObj.empty() ? params : argsObj, "path" ).value;
    const std::string root = !pathArg.empty() ? pathArg : !ctx.activeRoot.empty() ? ctx.activeRoot
                           : !policy.pinnedRoot.empty() ? policy.pinnedRoot
                           : !policy.defaultRoot.empty() ? policy.defaultRoot : policy.assumedRoot;
    std::int64_t coveredBytes = 0, coveredFiles = 0;
    std::vector<std::pair<std::string, int>> perFolder;   // multi-project: which folder this answer is mostly about
    static std::unordered_set<std::string> creditedFiles;   // each file is credited once per session, not per answer
    if( ok && !root.empty() )
    {
        mt::forEachPathCandidate( r.resp, 400, [&]( const std::string& rel )
        {
            if( coveredFiles >= 200 ) return;
            std::error_code ec;
            std::filesystem::path p;
            for( const auto& folder : workspaceFolders( root ) )
            {
                // single-root answers spell paths root-relative; multi-root ones as <folder-name>/<rel>
                for( const auto& base : { std::filesystem::path( folder ), std::filesystem::path( folder ).parent_path() } )
                {
                    const auto candidate = base / rel;
                    if( std::filesystem::is_regular_file( candidate, ec ) && !ec ) { p = candidate; break; }
                    ec.clear();
                }
                if( !p.empty() )
                {
                    bool found = false;
                    for( auto& pf : perFolder ) if( pf.first == folder ) { ++pf.second; found = true; }
                    if( !found ) perFolder.emplace_back( folder, 1 );
                    break;
                }
            }
            if( p.empty() ) return;
            const auto sz = std::filesystem::file_size( p, ec );
            if( ec || !creditedFiles.insert( p.lexically_normal().string() ).second ) return;
            coveredBytes += static_cast<std::int64_t>( sz ); ++coveredFiles;
        } );
    }
    const std::int64_t avoided = mt::tokensForBytes( static_cast<std::uint64_t>( coveredBytes ), mt::kSourceBytesPerToken );

    // Repeat guard: an identical answer to an identical read-only question is not resent the second time.
    std::string detail = "codecortex_answer";
    if( ok )
    {
        const auto body = r.resp.find( "\"result\"" );
        const auto hit = meter.checkRepeat( name, argsObj, body == std::string::npos ? std::string_view( r.resp )
                                                                                     : std::string_view( r.resp ).substr( body ),
                                            callId, answerTokens );
        if( hit.suppress )
        {
            const std::string note =
                "CodeCortex: same result as call " + hit.earlierCallId + " earlier in this session (nothing changed), "
                "so it was not resent (~" + std::to_string( hit.tokensNotResent ) + " tokens saved). "
                "If that earlier result is no longer in your context, call this tool again with the same arguments "
                "and the full text will be returned.";
            r.resp = toolResult( rawId, note, false );
            meter.repeatTokensSaved += hit.tokensNotResent;
            ++meter.repeatsSuppressed;
            detail = "repeat_suppressed earlier_call=" + hit.earlierCallId + " tokens_saved=" + std::to_string( hit.tokensNotResent );
            answerTokens = mt::estimateAnswerTokens( r.resp );
        }
    }
    meter.answersTokens += answerTokens;
    meter.avoidedTokens += avoided;

    if( !telemetryPath.empty() && !ctx.activeRunId.empty() && name != "codecortex_status" )
    {
        rw::telemetry::Collector c( telemetryPath );
        if( c.enabled() )
        {
            rw::telemetry::Event e;
            e.type = rw::telemetry::EventType::ContextReturned;
            // In a multi-project workspace, file the answer under the project most of its files belong to, so
            // the dashboard's per-project view can show it; otherwise under the workspace's first folder.
            std::string attributed = root;
            if( perFolder.size() > 0 && workspaceFolders( root ).size() > 1 )
            {
                const auto best = std::max_element( perFolder.begin(), perFolder.end(),
                                                    []( const auto& a, const auto& b ) { return a.second < b.second; } );
                attributed = best->first;
            }
            decorateEvent( e, attributed, ctx.activeRunId, "derived" );
            e.target = name;
            e.detail = detail;
            e.toolCallId = callId;
            e.success = ok;
            e.responseBytes = static_cast<std::int64_t>( r.resp.size() );
            e.estTokens = answerTokens;
            e.avoidedTokensEst = avoided;
            e.coveredFiles = coveredFiles;
            c.record( e );
        }
    }
    return r;
}

// MCP roots (Step 3 of the setup plan): when CodeCortex was started WITHOUT a project folder, ask a client
// that supports roots which folder the user has open, and use it. An explicit folder always wins.
inline std::string fileUriToPath( std::string_view uri )
{
    if( !uri.starts_with( "file://" ) ) return {};
    std::string_view rest = uri.substr( 7 );
    if( rest.starts_with( "localhost/" ) ) rest.remove_prefix( 9 );
    std::string out;
    for( std::size_t i = 0; i < rest.size(); ++i )
    {
        if( rest[i] == '%' && i + 2 < rest.size() )
        {
            auto hex = []( char c ) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            const int hi = hex( rest[i + 1] ), lo = hex( rest[i + 2] );
            if( hi >= 0 && lo >= 0 ) { out.push_back( static_cast<char>( hi * 16 + lo ) ); i += 2; continue; }
        }
        out.push_back( rest[i] );
    }
    // file:///C:/x -> C:/x on Windows; file:///home/x stays /home/x.
    if( out.size() >= 3 && out[0] == '/' && std::isalpha( static_cast<unsigned char>( out[1] ) ) && out[2] == ':' ) out.erase( 0, 1 );
    return out;
}

inline std::string firstRootPath( const std::string& responseLine )
{
    const std::string result = mcpdetail::findObject( responseLine, "result" );
    const auto k = result.find( "\"uri\"" );
    if( k == std::string::npos ) return {};
    const auto q = result.find( '"', result.find( ':', k ) );
    if( q == std::string::npos ) return {};
    std::string uri;
    for( std::size_t i = q + 1; i < result.size() && result[i] != '"'; ++i )
    {
        if( result[i] == '\\' && i + 1 < result.size() ) { ++i; }
        uri.push_back( result[i] );
    }
    return fileUriToPath( uri );
}

inline int runCodeCortexMcp( int topK, bool stable, bool noRedact,
                             const std::string& root = {}, const std::vector<std::string>& roots = {},
                             std::string telemetryPath = {} )
{
    std::string defaultRoot;
    if( roots.size() >= 2 )
    {
        std::string wsErr; const std::string key = mcpWorkspaceKey( roots, wsErr );
        if( !key.empty() ) defaultRoot = mcpCanonRoot( key );
        workspacePrimaryFolder() = mcpCanonRoot( root.empty() ? roots.front() : root );
    }
    else if( !root.empty() ) defaultRoot = mcpCanonRoot( root );

    McpDispatchPolicy policy{ .legendSession = &mcpStdioLegendSession() };
    policy.defaultRoot = defaultRoot;
    if( defaultRoot.empty() ) policy.assumedRoot = mcpResolveAssumedRoot();
    const bool telemetryPinned = !telemetryPath.empty();
    if( telemetryPath.empty() ) telemetryPath = telemetryPathFor( defaultRoot.empty() ? policy.assumedRoot : defaultRoot );

    const bool explicitRoot = !defaultRoot.empty();
    bool clientHasRoots = false;
    int rootsRequests = 0;
    auto requestRoots = [&]()
    {
        const std::string req = "{\"jsonrpc\":\"2.0\",\"id\":\"ccx-roots-" + std::to_string( ++rootsRequests ) + "\",\"method\":\"roots/list\"}";
        std::fputs( req.c_str(), stdout ); std::fputc( '\n', stdout ); std::fflush( stdout );
    };

    std::string line; bool overflow = false;
    while( readByteSafeLineBounded( stdin, line, kMcpStdioLineMaxBytes, overflow ) )
    {
        if( overflow ) { emitMcpStdioLineOverflowRefusal(); continue; }
        if( line.find_first_not_of( " \t\r\n" ) == std::string::npos ) continue;
        const std::string method = mcpdetail::findString( line, "method" );
        if( method.empty() && mcpdetail::findRawId( line ).token.starts_with( "\"ccx-roots-" ) )
        {
            // The client's answer to OUR roots/list request — never dispatched as a request.
            const std::string path = firstRootPath( line );
            std::error_code ec;
            if( !explicitRoot && !path.empty() && std::filesystem::is_directory( path, ec ) )
            {
                policy.defaultRoot = mcpCanonRoot( path );
                policy.assumedRoot.clear();
                bridgeContext().activeRoot = policy.defaultRoot;
                if( !telemetryPinned ) telemetryPath = telemetryPathFor( policy.defaultRoot );
            }
            continue;
        }
        if( method == "initialize" )
        {
            const std::string caps = mcpdetail::findObject( mcpdetail::findObject( line, "params" ), "capabilities" );
            clientHasRoots = caps.find( "\"roots\"" ) != std::string::npos;
        }
        const auto r = dispatchCodeCortexMcpLine( line, topK, stable, noRedact, policy, telemetryPath );
        if( !r.isNotification )
        {
            std::fputs( r.resp.c_str(), stdout ); std::fputc( '\n', stdout ); std::fflush( stdout );
        }
        if( clientHasRoots && !explicitRoot && ( method == "notifications/initialized" || method == "notifications/roots/list_changed" ) )
            requestRoots();
    }
    return 0;
}

} // namespace codecortex_mcp
} // namespace rw
