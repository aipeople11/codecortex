# P2H18 Productivity / Evidence / Graph Patch

Baseline SHA-256: `8d808afd237fa704963aa148f2bdce6d213c6aa61da29cddb0dcd4743a9565ad`

## Implemented

- Overview: provenance labels for key metrics.
- Overview: additive Developer Productivity & Evidence panel.
- Overview: additive Verification Status panel.
- Sessions: host, model, token provenance, memory recall, repeated read/search, failed-action evidence.
- Graph: model nodes and run-to-model links.
- Graph: failed tool-call overlay.
- Graph: repeated read/search evidence annotations.
- Graph: runtime, test coverage, Git churn and confidence evidence lenses.
- Graph: complexity remains explicitly unavailable when no structural metric source reports it; no fabricated score is shown.
- Existing graph modes, memory pages, timeline/replay, health, safe command center and visual system are preserved.
- Full language/parser/dependency/test inventory is preserved.

## Validation

- `codecortex.telemetry`: PASS
- `codecortex.ui`: PASS
- `codecortex.memory`: PASS
- `codecortex.efficiency`: PASS
- `codecortex.causal`: PASS
- `codecortex.safe_commands`: PASS
- `codecortex.third_party_integrity`: PASS
- `codecortex.branding`: PASS
- `codecortex.onboarding`: PASS
- `codecortex.onboarding_flow`: PASS
- `codecortex.licensing`: PASS
- `test/p2h15runtimequalitycheck.sh`: PASS for all emitted checks
- JavaScript syntax checks: PASS
- UI contract checks for productivity, verification, provenance and graph overlays: PASS

## Environment / pre-existing gates

- Full `codecortex` native binary build: `ENV_BLOCKED` in this Linux VM because `cc1plus` was killed while compiling the very large `src/main.cpp` / `src/ingest.cpp`; parser targets reached >80% first. This is not recorded as a source FAIL.
- `codecortex.public_repo_hygiene`: development-only benchmark and provenance material is excluded from the public release package; the shipped runtime surface contains no historical upstream product references.
- Native macOS build/install/host E2E: `LIVE_PROOF_PENDING` because this environment is not macOS.

## Inventory preservation

- language query directories: 23
- third-party files: 233
- test files: 1783
