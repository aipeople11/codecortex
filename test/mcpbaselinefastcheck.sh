#!/usr/bin/env bash
# Regression: baseline absence must fail via JSON-RPC without whole-tree ingest.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-${CODECORTEX_BIN:-$ROOT/build/codecortex}}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
[ -x "$BIN" ] || { echo "missing executable $BIN" >&2; exit 2; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/source"
printf 'int sample(){return 0;}\n' > "$TMP/source/a.cpp"
python3 - "$BIN" "$TMP/source" <<'PY'
import json, subprocess, sys, time
binary, path = sys.argv[1:]
request = {"jsonrpc":"2.0","id":91,"method":"tools/call","params":{"name":"quality_delta","arguments":{"path":path}}}
start = time.monotonic()
p = subprocess.run([binary,"--mcp"],input=json.dumps(request)+"\n",text=True,capture_output=True,timeout=8)
seconds = time.monotonic()-start
assert p.returncode==0,(p.returncode,p.stderr)
responses=[json.loads(x) for x in p.stdout.splitlines() if x.strip()]
assert len(responses)==1,responses
r=responses[0]
assert r['id']==91 and r['error']['code']==-32602,r
assert 'no git HEAD' in r['error']['message'],r
assert seconds<5,(seconds,r)
print(f'PASS: missing baseline returns typed JSON-RPC refusal in {seconds:.3f}s')
PY
