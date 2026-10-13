/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Saurabh Verma
 *
 * Tokens & cost view. Three kinds of numbers, never mixed:
 *   HOST-REPORTED  token_usage events  — the API's own counts, read from the Claude Code transcript.
 *   ESTIMATE       cost               — host-reported tokens x list price from /pricing.json.
 *   DERIVED        context_returned   — CodeCortex's estimate of its own answers and of the file reads they
 *                                       covered. Shown even when the host reports nothing (Claude Desktop).
 * Loaded before app.js; app.js's renderAll() calls CodeCortexTokens.render(). Uses app.js globals
 * ($, esc, fmtInt, fmtTime, contextEvents, provenanceBadge) only at render time.
 */
(function(){
  let pricing=null, pricingRequested=false;

  function loadPricing(){
    if(pricingRequested)return; pricingRequested=true;
    fetch('/pricing.json',{cache:'no-store'}).then(r=>r.ok?r.json():null).then(j=>{pricing=j; if(window.CodeCortexTokens)window.CodeCortexTokens.render();}).catch(()=>{pricing=null;});
  }
  function priceFor(model){
    if(!pricing||!Array.isArray(pricing.models)||!model)return null;
    const id=String(model).toLowerCase(); let best=null;
    pricing.models.forEach(m=>{ const p=String(m.match||'').toLowerCase(); if(p&&id.startsWith(p)&&(!best||p.length>best.match.length))best=m; });
    return best;
  }
  const n=v=>Number.isFinite(v)&&v>0?v:0;
  function tokensOf(e){ return {input:n(e.input_tokens),output:n(e.output_tokens),cacheRead:n(e.cache_read_tokens),cacheWrite:n(e.cache_write_5m_tokens)+n(e.cache_write_1h_tokens),w5:n(e.cache_write_5m_tokens),w1:n(e.cache_write_1h_tokens)}; }
  function totalTokens(e){ const t=tokensOf(e); return t.input+t.output+t.cacheRead+t.cacheWrite; }
  // USD for one token_usage event, or null when the model is not in the price table.
  function costOf(e){
    const m=priceFor(e.model); if(!m)return null;
    const rate=(String(e.detail||'').includes('tier=long_prompt')&&m.long_prompt)?Object.assign({},m,m.long_prompt):m;
    const t=tokensOf(e), unit=Number(pricing.unit_tokens)||1e6;
    return (t.input*rate.input+t.output*rate.output+t.cacheRead*rate.cache_read+t.w5*rate.cache_write_5m+t.w1*rate.cache_write_1h)/unit;
  }
  function fmtUsd(v){ if(v==null)return 'price unknown'; if(v===0)return '$0.00'; return v<0.01?'<$0.01':'$'+v.toFixed(2); }
  function sumCost(list){ let total=0, unknown=false; list.forEach(e=>{const c=costOf(e); if(c==null)unknown=true; else total+=c;}); return {total,unknown}; }

  // A "prompt" = one host turn (run_id minted at UserPromptSubmit). CodeCortex answers are matched to the turn
  // by time window, because the MCP server and the hook are separate processes with separate run ids.
  function promptTurns(events){
    const byRun=new Map();
    events.filter(e=>e.type==='token_usage').forEach(e=>{ if(!byRun.has(e.run_id))byRun.set(e.run_id,[]); byRun.get(e.run_id).push(e); });
    const answers=events.filter(e=>e.type==='context_returned');
    return [...byRun.entries()].map(([runId,usage])=>{
      const runEv=events.filter(e=>e.run_id===runId);
      const start=(runEv.find(e=>e.type==='run_started')||{}).ts_ms||Math.min(...usage.map(e=>e.ts_ms||0));
      const end=(runEv.slice().reverse().find(e=>e.type==='run_finished')||{}).ts_ms||Math.max(...usage.map(e=>e.ts_ms||0));
      const ccx=answers.filter(a=>(a.ts_ms||0)>=start&&(a.ts_ms||0)<=end);
      const c=sumCost(usage);
      return {runId,start,end,usage,models:[...new Set(usage.map(e=>e.model).filter(Boolean))],
        total:usage.reduce((s,e)=>s+totalTokens(e),0),input:usage.reduce((s,e)=>s+tokensOf(e).input,0),output:usage.reduce((s,e)=>s+tokensOf(e).output,0),
        cacheRead:usage.reduce((s,e)=>s+tokensOf(e).cacheRead,0),cacheWrite:usage.reduce((s,e)=>s+tokensOf(e).cacheWrite,0),
        requests:usage.reduce((s,e)=>s+n(e.requests),0),cost:c.total,costUnknown:c.unknown,ccxTokens:ccx.reduce((s,a)=>s+n(a.est_tokens),0)};
    }).sort((a,b)=>b.start-a.start);
  }

  function wasteAlerts(events){
    const alerts=[], byRun=new Map();
    events.forEach(e=>{ if(!e.run_id)return; if(!byRun.has(e.run_id))byRun.set(e.run_id,[]); byRun.get(e.run_id).push(e); });
    byRun.forEach((ev,runId)=>{
      const count=(type)=>{const m=new Map(); ev.filter(e=>e.type===type&&e.target).forEach(e=>m.set(e.target,(m.get(e.target)||0)+1)); return m;};
      count('file_read').forEach((c,t)=>{ if(c>=2)alerts.push({ts:Math.max(...ev.map(e=>e.ts_ms||0)),kind:'Re-read',text:`${t} was read ${c} times in one prompt`,tip:'Ask CodeCortex (code_search / code_graph) for the part you need instead of re-reading the whole file.'}); });
      count('search').forEach((c,t)=>{ if(c>=2)alerts.push({ts:Math.max(...ev.map(e=>e.ts_ms||0)),kind:'Repeated search',text:`"${t}" searched ${c} times in one prompt`,tip:'The earlier result is still in context; reuse it.'}); });
      ev.filter(e=>e.type==='tool_call'&&n(e.attempt_number)>=3).forEach(e=>alerts.push({ts:e.ts_ms||0,kind:'Retry loop',text:`${e.target} reached attempt ${e.attempt_number}`,tip:'Same tool failing repeatedly: a new approach is usually cheaper than another retry.'}));
    });
    events.filter(e=>e.type==='context_returned').forEach(e=>{
      if(String(e.detail||'').startsWith('repeat_suppressed'))alerts.push({ts:e.ts_ms||0,kind:'Repeat avoided',text:`${e.target}: identical answer not resent`,tip:'CodeCortex already sent this answer earlier in the session.',good:true});
      else if(n(e.est_tokens)>8000)alerts.push({ts:e.ts_ms||0,kind:'Large answer',text:`${e.target} returned ~${fmtInt(e.est_tokens)} tokens`,tip:'Pass a smaller budget_tokens, or narrow the task.'});
    });
    return alerts.sort((a,b)=>b.ts-a.ts).slice(0,40);
  }

  function card(label,value,source,sub){ return `<article><span>${esc(label)}</span><b>${esc(value)}</b><small class="metric-source">${esc(source)}${sub?' · '+esc(sub):''}</small></article>`; }

  function render(){
    if(typeof contextEvents!=='function')return;
    loadPricing();
    const ev=contextEvents();
    const usage=ev.filter(e=>e.type==='token_usage'), answers=ev.filter(e=>e.type==='context_returned');
    const turns=promptTurns(ev), cost=sumCost(usage);
    const hostTotal=usage.reduce((s,e)=>s+totalTokens(e),0);
    const ccxTotal=answers.reduce((s,a)=>s+n(a.est_tokens),0), avoided=answers.reduce((s,a)=>s+n(a.avoided_tokens_est),0);
    const repeatSaved=answers.filter(a=>String(a.detail||'').startsWith('repeat_suppressed')).reduce((s,a)=>{const m=/tokens_saved=(\d+)/.exec(a.detail);return s+(m?Number(m[1]):0);},0);
    const alerts=wasteAlerts(ev);

    // Overview tile: replace the input+output-only sum with the full host-reported total plus cost.
    const tile=$('tokens');
    if(tile){
      if(turns.length){ tile.textContent=fmtInt(turns[0].total); setMetricSource('tokensSource',`HOST-REPORTED · last prompt · ≈${fmtUsd(turns[0].costUnknown&&!turns[0].cost?null:turns[0].cost)}`); }
      else if(answers.length){ tile.textContent=fmtInt(ccxTotal); setMetricSource('tokensSource','DERIVED · CodeCortex answers only'); }
    }

    const view=$('usage'); if(!view)return;
    $('usageCards').innerHTML=[
      card('Conversation tokens',usage.length?fmtInt(hostTotal):'Not reported by host','HOST-REPORTED',usage.length?`${turns.length} prompt(s)`:'Claude Code only'),
      card('Estimated cost',usage.length?fmtUsd(cost.unknown&&!cost.total?null:cost.total)+(cost.unknown?' +':''):'—','ESTIMATE',pricing?`list price · ${pricing.retrieved||''}`:'price table loading'),
      card('CodeCortex answers',answers.length?fmtInt(ccxTotal):'—','DERIVED',`${answers.length} answer(s)`),
      card('File reads avoided',avoided?'~'+fmtInt(avoided):'—','DERIVED','size of the files those answers covered'),
      card('Repeats not resent',repeatSaved?'~'+fmtInt(repeatSaved):'0','DERIVED','identical answers skipped'),
      card('Waste alerts',String(alerts.filter(a=>!a.good).length),'DERIVED','re-reads · repeated searches · retry loops')
    ].join('');

    $('usageNote').innerHTML=usage.length?'':`<div class="truth-note">Conversation tokens and cost need <b>Claude Code</b> with CodeCortex hooks installed (<code>codecortex install --host claude</code>): it reports exact usage per prompt. The Claude Desktop app does not share usage with connected tools, so there CodeCortex can only show its <b>own</b> answers (estimated).</div>`;

    $('usagePrompts').innerHTML=turns.length?`<table class="usage-table"><thead><tr><th>Prompt</th><th>Model</th><th>Input</th><th>Output</th><th>Cache read</th><th>Cache write</th><th>Total</th><th>Est. cost</th><th>CodeCortex share</th></tr></thead><tbody>`+
      turns.slice(0,100).map(t=>`<tr><td>${esc(fmtTime(t.start))}<small>${esc(t.requests)} request(s)</small></td><td>${esc(t.models.join(', ')||'not reported')}</td><td>${fmtInt(t.input)}</td><td>${fmtInt(t.output)}</td><td>${fmtInt(t.cacheRead)}</td><td>${fmtInt(t.cacheWrite)}</td><td><b>${fmtInt(t.total)}</b></td><td>${esc(fmtUsd(t.costUnknown&&!t.cost?null:t.cost))}</td><td>${t.ccxTokens?'~'+fmtInt(t.ccxTokens)+(t.total?` (${Math.round(100*t.ccxTokens/Math.max(1,t.input+t.cacheRead+t.cacheWrite))}% of input)`:''):'—'}</td></tr>`).join('')+'</tbody></table>'
      :'<div class="empty">No host-reported usage yet.</div>';

    $('usageAnswers').innerHTML=answers.length?`<table class="usage-table"><thead><tr><th>Time</th><th>Tool</th><th>Answer (est.)</th><th>Files covered</th><th>Reading them whole (est.)</th><th>Note</th></tr></thead><tbody>`+
      answers.slice().reverse().slice(0,100).map(a=>{const rep=String(a.detail||'').startsWith('repeat_suppressed');return `<tr><td>${esc(fmtTime(a.ts_ms))}</td><td>${esc(a.target)}</td><td>~${fmtInt(n(a.est_tokens))}</td><td>${fmtInt(n(a.covered_files))}</td><td>${n(a.avoided_tokens_est)?'~'+fmtInt(a.avoided_tokens_est):'—'}</td><td>${rep?'repeat not resent':(a.success===false?'failed':'')}</td></tr>`;}).join('')+'</tbody></table>'
      :'<div class="empty">CodeCortex answers appear here once the agent uses a CodeCortex tool.</div>';

    $('usageAlerts').innerHTML=alerts.length?alerts.map(a=>`<div class="evidence-row"><span><b>${esc(a.kind)}</b> — ${esc(a.text)}<br><small class="muted">${esc(a.tip)}</small></span><strong class="${a.good?'ok':''}">${esc(fmtTime(a.ts))}</strong>${provenanceBadge('DERIVED')}</div>`).join(''):'<div class="empty">No waste detected.</div>';
  }

  window.CodeCortexTokens={render,costOf,priceFor};
})();
