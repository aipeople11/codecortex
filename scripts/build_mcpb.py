#!/usr/bin/env python3
"""Build a one-click Claude Desktop extension (.mcpb) from a packaged CodeCortex release folder.

A .mcpb is a zip with manifest.json at its root (spec: github.com/modelcontextprotocol/mcpb, MANIFEST.md v0.3).
Opening it in the Claude desktop app shows an install dialog; the user picks their project folder and is done:
no compiler, no PATH edits, no hand-written JSON config.

Usage:
  build_mcpb.py --binary PATH --ui DIR --skills DIR --hooks DIR --version X.Y.Z --platform win32|darwin --out FILE.mcpb

Layout inside the bundle mirrors an installed prefix, because src/ui/server.cpp resolves the dashboard at
<exe>/../share/codecortex/ui. Keep the two in step:
  manifest.json  icon.png  bin/codecortex[.exe]  share/codecortex/{ui,skills,hooks}/
"""
import argparse, json, os, stat, sys, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEMPLATE = HERE.parent / "packaging" / "mcpb" / "manifest.template.json"
ICON = HERE.parent / "packaging" / "mcpb" / "icon.png"
SCHEMA = HERE.parent / "packaging" / "mcpb" / "mcpb-manifest-v0.3.schema.json"


def validate(m: dict) -> None:
    """Check against the official MCPB v0.3 schema (vendored copy). A manifest Claude rejects means a user sees
    an install error instead of an install dialog, so a bad manifest fails the build here, not on their machine."""
    try:
        import jsonschema
    except ImportError:
        sys.exit("build_mcpb: python package 'jsonschema' is required to validate the manifest (pip install jsonschema)")
    jsonschema.validate(m, json.loads(SCHEMA.read_text(encoding="utf-8")))


def manifest(version: str, platform: str) -> dict:
    m = json.loads(TEMPLATE.read_text(encoding="utf-8"))
    exe = "bin/codecortex.exe" if platform == "win32" else "bin/codecortex"
    m["version"] = version
    m["server"]["entry_point"] = exe
    m["server"]["mcp_config"]["command"] = "${__dirname}/" + exe
    m["compatibility"]["platforms"] = [platform]
    return m


def add_tree(z: zipfile.ZipFile, src: Path, arc: str) -> None:
    for p in sorted(src.rglob("*")):
        if p.is_file():
            z.write(p, f"{arc}/{p.relative_to(src).as_posix()}")


def main() -> int:
    ap = argparse.ArgumentParser()
    for a in ("--binary", "--ui", "--skills", "--hooks", "--version", "--platform", "--out"):
        ap.add_argument(a, required=a not in ("--skills", "--hooks"))
    # Also drop manifest.json + icon.png into an unpacked folder with the same layout, so Claude's
    # "Install unpacked extension" works on the extracted release zip (a fallback for Claude Desktop builds
    # whose .mcpb install dialog does not complete).
    ap.add_argument("--manifest-out")
    a = ap.parse_args()
    if a.platform not in ("win32", "darwin"):
        sys.exit("build_mcpb: --platform must be win32 or darwin (Claude desktop runs on these two only)")
    binary, ui = Path(a.binary), Path(a.ui)
    if not binary.is_file():
        sys.exit(f"build_mcpb: no binary at {binary}")
    if not (ui / "index.html").is_file():
        sys.exit(f"build_mcpb: {ui} has no index.html, so the dashboard would be broken")

    m = manifest(a.version, a.platform)
    validate(m)
    out = Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(m, indent=2) + "\n")
        z.write(ICON, "icon.png")
        # The executable bit must survive the zip on macOS, or Claude cannot launch the server.
        info = zipfile.ZipInfo(m["server"]["entry_point"])
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = (stat.S_IFREG | 0o755) << 16
        z.writestr(info, binary.read_bytes())
        add_tree(z, ui, "share/codecortex/ui")
        for opt, arc in ((a.skills, "share/codecortex/skills"), (a.hooks, "share/codecortex/hooks")):
            if opt and Path(opt).is_dir():
                add_tree(z, Path(opt), arc)
    if a.manifest_out:
        d = Path(a.manifest_out); d.mkdir(parents=True, exist_ok=True)
        (d / "manifest.json").write_text(json.dumps(m, indent=2) + "\n", encoding="utf-8")
        (d / "icon.png").write_bytes(ICON.read_bytes())
    print(f"build_mcpb: wrote {out} ({out.stat().st_size // 1024} KiB, platform {a.platform}, v{a.version})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
