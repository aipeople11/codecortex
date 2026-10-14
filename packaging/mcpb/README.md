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

1. Claude → Settings → Extensions → drag the `.mcpb` onto the page → the CodeCortex install screen appears.
   If its Install button stays grey (a Claude Desktop bug in some builds), extract the Windows zip and use
   **Advanced settings → Install unpacked extension** on the folder that has `manifest.json` inside.
2. **Configure** → add one or more project folders (up to 16, not nested) → **Save**; set Read-only tools to
   **Always allow**.
3. Fully quit Claude and reopen it. New chat: **"Show CodeCortex status"** → lists the folder(s) and the
   dashboard address (7331, or the next free port if an older CodeCortex still holds it).
4. Open the dashboard → it loads (not "UI asset unavailable"); with several folders the Project menu shows
   **All projects** plus each folder.
5. Ask Claude **"Save to CodeCortex memory that …"** → the entry appears under **Memories**.

## Known limits

- **Several folders = one workspace.** The folder setting accepts up to 16 folders (not nested inside each
  other); the engine merges them into one graph. quality_delta / prove_change stay single-project: pass `path`.
- **Unsigned binaries** will trigger Windows SmartScreen / macOS Gatekeeper warnings until code signing is added
  to `release.yml`.
- Host-hook telemetry is not part of the bundle; Claude desktop uses the MCP path only.
