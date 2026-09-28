# CodeCortex Deployment Architecture

## Default architecture: local-first, single-user

CodeCortex is designed primarily for one developer, one workstation, and one authoritative local workspace per project. The recommended connection is local **stdio MCP**.

```text
Developer workstation
│
├─ Codex
├─ Claude Code
├─ Gemini CLI
└─ any standards-compatible MCP client
      │
      │ local stdio MCP
      ▼
   CodeCortex
      │
      ├─ deterministic repository graph
      ├─ impact / dependency / test intelligence
      ├─ project-scoped memory adapter
      ├─ observed run telemetry
      ├─ proof / verification checks
      └─ Developer Console (127.0.0.1:7331)
```

The coding host remains the agent. CodeCortex does not add an intent classifier, planner swarm, model router, or second agent runtime.

## Why local-first

The authoritative inputs live beside the developer: repository bytes, Git history, branch/commit state, local coding-host activity, and project-scoped engineering history. Keeping CodeCortex local avoids a second synchronization and tenancy problem and lets the graph reflect the workspace actually being edited.

## Connection modes

### 1. Local stdio MCP — recommended

```text
codecortex <repo> --mcp --mcp-ui
```

Use this for coding CLIs and desktop tools that can launch a local MCP process. This is the primary supported deployment mode.

### 2. Local Streamable HTTP MCP — supported

Some clients prefer an HTTP endpoint. CodeCortex already supports its MCP handler over Streamable HTTP while keeping the workspace local:

```text
codecortex <repo> --mcp --listen=127.0.0.1:7332 --mcp-ui
```

MCP endpoint:

```text
http://127.0.0.1:7332/mcp
```

The Developer Console remains loopback-only on port 7331 unless changed with `--ui-port`.

### 3. Non-loopback / remote HTTP — advanced, not the default

CodeCortex can bind its existing HTTP MCP listener beyond loopback, but this is an advanced deployment surface rather than the recommended GitHub quickstart. The runtime refuses a non-loopback bind without a bearer token, keeps remote edits disabled unless explicitly enabled, and pins the listener to one workspace. It does not provide TLS; deploy behind a proper reverse proxy if used remotely.

A future multi-user service would need a separate architecture for authentication, tenancy, repository synchronization/local bridges, memory isolation, and workspace authority. Do not treat the current single-user local graph as a global multi-tenant graph service.

## Workspace authority

Every run resolves to an authoritative repository/workspace identity. Multiple local coding clients may use the same CodeCortex installation and project history, but each event must remain scoped by repository/project/session/run identity.

```text
Project
├─ authoritative workspace root
├─ repository identity
├─ branch / commit context
├─ code graph
├─ project memory namespace
└─ runs from one or more coding hosts
```

## Generic MCP compatibility

Any MCP-compatible client can use CodeCortex core capabilities even when CodeCortex has no custom adapter for that host.

```text
codecortex install --generic
```

prints the stdio and local-HTTP connection recipe. Enhanced host telemetry is conditional: recognized hosts can expose more lifecycle/tool data through their own hooks or telemetry interfaces, while an unknown MCP client still receives code intelligence, graph, impact, tests, memory, and proof tools.

## Product rule

**Install once. Connect locally. Keep the repository graph authoritative to the developer's actual workspace.**
