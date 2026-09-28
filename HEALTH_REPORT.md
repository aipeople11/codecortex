# CodeCortex Release Health Report — Local-First Release Candidate

Date: 2026-09-26

## Executive status

**Overall: AMBER / release-candidate, not yet final multi-host certification.**

The local-first architecture, stdio MCP, optional Streamable HTTP MCP, repository intelligence, UI server, graph APIs, telemetry model, memory adapter and public-repo packaging are present. The remaining material gate is live certification on real Codex/Claude/Gemini installations and a complete native build/link on the target release machines.

## End-to-end service health

| Area | Status | Evidence / limitation |
| --- | --- | --- |
| Local stdio MCP | GREEN | primary MCP runtime exists and uses the shared MCP dispatcher |
| Streamable HTTP MCP | GREEN for local/advanced use | existing `--listen` transport; loopback by default, token required for non-loopback, remote edits off by default |
| Generic MCP compatibility | GREEN | core server is standard MCP; `codecortex install --generic` prints connection recipes |
| Repository graph/index | GREEN | deterministic local index, graph, dependencies, architecture, impact/test surfaces present |
| Project/repository identity | GREEN | project/repo/workspace context is wired into MCP/telemetry/UI |
| MCP-owned telemetry | GREEN | CodeCortex can observe its own MCP calls and semantic events |
| Host-wide run telemetry | AMBER | host-dependent; Codex/Claude hook adapter exists, Gemini richer telemetry remains an explicit integration/certification item |
| Token/cost telemetry | AMBER / conditional | intentionally unavailable unless the host/provider reports authoritative usage |
| Memory adapter | AMBER / optional | client and UI proxy exist; external memory service health depends on configured service |
| Developer Console | GREEN | loopback UI/API, graph modes, health view and runtime wiring are present |
| Live neural graph data richness | AMBER | renderer is present; usefulness depends on successful host telemetry and project memory capture |
| Installer UX | GREEN for source/binary install flow | one `codecortex install` flow; generic stdio recipe included; Codex/Claude hook setup uses the native CodeCortex binary |
| No-Python enhanced observation | GREEN in source design | Codex/Claude observation and hook configuration now use native `codecortex observe` / `codecortex configure-hooks`; Python is not a runtime dependency |
| Native full release build | AMBER | prior runner reached large-TU compilation without compiler diagnostics but did not complete link within runner limit; target-machine certification required |
| Public repo hygiene/licensing | GREEN subject to final scan | MIT for CodeCortex-original work; inherited/third-party notices retained where legally required |
| Remote multi-user/global service | NOT A V1 TARGET | current architecture is intentionally single-user/local-workspace; global tenancy requires a separate design |

## Truthfulness gates

- Structural code relationships are not labeled as observed agent actions.
- Test selection is not labeled as test execution.
- Proof checks do not claim a test suite was executed unless observed.
- Token/cost values are not estimated when the host does not report them.
- Generic MCP clients receive core intelligence even if deeper host observation is unavailable.


## Validation executed in this build environment

The following focused gates passed on the exact release-candidate tree:

```text
codecortex.telemetry              PASS
codecortex.memory                 PASS
codecortex.ui                     PASS
codecortex.efficiency             PASS
codecortex.causal                 PASS
codecortex.safe_commands          PASS
codecortex.third_party_integrity  PASS
codecortex.branding               PASS
codecortex.onboarding             PASS
codecortex.onboarding_flow        PASS
codecortex.licensing              PASS
codecortex.public_repo_hygiene    PASS
```

Additional checks:

- `ui/app.js` syntax: PASS
- `ui/graph.js` syntax: PASS
- portable `plugin.json` / `mcp.json` JSON validation: PASS
- `scripts/onboard.sh` shell syntax: PASS
- `src/ui/server.cpp` targeted compile: PASS
- full CMake configure: PASS
- full native build: CMake configure passed; this runner was OOM-killed while compiling the existing large `src/main.cpp` translation unit at 84%, so final target-machine link certification remains pending.
- native host-observer focused compile/privacy/idempotency gate: PASS.
- Codex onboarding with native hook configurator: PASS.
- Optional repository-mapping impact/test gate: not executed because that local mapper is not installed in this runner; nothing was installed silently.

## Release blockers before a public 1.0 claim

1. Complete native build/link and installer certification on macOS, Windows and Linux release targets.
2. Run real Codex hook E2E and prove non-zero host tool events under the same native session/turn.
3. Run real Claude Code E2E with the current supported hook schema.
4. Decide and certify the Gemini telemetry adapter with explicit privacy opt-in.
6. Run final branding, licensing, public-hygiene and third-party-integrity gates on the exact release archive.

## Recommended release designation

Use **Local-First Release Candidate** until the above runtime certification gates are green. The architecture itself is suitable for the intended public GitHub positioning: local-first stdio MCP by default, optional local HTTP, one project-aware CodeCortex intelligence layer, and host-specific telemetry only when supported.

## 0.7.0 runtime-quality closure
The installer now migrates retired pre-0.7 host registrations, current UI assets are branded only as CodeCortex, a standalone `codecortex dashboard` mode is available when persistent UI lifetime is desired, and memory/graph API failures use non-200 HTTP status codes. Memory remains optional; `configured=false` is distinct from a configured service that is unhealthy.
