# CodeCortex validation posture

CodeCortex should be evaluated on task correctness, repository-understanding accuracy, impact/test selection, tool efficiency, repeated-work rate, memory usefulness, false-completion rate, latency, and actual provider token/cost telemetry when the host reports it.

No token or cost savings may be fabricated from fixed assumptions. A metric unavailable from the host must be labelled unavailable/not reported.

For release/source validation, use the CTest integration gates plus focused repository fixtures. Historical experiment artifacts are intentionally not shipped in the clean public source repository.
