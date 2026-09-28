#!/usr/bin/env bash
# CodeCortex simple host onboarding. Safe to re-run; does not install coding hosts.
set -eu

host="auto"
yes=0
hooks=1
scope="user"
binary="${CODECORTEX_BINARY:-}"
print_generic=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --host) host="$2"; shift 2 ;;
    --host=*) host="${1#*=}"; shift ;;
    --all) host="all"; shift ;;
    --yes|-y) yes=1; shift ;;
    --no-hooks) hooks=0; shift ;;
    --generic|--print-generic) print_generic=1; host="generic"; shift ;;
    --project) scope="project"; shift ;;
    --binary) binary="$2"; shift 2 ;;
    *) echo "codecortex install: unknown option: $1" >&2; exit 2 ;;
  esac
done

[ -n "$binary" ] || binary="$(command -v codecortex 2>/dev/null || true)"
[ -n "$binary" ] && [ -x "$binary" ] || { echo "codecortex install: codecortex binary not found" >&2; exit 1; }

legacy_binary="$(command -v codecodex 2>/dev/null || true)"
if [ -n "$legacy_binary" ] && [ "$legacy_binary" != "$binary" ]; then
  echo "CodeCortex migration: legacy CodeCodex binary detected at $legacy_binary"
  echo "  Host registrations named codecodex will be removed; the old binary itself is left untouched."
fi

prefix="$(cd "$(dirname "$binary")/.." 2>/dev/null && pwd || true)"
root=""
for candidate in \
  "${CODECORTEX_ASSET_ROOT:-}" \
  "$prefix/share/codecortex" \
  "$(pwd)"
do
  [ -n "$candidate" ] || continue
  if [ -f "$candidate/skills/install.sh" ]; then root="$candidate"; break; fi
done
[ -n "$root" ] || { echo "codecortex install: bundled assets not found beside the installation" >&2; exit 1; }

skills="$root/skills/install.sh"

has() { command -v "$1" >/dev/null 2>&1; }

detect_hosts() {
  out=""
  has codex && out="$out codex"
  has claude && out="$out claude"
  has gemini && out="$out gemini"
  printf '%s\n' "$out"
}

if [ "$host" = auto ] || [ "$host" = all ]; then
  hosts="$(detect_hosts)"
else
  hosts=" $host"
fi

print_generic_recipe() {
  cat <<EOF

Generic MCP connection (recommended local mode)
  transport: stdio
  command:   $binary
  args:      --mcp --mcp-ui

For MCP clients that require HTTP, run CodeCortex locally:
  $binary --mcp --listen=127.0.0.1:7332 --mcp-ui
  MCP URL:   http://127.0.0.1:7332/mcp

Remote/non-loopback HTTP is advanced only. CodeCortex requires an explicit bearer token and pins one workspace; see docs/DEPLOYMENT_ARCHITECTURE.md.
EOF
}

if [ "$print_generic" = 1 ]; then
  print_generic_recipe
  exit 0
fi

if [ -z "$(printf '%s' "$hosts" | tr -d ' ')" ]; then
  cat <<EOF
CodeCortex is installed, but no supported coding CLI was detected.
Install Codex, Claude Code, or Gemini CLI, then run:
  codecortex install
EOF
  exit 0
fi

install_codex() {
  has codex || { echo "CodeCortex: Codex not found; skipped."; return; }
  codex mcp remove codecodex >/dev/null 2>&1 || true
  codex mcp remove codecortex >/dev/null 2>&1 || true
  codex mcp add codecortex -- "$binary" --mcp --mcp-ui >/dev/null
  [ -f "$skills" ] && bash "$skills" --codex >/dev/null 2>&1 || true
  hook_state="off"
  if [ "$hooks" = 1 ]; then
    if "$binary" configure-hooks --host codex >/dev/null; then hook_state="✓"; else hook_state="unavailable (MCP still works)"; fi
  fi
  echo "  Codex       MCP ✓  runtime hooks $hook_state  tokens: host not guaranteed"
}

install_claude() {
  has claude || { echo "CodeCortex: Claude Code not found; skipped."; return; }
  claude mcp remove codecodex --scope user >/dev/null 2>&1 || true
  claude mcp remove codecortex --scope user >/dev/null 2>&1 || true
  claude mcp add codecortex --scope user -- "$binary" --mcp --mcp-ui >/dev/null
  [ -f "$skills" ] && bash "$skills" --claude >/dev/null 2>&1 || true
  hook_state="off"
  if [ "$hooks" = 1 ]; then
    if "$binary" configure-hooks --host claude >/dev/null; then hook_state="✓"; else hook_state="unavailable (MCP still works)"; fi
  fi
  echo "  Claude Code MCP ✓  runtime hooks $hook_state  tokens: host not guaranteed"
}

install_gemini() {
  has gemini || { echo "CodeCortex: Gemini CLI not found; skipped."; return; }
  gemini mcp remove codecodex >/dev/null 2>&1 || true
  gemini mcp remove codecortex >/dev/null 2>&1 || true
  if [ "$scope" = project ]; then
    gemini mcp add --scope project codecortex "$binary" --mcp --mcp-ui >/dev/null
  else
    gemini mcp add --scope user codecortex "$binary" --mcp --mcp-ui >/dev/null
  fi
  echo "  Gemini CLI  MCP ✓  runtime telemetry: available via Gemini OTel (not enabled automatically for privacy)"
}

echo "CodeCortex detected and configuring:$hosts"
for h in $hosts; do
  case "$h" in
    codex) install_codex ;;
    claude) install_claude ;;
    gemini) install_gemini ;;
    generic) print_generic_recipe ;;
    *) echo "codecortex install: host '$h' is not auto-configured. Use --generic for the standard MCP recipe." >&2; exit 2 ;;
  esac
done

cat <<'EOF'

CodeCortex is ready.
Open your coding CLI in a repository and work normally.
Dashboard during an active MCP session: http://127.0.0.1:7331
Persistent local dashboard when you want it independently of the host:
  codecortex dashboard

Observation truth:
  CodeCortex graph/impact/memory     always available locally
  Host tool/session telemetry        available only when that host exposes hooks/telemetry
  Tokens/cost                         shown only when actually reported; never estimated

For another MCP-compatible CLI or desktop app:
  codecortex install --generic
EOF
