# P2H17 validation consistency and repository cleanup

## Fixed contracts

- Optional/unconfigured memory endpoints return HTTP `503 Service Unavailable` and the UI regression now asserts that behavior.
- Disallowed memory resources return HTTP `404 Not Found` and are regression-covered.
- Missing/unavailable graph symbol targets return HTTP `404 Not Found`; the stale UI regression expectation was corrected from `200 OK`.
- Successful memory-provider REST operations remain covered by `test/verify_memory.cpp` using a loopback mock provider.

## Focused validation

- `codecortex_test_ui`: PASS (54 assertions).
- `codecortex_test_memory`: PASS (36 assertions).
- CMake configure with `CODECORTEX_TESTS=ON`: PASS.

## Repository packaging

The clean source candidate excludes development-only benchmark, prompt, presentation, paper, and old phase-validation artifacts. They are preserved in a separate Test Lab supplement.

The vendored `third_party/` tree remains in the source candidate because the current build intentionally supports a cold/offline reproducible source build. It is not part of the future minimal installed runtime binary.

## Clean package validation

- Clean source size: about 276 MB raw; the majority is vendored parser/dependency source under `third_party/`.
- Test Lab supplement size: about 28 MB raw.
- Clean source files: 2,374.
- Test Lab files: 444.
- Product-surface branding gate: PASS.
- Public-repository hygiene gate: PASS.
- Third-party integrity gate: PASS (233 files).
- Licensing boundary gate: PASS.
