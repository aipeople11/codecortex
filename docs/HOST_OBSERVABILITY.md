# Host observability contract

CodeCortex stays a simple MCP + tools architecture. It does not add an intent classifier, planner swarm, or a second coding-agent runtime.

## Observation levels

| Level | What CodeCortex can prove | Dependency |
|---|---|---|
| 1 — Code intelligence | graph, symbols, impact, likely tests, Git/history, CodeCortex memory | CodeCortex only |
| 2 — MCP observed | CodeCortex MCP tool calls and results | MCP client |
| 3 — Run observed | host session/turn lifecycle, native tool calls, edits, shell/tests | host hooks/telemetry |
| 4 — Usage observed | provider/model token usage and cost | host/provider must report it |

Missing data is displayed as `Not reported by host`, never estimated.

## Supported hosts

### Codex

`codecortex install --host codex` registers the CodeCortex MCP and observation-only lifecycle hooks. The adapter uses host-provided session/turn/tool identifiers when present. It records bounded metadata only; it does not store prompt text or tool output.

### Claude Code

`codecortex install --host claude` registers the CodeCortex MCP and observation-only lifecycle hooks. The same canonical event schema is used so the dashboard remains host-neutral.

### Gemini CLI

`codecortex install --host gemini` registers the MCP. Gemini CLI exposes rich OpenTelemetry, including session, tool, file-operation, model and token fields, but CodeCortex does not enable that telemetry automatically because doing so changes the user's telemetry/privacy configuration. A future explicit opt-in adapter can ingest it.

### Other MCP clients

They receive full CodeCortex code intelligence through MCP. Runtime observation is only as rich as the host's documented hooks/telemetry surface.

## Truth rules

CodeCortex never claims private model reasoning, model "understanding percentage", or tool executions it did not observe. Repository facts are structural; host actions are host-reported; CodeCortex MCP actions are observed by CodeCortex. Token/cost metrics appear only when the host/provider reports them.
