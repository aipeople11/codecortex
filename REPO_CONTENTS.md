# CodeCortex repository contents

CodeCortex is a local-first native MCP server. The source repository is intentionally larger than the future end-user binary because it carries an offline-build toolchain, language parsers, tests, and optional benchmark material.

## Required for the source build

- `src/` — CodeCortex runtime, graph, MCP, telemetry, memory adapter, installer, and UI server.
- `ui/` — bundled local Developer Console assets.
- `queries/` — language query definitions used by repository intelligence.
- `third_party/` — vendored dependencies and parser source trees. This is the largest directory and is required by the current cold/offline source build.
- `cmake/`, `CMakeLists.txt` — build system and integrity gates.
- `hooks/`, `skills/`, `scripts/`, `packaging/` — host integration and installation support.
- `test/` — regression suite and fixtures used to certify the source candidate.
- `LICENSE`, `LICENSES/`, `NOTICE`, `LEGAL/`, `THIRD_PARTY.md`, `provenance/` — licensing and required provenance.

## Development/test-lab material, not needed by normal users

These are distributed separately from the clean source candidate:

- `bench/` — historical benchmark corpora, results, and benchmark harnesses.
- `prompts/` — development/evaluation prompts.
- `present/` — showcase decks and presentation assets.
- `paper/` — research/preprint material.
- prior phase validation notes — development history, not runtime code.

## Why `third_party/` is large

The current source tree vendors its parser/dependency sources so a fresh clone can build reproducibly without fetching code during configuration. It should remain in the source repository until CodeCortex deliberately moves to a dependency-fetch or prebuilt-parser distribution model.

For end users, the eventual GitHub Release binary will not need to ship the complete source `third_party/` tree. The installed runtime target remains one native `codecortex` executable plus bundled UI/config assets.
