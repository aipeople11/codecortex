# Hermes skills

Hermes-format counterpart to the `codecortex-*` skill family. Kept in its own namespace (not
`skills/codecortex-*`) so `skills/install.sh` — which symlinks every `codecortex-*` directory into
Claude Code / Codex skill roots — never installs it into those agents.

Install into a Hermes profile:

```bash
cp -r skills/hermes/codecortex-repo-map ~/.hermes/skills/software-development/
```

Requires the `codecortex` binary on PATH (upstream release tarball, see repo README).
