# CodeCortex end-user onboarding

## What normal users should experience

CodeCortex is an MCP product, not a build-from-source exercise. A normal user should do one install, answer a few clear setup questions once, and then use Codex normally.

```text
Install CodeCortex
  -> detect Codex
  -> if missing, ask whether to install it
  -> register the CodeCortex MCP
  -> detect conflicting legacy direct MCPs and ask whether to disable them
  -> activate CodeCortex skills
  -> explain local hook telemetry and ask whether to enable hooks
  -> verify the MCP registration
  -> Done: open Codex on a repository
```

The default answers are designed for the normal CodeCortex path. Nothing destructive is done silently: removing a legacy MCP registration and enabling telemetry hooks both require a visible consent step.

## Preferred release install

Published releases should use the prebuilt installer. It must not require CMake or a compiler.

```bash
curl -fsSL <official-codecortex-install-url> | bash
```

The current repository also keeps `./install.sh` for developers building from source. That path may need CMake and a compiler and should not be the public onboarding path.

## Codex integration

When Codex is present, onboarding registers one server named `codecortex` equivalent to:

```text
codecortex . --mcp --mcp-ui
```

The same Codex MCP configuration is shared by the Codex CLI/IDE surfaces that read the Codex MCP config. The dashboard runs in the same CodeCortex process; no second terminal is required.

If legacy direct code-intelligence or memory MCP entries are detected, onboarding explains that they bypass CodeCortex run correlation and asks whether to disable those registrations. Their underlying engines/services are not deleted.

## Hooks

CodeCortex skills are activated automatically when possible. Hooks are recommended for richer routing/run visibility but can observe local tool metadata, so onboarding gives a concise disclosure and asks before enabling them. Declining hooks does not disable CodeCortex MCP or skills.

## Daily use

After one-time setup:

```text
Open Codex
  -> open/select a Git repository
  -> ask a normal coding question
  -> Codex calls CodeCortex when useful
  -> CodeCortex dashboard starts automatically
```

Optional first check in Codex:

```text
Show CodeCortex status
```

Dashboard default: `http://127.0.0.1:7331`.
