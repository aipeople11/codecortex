#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cat > "$TMP/obs_tool.cpp" <<'CPP'
#include "host_observer.h"
int main(int argc,char**argv){
  if(argc>1 && std::string_view(argv[1])=="observe") return rw::hostobserve::observe(argc,argv);
  if(argc>1 && std::string_view(argv[1])=="configure-hooks") return rw::hostobserve::configureHooks(argc,argv,argv[0]);
  return 2;
}
CPP
${CXX:-c++} -std=c++20 -I"$ROOT/src" "$TMP/obs_tool.cpp" "$ROOT/src/telemetry/collector.cpp" -o "$TMP/codecortex-observer-test"
BIN="$TMP/codecortex-observer-test"
export CODECORTEX_TELEMETRY_FILE="$TMP/runs.jsonl"

printf '%s' '{"hook_event_name":"UserPromptSubmit","session_id":"s1","turn_id":"t1","cwd":"'"$TMP"'","model":"gpt-test","prompt":"SECRET PROMPT"}' | "$BIN" observe --host codex
printf '%s' '{"hook_event_name":"PreToolUse","session_id":"s1","turn_id":"t1","tool_use_id":"tool-1","tool_name":"Read","tool_input":{"file_path":"src/a.cpp"},"cwd":"'"$TMP"'"}' | "$BIN" observe --host codex
printf '%s' '{"hook_event_name":"PostToolUse","session_id":"s1","turn_id":"t1","tool_use_id":"tool-1","tool_name":"Read","tool_input":{"file_path":"src/a.cpp"},"tool_response":"SECRET OUTPUT","cwd":"'"$TMP"'"}' | "$BIN" observe --host codex
printf '%s' '{"hook_event_name":"PreToolUse","session_id":"s1","turn_id":"t1","tool_use_id":"tool-2","tool_name":"Bash","tool_input":{"command":"pytest tests/test_a.py -q"},"cwd":"'"$TMP"'"}' | "$BIN" observe --host codex
printf '%s' '{"hook_event_name":"PostToolUseFailure","session_id":"s1","turn_id":"t1","tool_use_id":"tool-2","tool_name":"Bash","tool_input":{"command":"pytest tests/test_a.py -q"},"tool_response":"SECRET TEST OUTPUT","cwd":"'"$TMP"'"}' | "$BIN" observe --host claude
printf '%s' '{"hook_event_name":"Stop","session_id":"s1","turn_id":"t1","cwd":"'"$TMP"'"}' | "$BIN" observe --host codex

python3 - "$TMP/runs.jsonl" <<'PY'
import json,sys
rows=[json.loads(x) for x in open(sys.argv[1])]
assert rows[0]['type']=='run_started' and rows[0]['run_id']=='t1'
assert any(x['type']=='tool_call' and x['target']=='Read' for x in rows)
assert any(x['type']=='file_read' and x['target']=='src/a.cpp' for x in rows)
assert any(x['type']=='test' and x['success'] is False for x in rows)
assert rows[-1]['type']=='run_finished'
blob=open(sys.argv[1]).read()
assert 'SECRET PROMPT' not in blob and 'SECRET OUTPUT' not in blob and 'SECRET TEST OUTPUT' not in blob
assert all(x.get('source')=='host_reported' for x in rows)
assert any(x.get('turn_id')=='t1' for x in rows)
assert any(x.get('tool_call_id')=='tool-1' for x in rows)
PY

HOME="$TMP/home"; export HOME; mkdir -p "$HOME"
"$BIN" configure-hooks --host codex >/dev/null
"$BIN" configure-hooks --host codex >/dev/null
python3 - "$HOME/.codex/hooks.json" <<'PY'
import json,sys
j=json.load(open(sys.argv[1]))
for ev in ['SessionStart','UserPromptSubmit','PreToolUse','PostToolUse','Stop','Interrupt']:
    rows=[h for g in j['hooks'].get(ev,[]) for h in g.get('hooks',[]) if ' observe --host codex' in h.get('command','')]
    assert len(rows)==1,(ev,len(rows))
blob=open(sys.argv[1]).read()
assert 'codecortex-observe.py' not in blob
assert 'codecortex-route' not in blob
PY

echo "p2h12 native observation: PASS"
