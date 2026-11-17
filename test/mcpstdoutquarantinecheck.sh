#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/q.cpp" <<'CPP'
#include "mcp_stdio_quarantine.h"
#include <cstdio>
#include <iostream>
int main() {
    {
        rw::McpStdoutQuarantine q;
        std::fputs(".legacy-progress", stdout);
        std::cout << "<legacy/>";
        const auto n = q.finish();
        if (n != 25u) return 3;
    }
    std::fputs("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n", stdout);
    return 0;
}
CPP
CXX="${CXX:-c++}"
"$CXX" -std=c++20 -I"$ROOT/src" "$TMP/q.cpp" -o "$TMP/q"
"$TMP/q" >"$TMP/out" 2>"$TMP/err"
python3 - "$TMP/out" "$TMP/err" <<'PY'
import json,sys
out=open(sys.argv[1]).read(); err=open(sys.argv[2]).read()
lines=[x for x in out.splitlines() if x.strip()]
assert len(lines)==1,(out,err)
obj=json.loads(lines[0])
assert obj["jsonrpc"]=="2.0" and obj["id"]==1,(out,err)
assert ".legacy-progress" not in out and "<legacy/>" not in out,(out,err)
assert "suppressed 25 stray stdout byte(s)" in err,(out,err)
print("PASS: MCP stdout quarantine preserves one clean JSON-RPC frame")
PY
