# P2H18 Full Multi-Language Certification Baseline

This package intentionally unifies the latest P2H17 source fixes with the full Test Lab assets.

## Preserved

- Full CodeCortex source
- Full UI and CodeCortex branding
- How It Works page
- Full language query set
- Full vendored third-party parser/dependency tree
- Core regression tests
- Benchmark harnesses
- Evaluation prompts
- Research/preprint material
- Presentation/showcase material

## Counts at packaging time

- src files: 206
- test files: 1783
- third_party files: 233
- bench files: 396
- prompt files: 38
- language query directories: 23

## Validation truths preserved

- memory unavailable -> HTTP 503
- invalid/missing memory resource -> HTTP 404
- missing graph symbol -> HTTP 404
- no legacy product branding in UI/README
- CodeCortex logo assets present
- How It Works UI view present

## Packaging rule

Do not prune language/parser support to reduce source-package size. Scope test execution by selected language instead.
