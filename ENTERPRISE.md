# CodeCortex enterprise readiness

CodeCortex remains an on-demand intelligence layer, not an orchestrator. Enterprise mode changes
operational posture and integration boundaries; it does not take control of the coding agent.

## Stable enterprise seams

- **MCP:** curated 8-tool profile for Codex, Claude Code, Cursor and other MCP hosts.
- **Remote MCP:** non-loopback binding requires a bearer token; TLS/SSO belong at the enterprise
  reverse proxy or gateway.
- **Developer Console:** loopback-only by default. Do not expose the console directly to a network.
- **Telemetry:** append-only JSONL suitable for forwarding to SIEM/observability pipelines.
- **Memory:** CodeCortex Memory is behind `MemoryProvider`; another enterprise memory backend can replace it.
- **Edition metadata:** `CODECORTEX_EDITION=enterprise` and `CODECORTEX_ORG=<name>` are exposed through
  `/api/health` for packaging/support diagnostics. They do not bypass security or unlock hidden code.
- **Secrets:** use environment variables / an enterprise secret manager. Never commit bearer tokens.

## Recommended enterprise deployment

```text
Coding agents
   | stdio MCP (developer laptop)
   | or authenticated MCP HTTP
   v
CodeCortex
   +-- repository/code graph
   +-- bounded telemetry -> JSONL -> SIEM/OTel adapter
   +-- MemoryProvider -> approved memory service
   `-- local Developer Console (127.0.0.1 only)
```

For remote deployments, terminate TLS and enterprise identity at a reverse proxy/gateway, keep
CodeCortex workspace-scoped, and leave remote edits disabled unless the organization explicitly
authorizes them.

## Commercial extension boundary

Future enterprise-only connectors (SSO metadata, SIEM exporters, policy adapters, fleet inventory,
central admin) should live outside the deterministic code-intelligence core and communicate through
small provider interfaces. This keeps community/enterprise behavior comparable and avoids a second
execution engine.
