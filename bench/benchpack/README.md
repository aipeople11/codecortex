# CodeCortex benchmark pack

Status: **proposed**. The scenarios in `scenarios.json` are defined; none is implemented or run yet, so nothing here is a measurement.

The pack tests whether CodeCortex reduces wrong assumptions, failed tool executions, retries, context waste and time to the first correct action. It does not only ask whether the final task passes.

The failure examples come from the team's historical agent-log analysis (91 sessions with hard errors across 327 logs; file and path errors were the largest category). Those counts were reported to us and are not re-verified in this repository.

## Where the checks sit

```
                           AGENT
                             |
                      action proposal
                             |
               +-------------+-------------+
               |                           |
         STATIC REFLEXES              RUNTIME REFLEXES
         CodeCortex graph             Execution monitor
               |                           |
         symbol exists?               repeated call?
         signature right?             repeated failure?
         caller impact?               token explosion?
         exemplar exists?             huge output?
         right tests?                 no progress?
               |                           |
               +-------------+-------------+
                             |
                       POLICY DECISION
                             |
               +-------------+-------------+
               |                           |
            PROCEED                  CORRECT / STOP
               |                           |
               +-------------+-------------+
                             |
                        TOOL EXECUTION
                             |
                             v
                        OBSERVATION
                             |
              update graph / runtime evidence
```

What exists today: CodeCortex answers graph questions when the agent asks (symbol lookup, callers, impact, tests, edit checks, exemplars). The automatic pre-action checking, the execution monitor and the policy decision above are **planned**, and are what this pack is designed to evaluate. Scenarios with `"layer": "static"` map to the graph tools that exist; `"layer": "runtime"` scenarios need the planned runtime reflexes.

## Implemented so far

`src/telemetry/reflex.h` is the first runtime reflex, applied to CodeCortex's own tool calls inside the MCP bridge. It is advice only: it appends one short corrective note to a result and never blocks a call or changes the answer.

| Scenario | Reflex | Trigger |
|---|---|---|
| B38, B16 | `retry_storm` | the same tool fails 3 times in a row (then every 5th failure) |
| B39, B42 | `no_progress` | the repeat guard replaces the answer 3 times in a row |
| B35 | `huge_output` | one answer of about 8000 tokens or more (with a cooldown) |

Also addressed in the MCP layer (from the 0.7.0 pre-edit repair candidate): B31 (stray stdout can no longer corrupt JSON-RPC frames, `src/mcp_stdio_quarantine.h`) and B34 (`quality_delta` with no baseline and no git HEAD now fails fast instead of waiting for the client timeout). Shell checks: `test/mcpstdoutquarantinecheck.sh`, `test/mcpbaselinefastcheck.sh`, `test/mcpantigravitycheck.sh`.

Unit tests: `test/verify_reflex.cpp`, including a negative control (small successful answers never produce a note). These tests have not been compiled or run yet.

Not implemented: the static reflexes as an automatic pre-action check (the graph tools exist and can be called, but nothing intercepts an agent's proposed action), host-tool monitoring (reads, shell commands and tests run by the assistant itself), the policy step that can stop an action, and every other scenario.

## Suites

| Suite | Theme | Scenarios |
|---|---|---|
| A | Repository intelligence (CodeCortex-native) | B01 to B12 |
| B | Filesystem and path intelligence | B13, B14, B15, B16, B19 |
| C | Tool and environment readiness | B17, B18, B19, B34 |
| D | Data and format safety | B22 to B26 |
| E | Search and command efficiency | B20, B21, B35, B43 |
| F | Execution loop intelligence | B38 to B42, B50 |
| G | Context and token efficiency | B35, B36, B37, B40, B50 |
| H | Runtime and protocol reliability | B28 to B33, B44 to B49 |

Scenarios in more than one suite (B19, B35, B40, B50) run once and are reported in each suite that lists them. B27 was not assigned to a suite in the source plan (candidate: D).

## Scoring

Score each scenario on five dimensions, not pass/fail:

| Metric | What to measure |
|---|---|
| Correctness | Did the agent ultimately solve the task? |
| Prevention | Did it avoid the known failure? |
| Efficiency | Calls, tokens and time versus the baseline |
| Recovery | If the failure happened, how many attempts before the strategy changed? |
| Evidence quality | Did the agent use repository or runtime evidence instead of guessing? |

For the context and token suites also record: total input tokens, peak context per call, model calls, tool calls, duplicate calls, context growth, time to useful progress.

Report format (layout only, not a result):

```
Scenario: B03 Wrong Function Arity
Baseline agent                CodeCortex-enabled
Result / Tool calls / Failures / Tokens / Time / First correct action
Improvement: failures avoided, tool calls, tokens, wall time (percent change)
```

A comparison is valid only when the baseline and CodeCortex runs use the same tasks, model and repositories.

## Negative controls

CodeCortex must also show it does not intervene when it should not. N01 to N05 in `scenarios.json` (standard-library import, local variable rename, Markdown edit, runtime network failure, binary-file parse failure) must produce no CodeCortex lookup, or defer to runtime and file tooling.

## First release (20 scenarios + 5 negative controls)

- Repository intelligence: B01, B02, B03, B04, B07, B08, B09, B11
- Execution safety: B13, B15, B16, B17, B20, B21
- Runtime efficiency: B35, B36, B38, B39, B42, B50
- Negative controls: N01 to N05

Entries flagged `"first_release": true` in `scenarios.json` are exactly these.

## Next steps

1. For each first-release scenario, add a fixture repository and a scripted trigger under `bench/benchpack/`.
2. Run a baseline agent and a CodeCortex-enabled agent on identical tasks and record the five metrics.
3. Build the runtime reflexes only for scenarios the baseline shows are actually costly.
