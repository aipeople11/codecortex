# One-click Claude Desktop extension (`.mcpb`)

The goal: a non-technical user downloads **one file**, double-clicks it, picks their project folder in Claude's
install dialog, and CodeCortex is working. No compiler, no PATH, no hand-written JSON.

A `.mcpb` (MCP Bundle, formerly `.dxt`) is a zip with `manifest.json` at its root. Spec:
<https://github.com/modelcontextprotocol/mcpb/blob/main/MANIFEST.md> (manifest v0.3).

## Files

| File | Purpose |
|---|---|
| `manifest.template.json` | The manifest. `version`, `entry_point`, `command` and `platforms` are filled per platform by the builder. |
| `mcpb-manifest-v0.3.schema.json` | Vendored official schema, so the build fails on an invalid manifest instead of the user's install failing. |
| `icon.png` | 512×512 icon made from `assets/brand/codecortex-logo.png`. |
| `../../scripts/build_mcpb.py` | Builds the bundle. |

Bundle layout mirrors an installed prefix, because `src/ui/server.cpp` finds the dashboard at
`<exe>/../share/codecortex/ui`:

```
manifest.json  icon.png  bin/codecortex[.exe]  share/codecortex/{ui,skills,hooks}/
```

## How it is produced

`.github/workflows/release.yml` builds `codecortex-<ver>-windows-x64.mcpb` (job `build-windows`) and
`codecortex-<ver>-macos-arm64.mcpb` (macOS leg) on every release tag and attaches them to the GitHub Release.
Claude desktop runs on Windows and macOS only, so Linux gets no bundle.

## Build one locally (from a binary you already built)

```bash
pip install jsonschema
python3 scripts/build_mcpb.py --binary build/codecortex --ui ui --skills skills --hooks hooks \
    --version 0.7.0 --platform darwin --out codecortex.mcpb      # use --platform win32 with codecortex.exe
```

## Test checklist (do this on the first real release)

1. Double-click the `.mcpb` → Claude desktop shows the CodeCortex install dialog with icon and description.
2. Pick a project folder → Install.
3. New chat: **"Show CodeCortex status"** → names that folder and the dashboard address.
4. Open <http://127.0.0.1:7331> → the dashboard loads (not "UI asset unavailable").
5. Claude → Settings → Extensions → CodeCortex → change the folder → status now names the new folder.

## Known limits

- **Several folders = one workspace.** The folder setting accepts up to 16 folders (not nested inside each
  other); the engine merges them into one graph. quality_delta / prove_change stay single-project: pass `path`.
- **Unsigned binaries** will trigger Windows SmartScreen / macOS Gatekeeper warnings until code signing is added
  to `release.yml`.
- Host-hook telemetry is not part of the bundle; Claude desktop uses the MCP path only.
