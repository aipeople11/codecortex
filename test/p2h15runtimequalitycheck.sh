#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
ok(){ printf 'PASS %s\n' "$1"; }
no(){ printf 'FAIL %s\n' "$1" >&2; fail=1; }
rg -q 'project\(codecortex VERSION 0\.7\.0' "$ROOT/CMakeLists.txt" && ok 'product version advanced' || no 'product version not advanced'
! rg -q 'CodeCodex|codecodex' "$ROOT/ui" && ok 'UI has no legacy brand' || no 'legacy brand leaked into UI'
rg -q 'codex mcp remove codecodex' "$ROOT/scripts/onboard.sh" && ok 'Codex legacy MCP migration' || no 'Codex legacy migration missing'
rg -q 'claude mcp remove codecodex' "$ROOT/scripts/onboard.sh" && ok 'Claude legacy MCP migration' || no 'Claude legacy migration missing'
rg -q 'gemini mcp remove codecodex' "$ROOT/scripts/onboard.sh" && ok 'Gemini legacy MCP migration' || no 'Gemini legacy migration missing'
! rg -q 'mcp add codecortex -- "\$binary" \. --mcp' "$ROOT/scripts/onboard.sh" && ok 'installer no longer pins dot workspace' || no 'installer still pins dot workspace'
rg -q 'std::string_view\( argv\[1\] \) == "dashboard"' "$ROOT/src/main.cpp" && ok 'persistent dashboard command exists' || no 'dashboard command missing'
rg -q '503 Service Unavailable' "$ROOT/src/ui/server.cpp" && ok 'unavailable services return 503' || no '503 semantics missing'
rg -q '404 Not Found' "$ROOT/src/ui/server.cpp" && ok 'missing resources return 404' || no '404 semantics missing'
rg -q 'memoryConfigured' "$ROOT/src/ui/server.cpp" && ok 'memory health exposes configured state' || no 'memory configured state missing'
rg -q 'product.*CodeCortex' "$ROOT/src/ui/server.cpp" && ok 'health identifies CodeCortex product' || no 'health product identity missing'
rg -q 'cfg.version' "$ROOT/src/ui/server.cpp" && ok 'health exposes running version' || no 'health version missing'
rg -q 'cfg.executable' "$ROOT/src/ui/server.cpp" && ok 'health exposes running binary path' || no 'health binary path missing'
exit "$fail"
