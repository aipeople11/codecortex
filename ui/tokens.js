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
    // Claude Code's own cost for the request (OpenTelemetry) beats any estimate from our price table.
    if(Number.isFinite(e.cost_usd_micros)&&e.cost_usd_micros>=0)return e.cost_usd_micros/1e6;
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

  function card(label,value,source,sub){ return `<article><span>${esc(label)}</span><b${String(value).length>12?' class="long"':''}>${esc(value)}</b><small class="metric-source">${esc(source)}${sub?' · '+esc(sub):''}</small></article>`; }

  // Once exact OpenTelemetry usage is arriving, the older transcript-based usage for the same period would
  // count the same requests twice, so it is left out from the first OpenTelemetry event onwards.
  function withoutDoubleCount(list){
    const isOtel=e=>String(e.detail||'')==='claude_code_otel';
    const first=list.filter(e=>e.type==='token_usage'&&isOtel(e)).reduce((m,e)=>Math.min(m,e.ts_ms||Infinity),Infinity);
    if(!Number.isFinite(first))return list;
    return list.filter(e=>!(e.type==='token_usage'&&String(e.detail||'').startsWith('claude_code_transcript')&&(e.ts_ms||0)>=first));
  }

  // ---- "Exact usage" card: one-click turn on/off for the local OpenTelemetry receiver ----
  let otel=null, otelSig='', otelBusy=false;
  function otelRefresh(){
    fetch('/api/otel',{cache:'no-store'}).then(r=>r.ok?r.json():{unavailable:true}).then(j=>{ otel=j; drawOtel(); }).catch(()=>{ otel={unavailable:true}; drawOtel(); });
  }
  function otelAct(kind){
    if(otelBusy)return; otelBusy=true; drawOtel(true);
    fetch('/api/otel/'+kind,{method:'POST',headers:{'X-CodeCortex':'1'}}).then(r=>r.json()).then(j=>{ otel=j; }).catch(()=>{ otel=Object.assign({},otel||{},{ok:false,message:'Could not reach CodeCortex.'}); })
      .finally(()=>{ otelBusy=false; drawOtel(); });
  }
  function drawOtel(force){
    const host=$('otelCard'); if(!host||!otel)return;
    const sig=JSON.stringify(otel)+otelBusy; if(sig===otelSig&&!force)return; otelSig=sig;
    const o=otel;
    let status, body, button='';
    if(o.unavailable){ status='Not in this version'; body='This CodeCortex program is older than this dashboard, so the exact-usage switch cannot work yet. Install the latest CodeCortex build, then reopen this page.'; }
    else if(o.managed){ status='Managed by your organisation'; body='Claude Code’s telemetry settings are controlled by IT, so CodeCortex will not change them.'; }
    else if(o.events>0){ status='Receiving'; body=`${fmtInt(o.events)} request(s) recorded with Claude Code’s own token counts and cost. Last: ${esc(fmtTime(o.last_event_ms))}.`; button='off'; }
    else if(o.enabled&&o.configured){ status='On, waiting for Claude Code'; body='Reopen Claude Code once, then send a message. Usage will appear here.'; button='off'; }
    else if(o.conflict){ status='Needs attention'; body=esc(o.conflict); }
    else { status='Off'; body='Get the exact tokens and cost that Claude Code itself reports for every request, instead of estimates. Everything stays on this computer: nothing is sent anywhere, and your prompts and code are never stored.'; button='on'; }
    host.innerHTML=`<div class="truth-note otel-card"><b>Exact usage from Claude Code</b> <span class="muted">· ${esc(status)}</span><br>${body}${o.message?`<br><small class="muted">${esc(o.message)}</small>`:''}`+
      (button==='on'?`<br><button type="button" class="primary" data-otel="enable"${otelBusy?' disabled':''}>Turn on</button>`:'')+
      (button==='off'?`<br><button type="button" data-otel="disable"${otelBusy?' disabled':''}>Turn off</button>`:'')+`</div>`;
  }
  document.addEventListener('click',ev=>{ const b=ev.target.closest&&ev.target.closest('[data-otel]'); if(b)otelAct(b.getAttribute('data-otel')); });
  setInterval(otelRefresh,4000); otelRefresh();

  // ---- Overview headline: what CodeCortex saved, with vs without, in plain words ----
  let svSig='';
  function drawSavings(answers){
    const host=$('overview'); if(!host)return;
    let box=$('savingsHero');
    if(!box){ box=document.createElement('div'); box.id='savingsHero'; const first=host.firstElementChild; host.insertBefore(box,first&&first.nextSibling); }
    let withTok=0, without=0, repeats=0, first=Infinity;
    answers.forEach(a=>{
      const est=n(a.est_tokens), avoid=n(a.avoided_tokens_est), m=/tokens_saved=(\d+)/.exec(String(a.detail||''));
      withTok+=est; without+=Math.max(avoid,est);
      if(m){ const s=Number(m[1])||0; repeats+=s; without+=s; }
      if(a.ts_ms)first=Math.min(first,a.ts_ms);
    });
    const saved=Math.max(0,without-withTok), pct=without?Math.round(100*saved/without):0;
    const p=priceFor('claude-sonnet-5-5')||priceFor('claude-sonnet-5'), unit=pricing&&Number(pricing.unit_tokens)||1e6;
    const usd=p&&saved?saved*p.input/unit:null;
    const sig=[answers.length,withTok,without,repeats,!!p].join('|'); if(sig===svSig)return; svSig=sig;
    if(!answers.length){
      box.innerHTML=`<article class="panel sv-hero"><div class="sv-head"><div class="eyebrow">WHAT CODECORTEX DOES FOR YOU</div><h2>It gives your AI only the part of your code it needs, instead of whole files.</h2><p class="muted">That means fewer tokens used, lower cost and faster answers. This page shows how much it saved. <b>No data yet.</b> Ask your AI something about your code and the numbers appear here within seconds.</p></div>
        <div class="sv-try"><span>Try asking:</span><code>Where is login handled in this project?</code><code>What would break if I change the main function?</code><code>Find where errors are logged.</code></div></article>`;
      return;
    }
    const w=Math.max(4,Math.round(100*withTok/Math.max(1,without)));
    box.innerHTML=`<article class="panel sv-hero"><div class="sv-head"><div class="eyebrow">WHAT CODECORTEX SAVED YOU</div><h2>${saved?`About ${esc(fmtInt(saved))} tokens saved (${pct}% less)`:'Answers so far were already small'}${usd!=null?` · ≈ ${esc(fmtUsd(usd))}`:''}</h2>
      <p class="muted">Instead of reading whole files, your AI asked CodeCortex and got just the relevant code. Based on ${esc(fmtInt(answers.length))} answer(s)${Number.isFinite(first)?` since ${esc(fmtTime(first))}`:''}. These are estimates from file sizes, not billing figures.</p></div>
      <div class="sv-bars">
        <div class="sv-row"><span class="sv-label">Without CodeCortex<small>reading the whole files</small></span><div class="sv-track"><i class="sv-bar sv-without" data-w="100"></i></div><b>~${esc(fmtInt(without))}</b></div>
        <div class="sv-row"><span class="sv-label">With CodeCortex<small>only what was needed</small></span><div class="sv-track"><i class="sv-bar sv-with" data-w="${w}"></i></div><b>~${esc(fmtInt(withTok))}</b></div>
      </div>${repeats?`<p class="muted sv-note">Includes ~${esc(fmtInt(repeats))} tokens not re-sent because the same answer was already given.</p>`:''}</article>`;
    // The dashboard's content policy forbids inline styles, so bar widths are applied from script.
    box.querySelectorAll('[data-w]').forEach(el=>{ el.style.width=el.getAttribute('data-w')+'%'; });
  }

  // ---- Token categories: input vs output (and the cache parts of input), tokens, share and cost ----
  let splitSig='';
  function drawSplit(usage,answers){
    const view=$('usage'), cards=$('usageCards'); if(!view||!cards)return;
    let box=$('tokenSplit');
    if(!box){ box=document.createElement('div'); box.id='tokenSplit'; cards.insertAdjacentElement('afterend',box); }
    const cats=[
      {k:'input',name:'Input: new',what:'Fresh text sent to the AI: your question, file contents, tool results.',cls:'vz1',t:0,c:0},
      {k:'cacheRead',name:'Input: reused (cache read)',what:'Earlier context the AI reused at a big discount.',cls:'vz0',t:0,c:0},
      {k:'cacheWrite',name:'Input: stored (cache write)',what:'Context saved so the next request can reuse it.',cls:'vz3',t:0,c:0},
      {k:'output',name:'Output',what:'What the AI wrote back: answers, code, tool calls. Costs the most per token.',cls:'vz2',t:0,c:0}];
    let priced=true;
    usage.forEach(e=>{
      const t=tokensOf(e), m=priceFor(e.model);
      const rate=m?((String(e.detail||'').includes('tier=long_prompt')&&m.long_prompt)?Object.assign({},m,m.long_prompt):m):null, u=pricing&&Number(pricing.unit_tokens)||1e6;
      cats[0].t+=t.input; cats[1].t+=t.cacheRead; cats[2].t+=t.cacheWrite; cats[3].t+=t.output;
      if(rate){ cats[0].c+=t.input*rate.input/u; cats[1].c+=t.cacheRead*rate.cache_read/u; cats[2].c+=t.w5*rate.cache_write_5m/u+t.w1*rate.cache_write_1h/u; cats[3].c+=t.output*rate.output/u; } else priced=false;
    });
    const total=cats.reduce((s,c)=>s+c.t,0);
    const sig=JSON.stringify([cats.map(c=>c.t),answers.length,priced,!!pricing]); if(sig===splitSig)return; splitSig=sig;
    if(!total){
      const est=answers.reduce((s,a)=>s+n(a.est_tokens),0);
      box.innerHTML=`<article class="panel"><h2>Input vs output</h2><p class="muted"><b>Input</b> is what your AI reads. <b>Output</b> is what it writes back.${est?` CodeCortex has sent about <b>~${esc(fmtInt(est))} input tokens</b> (estimate) in ${esc(fmtInt(answers.length))} answer(s).`:''} The exact input/output split for the whole conversation needs <b>Claude Code</b>; Claude Desktop does not share it.</p></article>`;
      return;
    }
    box.innerHTML=`<article class="panel"><h2>Input vs output</h2><p class="muted">Where the tokens went. Input is what the AI reads, output is what it writes.</p>
      <div class="sv-stack">${cats.filter(c=>c.t).map(c=>`<i class="sv-seg ${c.cls}" data-w="${Math.max(1,Math.round(100*c.t/total))}" title="${esc(c.name)}"></i>`).join('')}</div>
      <div class="tw"><table class="usage-table"><thead><tr><th>Category</th><th>What it is</th><th>Tokens</th><th>Share</th><th>Cost</th></tr></thead><tbody>`+
      cats.map(c=>`<tr><td><i class="sv-dot ${c.cls}"></i> ${esc(c.name)}</td><td>${esc(c.what)}</td><td>${esc(fmtInt(c.t))}</td><td>${Math.round(100*c.t/total)}%</td><td>${priced?esc(fmtUsd(c.c)):'—'}</td></tr>`).join('')+
      `</tbody></table></div></article>`;
    box.querySelectorAll('[data-w]').forEach(el=>{ el.style.width=el.getAttribute('data-w')+'%'; });
  }

  function render(){
    if(typeof contextEvents!=='function')return;
    loadPricing();
    const ev=withoutDoubleCount(contextEvents());
    drawSavings(ev.filter(e=>e.type==='context_returned'));
    if(!$('otelCard')&&$('usageCards')){ const d=document.createElement('div'); d.id='otelCard'; $('usageCards').parentNode.insertBefore(d,$('usageCards')); drawOtel(true); }
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
    drawSplit(usage,answers);
    $('usageCards').innerHTML=[
      card('Conversation tokens',usage.length?fmtInt(hostTotal):'—',usage.length?'HOST-REPORTED':'NEEDS CLAUDE CODE',usage.length?`${turns.length} prompt(s)`:'Claude Desktop does not share usage; Claude Code does'),
      usage.some(e=>Number.isFinite(e.cost_usd_micros)&&e.cost_usd_micros>=0)
        ? card('Reported cost',fmtUsd(cost.total)+(cost.unknown?' +':''),'HOST-REPORTED','as Claude Code reports it; older requests use list price')
        : card('Estimated cost',usage.length?fmtUsd(cost.unknown&&!cost.total?null:cost.total)+(cost.unknown?' +':''):'—','ESTIMATE',pricing?`list price · ${pricing.retrieved||''}`:'price table loading'),
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
