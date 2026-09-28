# CodeCortex P2H18 Evidence API + Indexed Backend Patch

Date: 2026-09-26
Parent candidate: `CODECORTEX_P2H18_PRODUCTIVITY_EVIDENCE_GRAPH_ENRICHED_2026-09-26.zip`

## Intent

Close the missing bridge between CodeCortex telemetry JSONL and the existing Developer Console without redesigning the UI.

## Implemented

- Added `src/evidence/evidence_service.{h,cpp}`.
- Incrementally indexes complete JSONL telemetry records and keeps a run/event index in memory.
- Removed the Developer Console dependency on the legacy 8 MB `/api/telemetry` tail. The UI now reads `/api/evidence?limit=5000` and the backend supports cursor pagination plus run-scoped retrieval.
- Added:
  - `GET /api/evidence`
  - `GET /api/evidence/runs`
  - `GET /api/evidence/run?id=...`
  - `GET /api/productivity[?id=...]`
  - `GET /api/causal/run?id=...`
  - `GET /api/efficiency/run?id=...`
- Reused `engineering_metrics` for efficiency output.
- Reused and minimally extended `causal_graph` for model/tool/read/search/edit/test/proof evidence.
- Extended telemetry provenance vocabulary with `provider_reported` and `derived`.
- `appendTokenUsage` now records `provider_reported` when a provider is supplied and `host_reported` otherwise.
- `appendToolCall` now records `observed` provenance.
- No token estimation and no savings claim were introduced.
- Existing navigation, colors, graph UI, memory pages, replay, health and Safe Command Center remain intact.
- Full multi-language parsers, grammars, third-party sources and benchmarks remain intact.

## Truth rules

- Missing tokens remain `NOT AVAILABLE`.
- Provider-reported tokens are labeled `PROVIDER-REPORTED`.
- Test execution provenance is derived from the actual event source, not inferred from event type.
- Repeated reads/searches are `DERIVED` from observed event repetition.
- Causal sequence edges are explicitly labeled `OBSERVED SEQUENCE; NOT CAUSAL PROOF`.
- Memory recalls are not presented as available when no recall evidence exists.

## Regression evidence

Focused CTest suite:

- `codecortex.telemetry` PASS
- `codecortex.efficiency` PASS
- `codecortex.causal` PASS
- `codecortex.evidence` PASS
- `codecortex.ui` PASS

`codecortex.evidence` covers:

- multiple runs
- missing token data
- provider-reported token data
- failed tool calls
- memory recall evidence
- host/model aggregation
- repeated reads
- test execution provenance
- causal graph generation
- efficiency generation
- pagination across a telemetry file larger than 8 MB

`codecortex.ui` exercises all six new HTTP API families.

JavaScript syntax:

- `ui/app.js` PASS (`node --check`)
- `ui/graph.js` PASS (`node --check`)

## Environment-limited gate

The full `codecortex` target was attempted twice with `-j1`. All parser/tree-sitter dependencies completed and the build reached `src/main.cpp.o`, but the build command exceeded this environment's execution timeout while compiling the large translation unit. No compiler diagnostic was emitted before termination.

Status: `ENV_BLOCKED` / full binary live proof pending. This is not recorded as a source PASS or FAIL.

The standalone upstream graph CLI is not installed in this VM, so standalone HTML parity remains a separate live-certification gate. No software was silently installed.
