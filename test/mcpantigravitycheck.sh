#!/usr/bin/env bash
# Reproduces the Antigravity failure paths against a built native CodeCortex:
# find_symbol + edit_check over one persistent stdio MCP session must emit JSON-RPC only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${1:-${CODECORTEX_BIN:-$ROOT/build/codecortex}}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
[ -x "$BIN" ] || { echo "missing executable $BIN" >&2; exit 2; }
command -v git >/dev/null 2>&1 || { echo 'git required' >&2; exit 2; }
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/repo"
cat > "$TMP/repo/sample.py" <<'PY'
def target(x):
    return x + 1

def caller():
    return target(1)
PY
git -C "$TMP/repo" init -q
git -C "$TMP/repo" config user.email codecortex-test@example.invalid
git -C "$TMP/repo" config user.name CodeCortexTest
git -C "$TMP/repo" add sample.py
git -C "$TMP/repo" commit -qm baseline
# Contract change after HEAD; caller remains fixed-arity at one argument.
python3 - "$TMP/repo/sample.py" <<'PY'
from pathlib import Path
p=Path(__import__('sys').argv[1]); s=p.read_text(); p.write_text(s.replace('def target(x):','def target(x, y=0):'))
PY
python3 - "$BIN" "$TMP/repo" <<'PY'
import json,subprocess,sys,time
binary,root=sys.argv[1:]
reqs=[
 {"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"find_symbol","arguments":{"path":root,"symbol":"target"}}},
 {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"edit_check","arguments":{"path":root,"symbol":"target"}}},
]
p=subprocess.Popen([binary,root,"--mcp"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
assert p.stdin and p.stdout
rows=[]
try:
    for q in reqs:
        p.stdin.write(json.dumps(q,separators=(",",":"))+"\n"); p.stdin.flush()
        line=p.stdout.readline()
        assert line,('no response',q,p.stderr.read() if p.stderr else '')
        rows.append(json.loads(line))
finally:
    p.terminate()
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill(); p.wait(timeout=3)
assert [r.get('id') for r in rows]==[1,2],rows
assert all(('result' in r) and ('error' not in r) for r in rows),rows
print('PASS: find_symbol + edit_check returned clean JSON-RPC frames in one persistent MCP session')
PY
