# CodeCortex End-to-End Service Wiring

This document is the canonical wiring map for the public repository.

## Runtime path

```text
┌─────────────────────────────────────────────────────────────┐
│ Coding host                                                 │
│ Codex / Claude Code / Gemini CLI / generic MCP client      │
└──────────────────────────────┬──────────────────────────────┘
                               │ MCP
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ CodeCortex MCP surface                                      │
│ shared dispatch for stdio and Streamable HTTP              │
└───────┬──────────────────────┬──────────────────────┬───────┘
        │                      │                      │
        ▼                      ▼                      ▼
 Code intelligence        Memory adapter        Telemetry collector
 graph / symbols          optional service      observed facts only
 impact / deps            project scoped        JSONL event stream
 tests / quality
        │                      │                      │
        └──────────────┬───────┴──────────────┬──────┘
                       ▼                      ▼
                Proof / verification     Causal/run model
                       │                      │
                       └──────────┬───────────┘
                                  ▼
                     Developer Console API
                                  │
              ┌───────────────────┼───────────────────┐
              ▼                   ▼                   ▼
           Overview             Graph              Health
        runs / KPIs       code/runtime/memory   wiring/capability
```

## Components and owners

| Component | Owner in repo | Role | Required? |
| --- | --- | --- | --- |
| MCP stdio transport | `src/mcp.h`, `src/codecortex_mcp_bridge.h` | primary local connection | Yes |
| MCP Streamable HTTP transport | `src/mcpserver.h` | optional local/advanced HTTP transport | Optional |
| Code graph/index | `src/ingest*`, `src/graph.h`, `src/mcpindex.h` | deterministic repository intelligence | Yes |
| MCP tool catalog | `src/mcp*.h`, `src/mcpverbs.h` | exposes code intelligence and CodeCortex tools | Yes |
| Project identity | `src/project_identity.h` | repo/project/session scoping | Yes |
| Telemetry collector | `src/telemetry/` + MCP bridge | run/tool/events owned by CodeCortex | Yes for metrics |
| Host observation adapter | native `codecortex observe` + host hook configuration | additional host lifecycle/tool facts | Conditional |
| Memory adapter | `src/memory/` | durable project lessons/history | Optional/degraded-safe |
| UI server | `src/ui/server.cpp` | loopback API and static dashboard | Optional but recommended |
| UI graph facade | `src/ui/graph_service.*` | graph data for dashboard | UI only |
| Browser UI | `ui/` | developer-facing views | UI only |

## MCP request wiring

Both stdio and HTTP MCP transports route into the same MCP dispatch layer. The HTTP server is a transport adapter, not a second tool engine. This prevents tool behavior from diverging by transport.

```text
stdio request ─────────┐
                       ├─► shared MCP dispatch ─► tool implementation
HTTP /mcp request ─────┘
```

## Telemetry wiring

```text
CodeCortex MCP call ─► MCP bridge ─► .codecortex/runs.jsonl
Host hook/telemetry ─► host adapter ─► .codecortex/runs.jsonl
                                      │
                                      └─► `/api/telemetry` ─► UI
```

The event stream distinguishes observed/runtime information from structural repository relationships. Missing host data remains unavailable rather than being fabricated.

## Memory wiring

```text
CodeCortex tool / UI
       │
       ▼
MemoryServiceClient
       │
       ├─ connected memory service -> sessions/lessons/actions/etc.
       └─ unavailable -> CodeCortex continues with code intelligence
```

Memory is optional to core repository analysis. A memory outage must not make the code graph unavailable.

## UI API wiring

| UI need | Local API | Backend owner |
| --- | --- | --- |
| Runtime/service status | `/api/health` | UI server + project identity + memory health |
| Observed run events | `/api/telemetry` | telemetry JSONL |
| Memory views | `/api/memory/*` | bounded memory proxy |
| Symbol graph | `/api/graph/symbol` | `GraphService` |
| Architecture graph | `/api/graph/architecture` | `GraphService` |
| Community | `/api/graph/community` | `GraphService` |
| Path | `/api/graph/path` | `GraphService` |
| Dependencies | `/api/graph/dependencies` | `GraphService` |
| Safe local commands | `/api/command/*` | safe-command allowlist |

The dashboard server binds to `127.0.0.1`, not all interfaces.

## Truth boundaries

1. A CodeCortex tool call is observed because CodeCortex owns it.
2. A host-native tool action is observed only when a supported host reports it.
3. Token/cost metrics are shown only when authoritative usage is reported.
4. Test selection is not test execution.
5. A proof check is not equivalent to an executed regression suite.
6. Structural graph relationships are not presented as runtime actions.
