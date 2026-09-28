# CodeCortex repository guide

This file applies to agents modifying CodeCortex itself.

- `src/` owns the native runtime and MCP bridge.
- `ui/` owns the local dashboard.
- `skills/` and `hooks/` are optional host integration assets.
- `third_party/` and inherited source notices must retain their license obligations.
- Public branding is CodeCortex / `codecortex` only.
- Runtime telemetry must distinguish observed actions from structural repository facts.
- Missing provider token/cost telemetry must be shown as unavailable/not reported, never estimated.
