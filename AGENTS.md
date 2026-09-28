# CodeCortex agent instructions

CodeCortex is a C++23 MCP engineering-intelligence runtime with a local dashboard. Keep changes deterministic, evidence-based and repository-scoped.

Build:

```bash
cmake -S . -B build
cmake --build build -j
```

Focused validation should include the affected unit/integration checks plus the repository hygiene and licensing gates. Do not fabricate token/cost data when the host does not provide it. Keep observed runtime evidence separate from structural repository evidence.
