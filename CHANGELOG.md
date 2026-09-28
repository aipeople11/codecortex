# Changelog

## Local-First Release Candidate — 2026-09-26

- Froze the default deployment as single-user, local-workspace CodeCortex with stdio MCP.
- Documented and surfaced the existing Streamable HTTP MCP as an optional loopback/advanced transport, not the default architecture.
- Added generic MCP connection recipes for third-party CLIs and desktop clients.
- Added end-to-end service wiring, UI wiring, deployment architecture and product North Star documents.
- Extended UI health payload with deployment/MCP transport and capability metadata.
- Added a visible Runtime Wiring chain to the Health screen.
- Made core onboarding degrade safely when Python is unavailable; enhanced Codex/Claude observation remains conditional until a native helper replaces the Python adapter.
- Added portable `plugin.json` and `mcp.json` manifests for future GitHub/plugin distribution.

## 0.6.2 — CodeCortex public-source baseline

- Unified CodeCortex product branding, executable, MCP server and dashboard.
- One MCP surface for repository intelligence, memory integration, telemetry and proof checks.
- Live Run / Code / Architecture / Dependencies / Memory graph modes.
- Project/repository/run correlation and observed-vs-structural graph semantics.
- Guided Codex onboarding and local dashboard startup.
- Public-repository hygiene pass removing development-session artifacts and historical workspace traces.
- Truthful UI states for telemetry not supplied by the MCP host.
## Native MCP Stabilization — 2026-09-26

- Replaced the Python Codex/Claude host observer with native `codecortex observe`.
- Added native, idempotent `codecortex configure-hooks` so normal installation needs no Python runtime.
- Preserved generic stdio MCP as the recommended path; enhanced host telemetry remains capability-dependent.
- Added native observer privacy/idempotency regression coverage.
- Kept local HTTP as an advanced localhost option; remote/global MCP remains outside the stable local-first path.


## 0.7.0-rc — Runtime quality and migration hardening
- Migrates retired pre-0.7 MCP registrations to the canonical `codecortex` name.
- Stops pinning a literal `.` workspace in host MCP registration.
- Adds `codecortex dashboard [repo]` for a persistent loopback-only Developer Console independent of MCP connection lifetime.
- Returns meaningful HTTP error codes for unavailable memory and graph failures.
- Distinguishes optional/unconfigured memory from a failed configured memory service.
- Keeps the current CodeCortex neural UI/logo assets as the only shipped dashboard branding.
## P2H16 — Product Explanation UI

- Added a branded **How it works** page to the Developer Console.
- Explains MCP flow, local-first deployment, evidence provenance, North-Star outcomes, and graph node meanings.
- Keeps the explanation static and client-side; no new service or runtime dependency was introduced.


## P2H17 — Validation consistency and repository packaging

- Corrected the UI regression contract: unavailable optional memory endpoints now assert HTTP 503, and disallowed memory resources assert HTTP 404.
- Kept valid memory-provider success coverage in the dedicated memory adapter regression suite.
- Added `REPO_CONTENTS.md` documenting which directories are runtime/source requirements versus Test Lab material.
- Split the clean source candidate from benchmark, prompt, presentation, and paper assets instead of deleting those development resources.
