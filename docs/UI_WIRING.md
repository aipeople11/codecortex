# Developer Console UI Wiring

The Developer Console is a loopback-only browser UI. It does not own a second analysis engine; it reads the same repository intelligence, telemetry, and memory surfaces used by CodeCortex.

## UI data flow

```text
Browser UI
│
├─ Overview / Sessions / Timeline / Activity
│      └─ `/api/telemetry`
│
├─ Graph
│      ├─ Live Run       <- observed telemetry
│      ├─ Code           <- symbol graph API
│      ├─ Architecture   <- architecture/community graph API
│      ├─ Dependencies   <- dependency graph API
│      └─ Memory         <- project-scoped memory + run evidence
│
├─ Memory / Lessons / Actions / Crystals / Replay
│      └─ `/api/memory/*`
│
├─ Health
│      ├─ `/api/health`
│      └─ `/api/telemetry`
│
└─ Safe commands
       └─ `/api/command/*`
```

## Graph semantics

The graph deliberately distinguishes two evidence families:

- **Observed:** an action/event CodeCortex actually saw in a run.
- **Structural:** a repository relationship derived from the code/index.

Graph modes are developer questions, not different graph engines:

| Mode | Developer question | Primary source |
| --- | --- | --- |
| Live Run | What did the coding agent actually do? | observed telemetry |
| Code | What symbols/files are related? | code graph |
| Architecture | How is the system organized? | communities/modules |
| Dependencies | What depends on what? | dependency graph |
| Memory | What prior engineering knowledge is relevant? | project memory |

Node styling combines glyph, shape, color, and selection/halo state so meaning does not depend on color alone.

## Health view

The Health screen now surfaces the end-to-end chain:

```text
Coding host
  -> CodeCortex MCP
  -> Code intelligence
  -> Memory (optional)
  -> Telemetry
  -> Developer UI
```

The health view must distinguish **ready**, **conditional/not reported**, and actual failures. For example, missing token telemetry from a host is not a CodeCortex service failure.

## UI security

- Dashboard binds to `127.0.0.1`.
- CSP restricts resources to self.
- Memory proxy exposes an allowlisted set of resources.
- Safe-command API uses a backend allowlist rather than arbitrary shell execution.
- Graph path overrides are canonicalized to an existing directory.
