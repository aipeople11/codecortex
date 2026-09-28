#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/home"
export HOME="$TMP/home"
export CODECORTEX_BINARY="$TMP/bin/codecortex"
printf '#!/bin/sh\nexit 0\n' > "$CODECORTEX_BINARY"; chmod +x "$CODECORTEX_BINARY"
log="$TMP/host.log"; export LOGFILE="$log"
cat > "$TMP/bin/codex" <<'EOF'
#!/bin/sh
echo "codex $*" >> "$LOGFILE"
exit 0
EOF
cat > "$TMP/bin/claude" <<'EOF'
#!/bin/sh
echo "claude $*" >> "$LOGFILE"
exit 0
EOF
cat > "$TMP/bin/gemini" <<'EOF'
#!/bin/sh
echo "gemini $*" >> "$LOGFILE"
exit 0
EOF
chmod +x "$TMP/bin/codex" "$TMP/bin/claude" "$TMP/bin/gemini"
PATH="$TMP/bin:$PATH" CODECORTEX_ASSET_ROOT="$ROOT" bash "$ROOT/scripts/onboard.sh" --all --yes >/dev/null

grep -q 'codex mcp add codecortex' "$log"
grep -q 'claude mcp add codecortex' "$log"
grep -q 'gemini mcp add --scope user codecortex' "$log"
[ -f "$HOME/.codex/hooks.json" ]
[ -f "$HOME/.claude/settings.json" ]
! grep -q 'codecortex-route' "$HOME/.codex/hooks.json"
! grep -q 'codecortex-route' "$HOME/.claude/settings.json"
echo "p2h12 install: PASS"
