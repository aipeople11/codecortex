#!/usr/bin/env bash
# Compatibility gate for scripts/onboard-codex.sh -> unified CodeCortex installer.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/home"
cat > "$TMP/driver.cpp" <<'CPP'
#include "host_observer.h"
int main(int argc,char**argv){
  if(argc>1 && std::string_view(argv[1])=="configure-hooks") return rw::hostobserve::configureHooks(argc,argv,argv[0]);
  return 0;
}
CPP
${CXX:-c++} -std=c++20 -I"$ROOT/src" "$TMP/driver.cpp" "$ROOT/src/telemetry/collector.cpp" -o "$TMP/bin/codecortex"
cat > "$TMP/bin/codex" <<'EOF2'
#!/bin/sh
echo "$*" >> "$LOGFILE"
exit 0
EOF2
chmod +x "$TMP/bin/codecortex" "$TMP/bin/codex"
export HOME="$TMP/home" LOGFILE="$TMP/log"
PATH="$TMP/bin:$PATH" CODECORTEX_ASSET_ROOT="$ROOT" "$ROOT/scripts/onboard-codex.sh" --binary "$TMP/bin/codecortex" --yes >"$TMP/out"
grep -q '^mcp add codecortex' "$TMP/log"
[ -f "$HOME/.codex/hooks.json" ]
grep -q ' observe --host codex' "$HOME/.codex/hooks.json"
! grep -q 'codecortex-observe.py' "$HOME/.codex/hooks.json"
! grep -Eq 'codecortex-(codex-|claude-)?route|codecortex-nudge' "$HOME/.codex/hooks.json"
grep -q 'CodeCortex is ready' "$TMP/out"
echo "onboard codex: PASS"
