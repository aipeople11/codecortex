# Contributing to CodeCortex

CodeCortex changes should preserve four contracts: deterministic repository analysis, explicit evidence/provenance, project/session isolation, and truthful UI states.

## Build

```bash
cmake -S . -B build -DCODECORTEX_TESTS=ON
cmake --build build -j
```

## Validate

Run the focused tests for the area you changed. Before publishing a source package, also run:

```bash
ctest --test-dir build --output-on-failure -R 'codecortex\.(branding|licensing|public_repo_hygiene|onboarding|onboarding_flow)$'
node --check ui/app.js
node --check ui/graph.js
```

Do not add personal machine paths, development-session artifacts, old product/donor branding, or credentials to the public tree. Do not remove required third-party copyright/license notices.

Runtime metrics must distinguish measured zero from unavailable/not-reported data. Observed agent actions must not be presented as structural code facts, or vice versa.
