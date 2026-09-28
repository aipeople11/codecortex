// SPDX-License-Identifier: MIT
// CodeCortex MCP integration layer over the full upstream CodeCortex MCP catalog.
#pragma once

#include "mcp.h"
#include "memory/memory_service_client.h"
#include "project_identity.h"
#include "telemetry/collector.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rw
{
namespace codecortex_mcp
{
inline std::string telemetryPathFor( std::string_view root )
{
    if( const char* p = std::getenv( "CODECORTEX_TELEMETRY_FILE" ); p && *p ) return p;
    const std::filesystem::path base( root.empty() ? "." : std::string( root ) );
    return ( base / ".codecortex" / "runs.jsonl" ).string();
}

inline std::string dashboardUrl()
{
    if( const char* p = std::getenv( "CODECORTEX_DASHBOARD_URL" ); p && *p ) return p;
    return "http://127.0.0.1:7331";
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
    const auto id = rw::project::identify( root );
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
                     root, task.empty() ? "implicit_mcp_task" : task, true );
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

inline McpDispatchResult dispatchCodeCortexMcpLine( const std::string& line, int topK, bool stable, bool noRedact,
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
        const auto ident = rw::project::identify( effectiveRoot );
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
            "MCP tools: 42 available\n"
            "Memory: " + std::string( mh.ok ? "connected" : "unavailable" );
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
            const auto ident = rw::project::identify( effectiveRoot );
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
        const auto ident = rw::project::identify( effectiveRoot );
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
        const auto ident = rw::project::identify( effectiveRoot );
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

inline int runCodeCortexMcp( int topK, bool stable, bool noRedact,
                             const std::string& root = {}, const std::vector<std::string>& roots = {},
                             std::string telemetryPath = {} )
{
    std::string defaultRoot;
    if( roots.size() >= 2 )
    {
        std::string wsErr; const std::string key = mcpWorkspaceKey( roots, wsErr );
        if( !key.empty() ) defaultRoot = mcpCanonRoot( key );
    }
    else if( !root.empty() ) defaultRoot = mcpCanonRoot( root );

    McpDispatchPolicy policy{ .legendSession = &mcpStdioLegendSession() };
    policy.defaultRoot = defaultRoot;
    if( defaultRoot.empty() ) policy.assumedRoot = mcpResolveAssumedRoot();
    if( telemetryPath.empty() ) telemetryPath = telemetryPathFor( defaultRoot.empty() ? policy.assumedRoot : defaultRoot );

    std::string line; bool overflow = false;
    while( readByteSafeLineBounded( stdin, line, kMcpStdioLineMaxBytes, overflow ) )
    {
        if( overflow ) { emitMcpStdioLineOverflowRefusal(); continue; }
        if( line.find_first_not_of( " \t\r\n" ) == std::string::npos ) continue;
        const auto r = dispatchCodeCortexMcpLine( line, topK, stable, noRedact, policy, telemetryPath );
        if( r.isNotification ) continue;
        std::fputs( r.resp.c_str(), stdout ); std::fputc( '\n', stdout ); std::fflush( stdout );
    }
    return 0;
}

} // namespace codecortex_mcp
} // namespace rw
