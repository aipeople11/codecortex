# Changelog

## [Unreleased] — contributions by Hafeez Izaz

- **OpenTelemetry (opt-in, local only):** receiver on 127.0.0.1:4318 with a one-click Turn on / Turn off card on
  the Tokens & cost page; prompts, replies and account ids are dropped on read.
- **Runtime reflexes:** retry storms, no-progress loops and huge answers add one short corrective note.
- **Benchmark pack:** 50 failure scenarios in 8 suites (specification; no runner yet).
- **MCP stdio guard:** stray stdout bytes written during a tool call are held back, so the client only receives
  valid JSON-RPC.
- **quality_delta:** with no baseline and no git HEAD it refuses at once instead of after the client's timeout.
- **from_trace on Windows:** `C:\` paths, drive-letter colons and the MSVC `path(line): error` shape now map to
  symbols.
- **JSX/TSX:** a 6-7 digit character reference (for example an emoji `&#129504;`) no longer fails the whole
  component; kParserVer 120.
- **Run log per project:** a tool call on a second project folder is also logged in that folder's `.codecortex`.
- **prove_change:** shows the refusal reason instead of only section headings.
- **whereis:** a zero over committed trees for a symbol the working tree defines now says
  `r="working-tree-only"` instead of looking like a missing symbol.
- **Dashboard status page:** optional items that are off (usage and activity not shared by the AI app, the
  optional shared memory service) are no longer shown as amber "needs a look"; they appear only when working.
- **Docs and setup:** dashboard port can move to 7332/7333; `CONNECT_GEMINI_CODEX.bat` registers the build with
  Gemini CLI and Codex CLI.

## [0.7.0] — 2026-10-13

- **Windows:** first prebuilt Windows download (zip) and Windows build fixes (Winsock include order, dashboard
  server and safe commands on the os:: layer).
- **One-click Claude Desktop extension** (`.mcpb`) for Windows and macOS — double-click, choose your project folder.
- **Tokens & cost:** exact per-prompt token usage from Claude Code (input, output, cache), estimated cost at
  Anthropic list prices (`ui/pricing.json`, overridable), and CodeCortex's own share of every conversation.
- **Less token waste:** identical repeated answers are not resent, smaller default answer budgets, savings
  estimates and waste alerts (re-reads, repeated searches, retry loops).
- **Dashboard:** new "Get started" checklist and "Tokens & cost" pages; retry chains drawn in the graph.
- **Fixes:** dashboard files now ship in every download; callers/callees truncation is disclosed.
- **Multi-project workspace:** up to 16 folders in one workspace; the dashboard's Project menu shows
  "All projects" plus each folder, and the dashboard moves to the next free port if an older one is running.
- **No more empty pages:** built-in project memory (`.codecortex/memory.jsonl`) powers Memories, Lessons and
  Actions without an external service; Profile shows the workspace's projects, top files and top concepts;
  lessons are also learned automatically from tools that only worked after retries.

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
