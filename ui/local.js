/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Saurabh Verma
 *
 * Local fallbacks so no page is empty just because the optional external memory service is not installed:
 *   - Memories / Lessons / Actions read the project's built-in memory (.codecortex/memory.jsonl, written by
 *     memory_checkpoint) via /api/local-memory.
 *   - Lessons also include ones learned automatically from the telemetry: a CodeCortex tool that only
 *     succeeded after retries, or one that keeps failing.
 *   - Profile's Top files / Top concepts come from what CodeCortex answers covered and what the agent asked.
 * Loaded before app.js; app.js calls these when state.memory.available is false. Uses app.js globals at call time.
 */
(function(){
  const STOP=new Set(('the and for with that this from into what where when which while about there their have has '+
    'does should would could will your you are was were been being use using used find show code file files '+
    'function functions class make need want like just also then than them they these those how why who all any '+
    'can not but our out get set new add change changes test tests project repo repository codecortex claude').split(' '));
  const L={
    entries:[], loadedAt:0,
    async load(force){
      if(!force&&Date.now()-this.loadedAt<5000)return; this.loadedAt=Date.now();
      try{const r=await fetch('/api/local-memory',{cache:'no-store'}); const j=await r.json(); this.entries=Array.isArray(j.entries)?j.entries:[];}catch(e){}
    },
    autoLessons(events){
      const out=[];
      (events||[]).filter(e=>e.type==='tool_call'&&Number(e.attempt_number)>=2).forEach(e=>{
        const n=Number(e.attempt_number);
        if(e.success!==false)out.push({type:'lesson',source:'auto',ts_ms:e.ts_ms||0,title:`${e.target} worked on attempt ${n}`,
          content:`It failed ${n-1} time(s) first. Open Graph or Timeline for this run to see what changed between the failed and the working call.`});
        else if(n>=3)out.push({type:'lesson',source:'auto',ts_ms:e.ts_ms||0,title:`${e.target} keeps failing (${n} attempts)`,
          content:'Repeating the same call is unlikely to help. A different approach, or narrower input, usually does.'});
      });
      return out;
    },
    items(kind,events){
      const all=this.entries.slice();
      if(kind==='memories')return all;
      if(kind==='lessons')return all.filter(e=>['lesson','bug','pattern','workflow'].includes(String(e.type)))
                                    .concat(this.autoLessons(events));
      if(kind==='actions')return all.filter(e=>String(e.type)==='action');
      return [];
    },
    render(kind,listId,metaId,filter,events){
      let items=this.items(kind,events).slice().sort((a,b)=>(b.ts_ms||0)-(a.ts_ms||0));
      const q=String(filter||'').toLowerCase(); if(q)items=items.filter(m=>JSON.stringify(m).toLowerCase().includes(q));
      const meta=$(metaId); if(meta)meta.textContent=`${items.length} saved in this project`;
      const empty={
        memories:'Nothing saved yet. Ask Claude: <b>"Save to CodeCortex memory that …"</b>. It is kept in the project\'s <code>.codecortex</code> folder and recalled in later chats.',
        lessons:'Lessons appear when Claude saves a bug, pattern, workflow or lesson, and automatically when a CodeCortex tool only works after retries.',
        actions:'Ask Claude: <b>"Add a CodeCortex action: …"</b> to keep a follow-up here.'
      }[kind];
      const el=$(listId); if(!el)return;
      el.innerHTML=items.slice(0,300).map(m=>`<article class="memory-item"><div class="item-top"><strong>${esc(m.title||m.type||'Memory')}</strong><span class="badge">${esc(m.source==='auto'?'learned':(m.type||'memory'))}</span></div>${m.content?`<p>${esc(m.content)}</p>`:''}<small>${esc(m.project_root?String(m.project_root).split(/[\\/]/).filter(Boolean).pop():'')} ${esc(fmtTime(m.ts_ms))}</small></article>`).join('')
        ||`<div class="empty">${empty}</div>`;
    },
    profile(events){
      const files=new Map(), concepts=new Map(), bump=(m,k,n=1)=>{k=String(k||'').trim(); if(k)m.set(k,(m.get(k)||0)+n);};
      (events||[]).forEach(e=>{
        if(e.type==='context_returned'){
          (Array.isArray(e.files)?e.files:[]).forEach(f=>bump(files,f));
          const q=String(e.query||'');
          (q.match(/[A-Za-z_][A-Za-z0-9_]{3,}/g)||[]).forEach(w=>{const lw=w.toLowerCase(); if(!STOP.has(lw))bump(concepts,/[A-Z_]/.test(w.slice(1))?w:lw);});
        }
        if(e.type==='file_read'||e.type==='edit')bump(files,e.target);
        if(e.type==='search')bump(concepts,e.target);
      });
      const top=m=>[...m.entries()].sort((a,b)=>b[1]-a[1]).slice(0,10);
      return {files:top(files),concepts:top(concepts)};
    }
  };
  window.CodeCortexLocal=L;
})();

/* Simple view: a "Detailed | Simple" switch in the top bar. Detailed is the original wording; Simple swaps technical
 * labels for plain language, adds a one-line "what this page is" note to every page, and hides raw/engineering detail.
 * Implemented as a text overlay (originals are kept and restored), so it also covers content rendered later by app.js. */
(function(){
  const EXACT={
    'Developer Intelligence Console':'Your coding helper',
    'Overview':'Summary','Tokens & cost':'Usage & cost','Graph':'Map','Sessions':'Work sessions','Memories':'Saved notes',
    'Lessons':'Lessons learned','Actions':'To-do list','Crystals':'Finished work','Crystals / Run Snapshots':'Finished work',
    'Timeline':'Step by step','Audit':'Activity log','Activity':'Live activity','Profile':'Project summary','Health':'Is it working?',
    'Is CodeCortex set up? Check the list below.':'Is CodeCortex ready? Check the list below.',
    'Run signal':'What your AI is doing','Developer memory':'What it remembers','Productivity signal':'Time and effort saved',
    'Verification status':'Checks on the work','CodeCortex health':'Is everything working?','Runtime wiring':'How the parts connect',
    'Token usage by prompt':'Cost of each request','Answers sent to the agent':'Answers CodeCortex gave your AI','Waste alerts':'Wasted effort',
    'What each prompt cost, and what CodeCortex saved':'What each request cost, and what CodeCortex saved',
    'Tokens':'Amount of text used','Tool calls':'Actions taken','Repeated work':'Repeated steps','Duration':'Time taken',
    'Current tool':'Doing now','Tool failures':'Things that failed',
    'Conversation tokens':'Text used in the chat','CodeCortex answers':'Quick answers given','File reads avoided':'File reading avoided',
    'Repeats not resent':'Repeat answers skipped',
    'Prompt':'Request','Model':'AI model','Input':'Sent to AI','Output':'AI wrote','Cache read':'Reused (cheap)','Cache write':'Stored for reuse',
    'Est. cost':'Cost (est.)',"CodeCortex share":"CodeCortex's part",'Tool':'Helper used','Answer (est.)':'Answer size','Files covered':'Files summarised',
    'Reading them whole (est.)':'If read in full (est.)',
    'NOT AVAILABLE':'not available yet','OBSERVED':'seen directly','HOST-REPORTED':'reported by your AI app','DERIVED':'worked out','ESTIMATE':'estimate',
    'Developer console':'Dashboard','MCP':'Connection to your AI','Repository graph':'Code map','Telemetry':'Activity tracking',
    'Host run observation':'Seeing what your AI does','Token telemetry':'Usage counting','CodeCortex Memory':'Shared memory service (optional)',
    'Repository':'Project folder','Graph UI':'Map screen','Run snapshots':'Finished work','● ready':'● working','○ attention':'○ needs a look',
    'Coding host':'Your AI app','CodeCortex MCP':'CodeCortex connection','Code intelligence':'Understands your code','Developer UI':'This dashboard',
    'Memory':'Memory','Saved note':'Saved note','Work':"Your AI's work",
    'deterministic local code intelligence':'understands your code, on this computer',
    'conditional: host adapter/hooks not reporting':'not available in this app (normal for Claude Desktop)',
    'not reported by host':'not shared by your AI app','Not reported by host':'Not shared by your AI app',
    'graph · impact · tests · dependencies':'code map · what a change affects · tests',
    'project-scoped lessons and history':'notes and lessons for this project','observed actions only':'only what really happened',
    'health · run · graph · history · replay':'status · history · map · replay','single local intelligence surface':'one helper on your computer',
    'UI-only process':'dashboard only','awaiting events':'waiting for activity','neural graph runtime loaded':'map is loaded',
    'No run telemetry yet':'Nothing has run yet',
    'Telemetry will appear here as coding-agent hooks publish events.':'Activity will show up here once your AI starts working.',
    'CodeCortex Memory-backed memories, lessons, actions and crystals appear when connected.':'Saved notes and lessons will show up here.',
    'Observed productivity signals will appear when run telemetry is available.':'Time-saving details appear after your AI has done some work.',
    'Impact, test and proof evidence will appear here.':"Checks on your AI's work will appear here.",
    'Select a session to inspect its evidence.':'Pick a work session to see what happened.','Waiting for graph data…':'Loading the map…',
    'LEARNED ENGINEERING KNOWLEDGE':'LESSONS',
    'Your coding agent still codes. CodeCortex gives it the right engineering context.':'Your AI still writes the code. CodeCortex helps it find the right files faster.',
    'CodeCortex is a local-first developer intelligence layer that connects through MCP, understands your repository, remembers engineering history, observes supported runtime activity, and helps verify the result.':'CodeCortex runs on your computer. It keeps a map of your project, remembers what it learned, and shows you what your AI did. Nothing leaves your machine.',
    'Your coding tool':'Your AI app','Codex · Claude Code · Gemini · any compatible MCP client':'Claude, Codex, Gemini and others',
    'One local intelligence surface for tools, graph, memory and proof':'Runs on your computer: code map, memory and checks',
    'Better engineering outcome':'Better results','More context, less waste, clearer verification':'Less guessing, less wasted effort, clearer checks',
    'Maps symbols, callers, dependencies, impact radius, tests, architecture and hotspots before the model explores blindly.':'Builds a map of your code so the AI does not search blindly.',
    'Reuses project-scoped lessons, prior failures, decisions and historical engineering context instead of relearning everything.':'Keeps notes from past work so the AI does not start from zero each time.',
    'Captures CodeCortex activity and, where the host exposes it, sessions, tool calls, edits, tests and usage telemetry.':'Records what your AI did and how much it used, when your AI app shares it.',
    'Separates structural evidence from observed execution and keeps “agent finished” distinct from engineering proof.':'Shows what was actually checked, so “finished” really means checked.',
    'What happens during a development task':'What happens when you ask for something','You ask for work':'You ask your AI for something',
    'CodeCortex scopes the code':'CodeCortex finds the right code','Relevant symbols, dependencies, impact and likely tests.':'The relevant files, what they affect, and which tests matter.',
    'Prior knowledge is recalled':'It remembers past work','Previous failures, lessons and project memory when available.':'Earlier mistakes, lessons and notes for this project.',
    'Your coding agent executes':'Your AI does the work','The host remains responsible for reasoning, editing and tool use.':'Your AI app still decides and edits the code.',
    'CodeCortex observes':'CodeCortex watches','Only facts actually exposed by the host are marked as observed.':'It only reports what your AI app really shares.',
    'The result is checked':'The result is checked','Impact, tests and proof state remain visible to the developer.':'What changed and what was tested stays visible to you.',
    'What CodeCortex is trying to improve':"What it's for",'Right code, right impact, right evidence.':'Finds the right code.',
    'Less repeated discovery and unnecessary work.':'Less repeating and wasted effort.','Faster path to useful evidence and verification.':'Gets to useful answers sooner.',
    'Understand what happened, why and what remains unresolved.':'See what happened and what is still open.','Keep project scope, tests, memory and proof honest.':'Keeps work on track and honest.',
    'Where CodeCortex gets its knowledge':'Where its information comes from','Structural':'The code itself','Historical':'Past work','Observed':'Seen live','Host-reported':'Told to us by your AI app',
    'Code graph · callers · dependencies · impact · tests':'How files and functions connect, and which tests matter',
    'Runs · Git context · lessons · failures · decisions':'Earlier tasks, git history, lessons and mistakes',
    'MCP tool activity and host runtime events when exposed':'What your AI actually did, when the app shares it',
    'Session · model · tokens · cost only when the host actually reports them':'Usage and cost, only when your AI app shares them',
    'CodeCortex never invents telemetry a coding host does not expose.':'CodeCortex never makes up numbers.',
    'Designed to live beside your code':'Stays on your computer','Your laptop':'Your computer','Source code':'Your code','Git':'Git history',
    'CodeCortex graph':'Code map','Project memory':'Notes for this project','Developer dashboard':'This dashboard','↑ stdio MCP ↓':'↑ connects locally ↓',
    'Codex · Claude · Gemini · other MCP client':'Claude, Codex, Gemini or another AI app',
    'Recommended: local stdio MCP. Localhost HTTP is optional for clients that prefer it.':'Everything runs locally; nothing is sent elsewhere.',
    'Graph nodes should answer a developer question, not just show topology':'How to read the map',
    'See what the agent did, what the code means, and what changed':'See what your AI did and how the code fits together',
    'Run':'Task','Symbol':'Function','Failure':'Problem','Observed coding session or work unit':'One piece of work your AI did',
    'Function, class or code symbol':'A function or class in your code','Repository file involved in the work':'A file involved in the work',
    'Selected or observed verification':'A test that was chosen or run','Prior project knowledge reused':'A saved note that was reused',
    'Error or failed engineering step':'Something that went wrong','observed runtime evidence':'seen happening','structural repository relationship':'connected in the code',
    'run':'task','symbol':'function','memory':'note','error':'problem','project context loading…':'loading project…'
  };
  const WORDS=[[/\bMCP\b/g,'connection'],[/\btelemetry\b/gi,'activity data'],[/\bhost\b/g,'AI app'],[/\brepository\b/gi,'project'],
    [/\bsymbols\b/gi,'functions'],[/\bsymbol\b/gi,'function'],[/\btool calls?\b/gi,'actions'],[/\bagents?\b/gi,'AI'],[/\bsession\(s\)/g,'work session(s)']];
  const INTRO={
    start:'This page checks that CodeCortex is set up. Green ticks mean a step is done. Each amber item says what to do.',
    overview:'A quick summary of what your AI has been doing and what CodeCortex has helped with. Numbers fill in as you work.',
    'how-it-works':'A plain explanation of what CodeCortex is. Nothing to click or test here.',
    usage:'How much text your AI read and wrote, and what that cost. “Tokens” are small chunks of text (about ¾ of a word). Costs are estimates at list price.',
    graph:'A map of the work. Dots are tasks, files, functions and tests; lines show how they connect. Click a dot to see details.',
    sessions:'Each row is one piece of work your AI did. Click one to see which files and tests it touched.',
    memories:'Notes CodeCortex saved so your AI remembers them next time. Ask Claude: “Save to CodeCortex memory that …”.',
    lessons:'Mistakes and good ideas worth remembering for this project, saved by you or learned automatically.',
    actions:'Your follow-up to-do list. Ask Claude: “Add a CodeCortex action: …”.',
    crystals:'A short summary kept for each finished piece of work.',
    timeline:'The steps of one piece of work, in order. Pick the work from the menu.',
    audit:'A record of what CodeCortex was asked to do and when.',
    activity:'A live feed of what is happening right now. Leave it open while your AI works.',
    profile:'A summary of one project: the files and topics your AI asked about most.',
    replay:'Step through a past piece of work like a recording. Use ▶ to play.',
    health:'Is everything working? Green = working. Amber is often normal, for example some items cannot be seen in Claude Desktop.'
  };
  const SKIP='SCRIPT,STYLE,PRE,CODE,TEXTAREA,INPUT,CANVAS';
  const orig=new WeakMap(); let simple=false, busy=false, obs=null, queued=false;
  const ci={}; Object.keys(EXACT).forEach(k=>ci[k.toLowerCase()]=EXACT[k]);
  function plain(s){
    const t=s.trim(); if(!t)return s;
    let r=EXACT[t]; if(r===undefined)r=ci[t.toLowerCase()];
    if(r===undefined){ r=t; WORDS.forEach(([re,to])=>{r=r.replace(re,to);}); if(r===t)return s; }
    const lead=s.match(/^\s*/)[0], trail=s.match(/\s*$/)[0]; return lead+r+trail;
  }
  function skipped(n){ for(let e=n.parentNode;e&&e!==document.body;e=e.parentNode){ if(e.nodeType===1&&(SKIP.indexOf(e.tagName)>=0||e.classList.contains('no-simple')))return true; } return false; }
  function apply(root){
    busy=true;
    try{
      const w=document.createTreeWalker(root||document.body,NodeFilter.SHOW_TEXT,null); const nodes=[]; let n;
      while((n=w.nextNode()))nodes.push(n);
      nodes.forEach(t=>{
        if(skipped(t))return;
        const o=orig.get(t);
        if(simple){
          if(o&&t.data===o.applied)return;
          const src=(o&&t.data===o.applied)?o.src:t.data; const out=plain(src);
          if(out!==src){ orig.set(t,{src:src,applied:out}); t.data=out; } else if(o){ orig.delete(t); }
        } else if(o){ if(t.data===o.applied)t.data=o.src; orig.delete(t); }
      });
      document.querySelectorAll('.simple-intro').forEach(e=>e.remove());
      if(simple)document.querySelectorAll('main > section.view').forEach(s=>{
        const msg=INTRO[s.id]; if(!msg)return; const p=document.createElement('p'); p.className='simple-intro no-simple'; p.textContent=msg; s.insertBefore(p,s.firstChild);
      });
    } finally { if(obs)obs.takeRecords(); busy=false; }
  }
  function schedule(){ if(queued||busy||!simple)return; queued=true; requestAnimationFrame(()=>{queued=false; apply();}); }
  function setMode(m,save){
    simple=(m==='simple'); document.body.classList.toggle('simple',simple);
    document.querySelectorAll('.viewmode button').forEach(b=>b.setAttribute('aria-pressed',b.dataset.mode===(simple?'simple':'detailed')?'true':'false'));
    if(save){ try{localStorage.setItem('codecortex.viewmode',m);}catch(e){} }
    apply();
  }
  document.querySelectorAll('.viewmode button').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode,true)));
  obs=new MutationObserver(schedule); obs.observe(document.body,{childList:true,subtree:true,characterData:true});
  let saved='detailed'; try{saved=localStorage.getItem('codecortex.viewmode')||'detailed';}catch(e){}
  setMode(saved,false);
})();

/* Overview charts: sparklines in the metric tiles plus four panels (activity over time, what happened, tokens or savings,
 * latest activity). Drawn as inline SVG from the same events the rest of the dashboard uses; colours come from CSS classes. */
(function(){
  const n=v=>Number.isFinite(v)&&v>0?v:0;
  const fmt=v=>Math.round(v).toLocaleString();
  function spark(vals){
    if(vals.length<2||Math.max.apply(null,vals)<=0)return '';
    const W=200,H=40,mx=Math.max.apply(null,vals),st=W/(vals.length-1);
    const pts=vals.map((v,i)=>[i*st,H-4-(v/mx)*(H-10)]);
    const d=pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+' '+p[1].toFixed(1)).join(' '), last=pts[pts.length-1];
    return `<svg class="vz-spark" viewBox="0 0 ${W} ${H}" aria-hidden="true"><path class="vz-area" d="${d} L${W} ${H} L0 ${H} Z"/><path class="vz-line" d="${d}"/><circle class="vz-dot" cx="${last[0].toFixed(1)}" cy="${last[1].toFixed(1)}" r="3.5"/></svg>`;
  }
  const tot=e=>n(e.input_tokens)+n(e.output_tokens)+n(e.cache_read_tokens)+n(e.cache_write_5m_tokens)+n(e.cache_write_1h_tokens);
  function tiles(){
    const runs=runGroups().filter(g=>g.start).sort((a,b)=>(a.start.ts_ms||0)-(b.start.ts_ms||0)).slice(-14);
    const series={
      tokens:g=>g.events.filter(e=>e.type==='token_usage').reduce((s,e)=>s+tot(e),0),
      toolCalls:g=>g.events.filter(e=>e.type==='tool_call').length,
      toolFailures:g=>g.events.filter(e=>e.type==='tool_call'&&e.success===false).length,
      repeat:g=>{const r=runEvidence(g.events);return r.repeatedReads+r.repeatedSearches;}
    };
    Object.keys(series).forEach(id=>{
      const el=$(id), card=el&&el.closest('article'); if(!card)return;
      const old=card.querySelector('.vz-spark'); if(old)old.remove();
      card.insertAdjacentHTML('beforeend',spark(runs.map(series[id])));
    });
  }
  function areaChart(ev){
    const ts=ev.map(eventTs).filter(Boolean).sort((a,b)=>a-b);
    if(ts.length<2||ts[ts.length-1]===ts[0])return '<div class="empty">Activity over time appears once there are a few events.</div>';
    const B=24,t0=ts[0],t1=ts[ts.length-1],cnt=new Array(B).fill(0);
    ts.forEach(t=>{cnt[Math.min(B-1,Math.floor((t-t0)/(t1-t0)*B))]++;});
    const mx=Math.max.apply(null,cnt),W=320,H=120,L=26,R=8,T=10,Bt=100,st=(W-L-R)/(B-1);
    const pts=cnt.map((c,i)=>[L+i*st,Bt-(c/mx)*(Bt-T)]);
    const d=pts.map((p,i)=>(i?'L':'M')+p[0].toFixed(1)+' '+p[1].toFixed(1)).join(' '), last=pts[B-1];
    const tf=t=>new Date(t).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'});
    return `<svg class="vz-chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="Events over time, from ${tf(t0)} to ${tf(t1)}, peak ${mx}">
      <line class="vz-gl" x1="${L}" y1="${Bt}" x2="${W-R}" y2="${Bt}"/><line class="vz-gl" x1="${L}" y1="${T}" x2="${W-R}" y2="${T}"/><line class="vz-gl" x1="${L}" y1="${(Bt+T)/2}" x2="${W-R}" y2="${(Bt+T)/2}"/>
      <text class="vz-t" x="${L-6}" y="${T+3}" text-anchor="end">${mx}</text><text class="vz-t" x="${L-6}" y="${Bt+3}" text-anchor="end">0</text>
      <path class="vz-area" d="${d} L${W-R} ${Bt} L${L} ${Bt} Z"/><path class="vz-line" d="${d}"/><circle class="vz-dot" cx="${last[0].toFixed(1)}" cy="${last[1].toFixed(1)}" r="4"/>
      <text class="vz-t" x="${L}" y="${H-4}">${tf(t0)}</text><text class="vz-t" x="${W-R}" y="${H-4}" text-anchor="end">${tf(t1)}</text></svg>`;
  }
  function barChart(rows){
    rows=rows.filter(r=>r[1]>0); if(!rows.length)return '<div class="empty">Nothing recorded yet.</div>';
    const mx=Math.max.apply(null,rows.map(r=>r[1])),W=320,rh=28,H=rows.length*rh+4,x0=124,bw=W-x0-46;
    return `<svg class="vz-chart" viewBox="0 0 ${W} ${H}" role="img" aria-label="${esc(rows.map(r=>r[0]+' '+r[1]).join(', '))}">`+rows.map((r,i)=>{
      const y=i*rh+4,w=Math.max(4,r[1]/mx*bw);
      return `<text class="vz-t" x="0" y="${y+12}">${esc(r[0])}</text><rect class="vz-track" x="${x0}" y="${y+3}" width="${bw}" height="10" rx="5"/><rect class="vz-bar" x="${x0}" y="${y+3}" width="${w.toFixed(1)}" height="10" rx="5"/><text class="vz-v" x="${W}" y="${y+12}" text-anchor="end">${fmt(r[1])}</text>`;
    }).join('')+'</svg>';
  }
  function stacked(segs){
    const total=segs.reduce((s,x)=>s+x[1],0); if(!total)return '';
    const W=320; let x=0;
    const bars=segs.filter(s=>s[1]>0).map(s=>{const w=Math.max(2,s[1]/total*W),r=`<rect class="vz-seg${s[2]}" x="${x.toFixed(1)}" y="0" width="${Math.max(0,w-2).toFixed(1)}" height="16" rx="3"/>`; x+=w; return r;}).join('');
    const legend=segs.map(s=>`<div class="vz-lg"><span class="vz-sw vz-sw${s[2]}"></span><span>${esc(s[0])}</span><b>${fmt(s[1])}</b><small>${Math.round(100*s[1]/total)}%</small></div>`).join('');
    return `<svg class="vz-chart" viewBox="0 0 ${W} 16" role="img" aria-label="Share of tokens by type">${bars}</svg><div class="vz-legend">${legend}</div>`;
  }
  const LABEL={tool_call:'Tool call',file_read:'File read',search:'Search',edit:'Edit',test:'Test',memory_recall:'Memory recalled',context_returned:'CodeCortex answer',run_started:'Run started',run_finished:'Run finished',token_usage:'Usage reported',proof_check:'Proof check',test_selection:'Tests chosen',memory_write:'Memory saved'};
  const DOT={tool_call:0,context_returned:0,search:1,file_read:1,edit:2,test:3,proof_check:3,memory_recall:3,memory_write:3};
  function feed(ev){
    const items=ev.slice().sort((a,b)=>eventTs(b)-eventTs(a)).slice(0,7);
    if(!items.length)return '<div class="empty">Events appear here as your AI works.</div>';
    return '<ul class="vz-feed">'+items.map(e=>{const t=eventTs(e),lbl=String(eventLabel(e)),k=DOT[e.type]===undefined?4:DOT[e.type];
      return `<li><i class="vz-sw vz-sw${k}"></i><span class="vz-fd"><b>${esc(LABEL[e.type]||e.type)}</b> <span>${esc(lbl.length>46?lbl.slice(0,45)+'…':lbl)}</span></span><small>${t?new Date(t).toLocaleTimeString([], {hour:'2-digit',minute:'2-digit'}):''}</small></li>`;}).join('')+'</ul>';
  }
  function render(){
    if(typeof contextEvents!=='function'||typeof runGroups!=='function')return;
    const m=document.querySelector('#overview .metrics'); if(!m)return;
    let host=$('vizPanels'); if(!host){host=document.createElement('div'); host.id='vizPanels'; host.className='viz-grid'; m.insertAdjacentElement('afterend',host);}
    const ev=contextEvents(), usage=ev.filter(e=>e.type==='token_usage'), answers=ev.filter(e=>e.type==='context_returned');
    const count=t=>ev.filter(e=>e.type===t).length;
    let tokPanel;
    if(usage.length){
      const s=k=>usage.reduce((a,e)=>a+n(e[k]),0);
      tokPanel=`<article class="panel"><h2>Where the tokens went</h2><p class="muted">Reported by your AI app, by type</p>`+stacked([['Sent to AI',s('input_tokens'),0],['AI wrote',s('output_tokens'),1],['Reused (cheap)',s('cache_read_tokens'),2],['Stored for reuse',s('cache_write_5m_tokens')+s('cache_write_1h_tokens'),3]])+`</article>`;
    } else {
      const a=answers.reduce((x,e)=>x+n(e.est_tokens),0), v=answers.reduce((x,e)=>x+n(e.avoided_tokens_est),0);
      tokPanel=`<article class="panel"><h2>What CodeCortex saved</h2><p class="muted">Estimates. Your AI app does not share usage here.</p>`+(answers.length?barChart([['Answers CodeCortex gave',a],['Reading those files fully',v]]):'<div class="empty">Savings appear once your AI uses CodeCortex.</div>')+`</article>`;
    }
    host.innerHTML=`<article class="panel"><h2>Activity over time</h2><p class="muted">Events recorded in this project</p>${areaChart(ev)}</article>`+
      `<article class="panel"><h2>What happened</h2><p class="muted">Count of each kind of step</p>${barChart([['Tool calls',count('tool_call')],['Files read',count('file_read')],['Searches',count('search')],['Edits',count('edit')],['Tests',count('test')],['Memory recalls',count('memory_recall')],['CodeCortex answers',answers.length]])}</article>`+
      tokPanel+`<article class="panel"><h2>Latest activity</h2>${feed(ev)}</article>`;
    tiles();
  }
  window.CodeCortexViz={render};
})();

/* Graph page: 2D | 3D switch. The 3D view is a perspective-projected force layout on a plain canvas (no library): drag to rotate,
 * scroll to zoom, click a dot to select it (the side panel works exactly as in 2D). 2D stays the default and the fallback. */
(function(){
  const wrap=document.getElementById('graph-canvas-wrap'); if(!wrap)return;
  const reduce=matchMedia('(prefers-reduced-motion: reduce)').matches;
  let canvas=null,ctx=null,on=false,raf=0,spin=!reduce,yaw=.5,pitch=-.35,cam=1,w=0,h=0,ticks=0,drag=null,hits=[];
  const P=new Map();
  const G=()=>window.CodeCortexGraph&&window.CodeCortexGraph.data?window.CodeCortexGraph.data():null;
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
  let want='2d'; try{want=localStorage.getItem('codecortex.graph')==='3d'?'3d':'2d';}catch(e){}

  const bar=document.createElement('div'); bar.className='g3d-bar no-simple';
  bar.innerHTML='<div class="g3d-seg" role="group" aria-label="Graph view"><button type="button" data-g="2d" aria-pressed="true">2D</button><button type="button" data-g="3d" aria-pressed="false">3D</button></div><button type="button" class="g3d-btn" id="g3dSpin" hidden>Pause</button><button type="button" class="g3d-btn" id="g3dReset" hidden>Reset view</button><span class="g3d-hint" id="g3dHint" hidden>Drag to rotate · scroll to zoom</span>';
  wrap.appendChild(bar);
  const spinBtn=bar.querySelector('#g3dSpin'), resetBtn=bar.querySelector('#g3dReset'), hint=bar.querySelector('#g3dHint');

  function seed(nodes){
    const N=nodes.length||1,R=110+Math.sqrt(N)*16,ga=Math.PI*(3-Math.sqrt(5));
    nodes.forEach((n,i)=>{const y=1-(i/Math.max(1,N-1))*2,r=Math.sqrt(Math.max(0,1-y*y)),t=ga*i;
      P.set(n.id,{x:Math.cos(t)*r*R+(Math.random()-.5)*14,y:y*R+(Math.random()-.5)*14,z:Math.sin(t)*r*R+(Math.random()-.5)*14,vx:0,vy:0,vz:0});});
    ticks=0;
  }
  function sync(g){
    const ids=new Set(g.nodes.map(n=>n.id)); let changed=false;
    P.forEach((v,k)=>{if(!ids.has(k)){P.delete(k);changed=true;}});
    const missing=g.nodes.filter(n=>!P.has(n.id));
    if(missing.length){
      if(P.size===0)seed(g.nodes);
      else{missing.forEach(n=>{const e=g.edges.find(x=>(x.from===n.id&&P.has(x.to))||(x.to===n.id&&P.has(x.from)));
        const b=e?P.get(e.from===n.id?e.to:e.from):{x:0,y:0,z:0};
        P.set(n.id,{x:b.x+(Math.random()-.5)*60,y:b.y+(Math.random()-.5)*60,z:b.z+(Math.random()-.5)*60,vx:0,vy:0,vz:0});}); ticks=0;}
    } else if(changed)ticks=0;
  }
  function step(g){
    const vis=g.nodes.filter(g.visible), n=vis.length; if(n<2||ticks>420)return;
    const pts=vis.map(x=>P.get(x.id)), per=n>260?2:n>120?3:5;
    for(let it=0;it<per;it++){
      for(let i=0;i<n;i++){const a=pts[i]; for(let j=i+1;j<n;j++){const b=pts[j];
        let dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z,d2=dx*dx+dy*dy+dz*dz;
        if(d2<4){dx=Math.random()-.5;dy=Math.random()-.5;dz=Math.random()-.5;d2=1;}
        const d=Math.sqrt(d2),f=Math.min(2.2,2600/d2),fx=f*dx/d,fy=f*dy/d,fz=f*dz/d;
        a.vx+=fx;a.vy+=fy;a.vz+=fz;b.vx-=fx;b.vy-=fy;b.vz-=fz;}}
      g.edges.forEach(e=>{const a=P.get(e.from),b=P.get(e.to),na=g.byId.get(e.from),nb=g.byId.get(e.to);
        if(!a||!b||!na||!nb||!g.visible(na)||!g.visible(nb))return;
        const dx=b.x-a.x,dy=b.y-a.y,dz=b.z-a.z,d=Math.max(1,Math.hypot(dx,dy,dz)),f=(d-110)*.0035,fx=f*dx/d,fy=f*dy/d,fz=f*dz/d;
        a.vx+=fx;a.vy+=fy;a.vz+=fz;b.vx-=fx;b.vy-=fy;b.vz-=fz;});
      pts.forEach(p=>{p.vx-=p.x*.0006;p.vy-=p.y*.0006;p.vz-=p.z*.0006;p.vx*=.86;p.vy*=.86;p.vz*=.86;p.x+=p.vx;p.y+=p.vy;p.z+=p.vz;});
    }
    ticks++;
  }
  function project(p){
    const cy=Math.cos(yaw),sy=Math.sin(yaw),cp=Math.cos(pitch),sp=Math.sin(pitch);
    const x=p.x*cy+p.z*sy; let z=-p.x*sy+p.z*cy; const y=p.y*cp-z*sp; z=p.y*sp+z*cp;
    const s=640/Math.max(60,z+720*cam); return{x:w/2+x*s,y:h/2+y*s,s,z};
  }
  function draw(g){
    ctx.clearRect(0,0,w,h);
    const vis=g.nodes.filter(g.visible), pr=new Map(), deg=new Map();
    g.edges.forEach(e=>{deg.set(e.from,(deg.get(e.from)||0)+1);deg.set(e.to,(deg.get(e.to)||0)+1);});
    vis.forEach(n=>{const p=P.get(n.id); if(p)pr.set(n.id,project(p));});
    const sel=g.selected, near=new Set();
    if(sel){near.add(sel.id);g.edges.forEach(e=>{if(e.from===sel.id)near.add(e.to);if(e.to===sel.id)near.add(e.from);});}
    const depthA=z=>clamp(1.05-(z+260)/760,.22,1);
    g.edges.forEach(e=>{const a=pr.get(e.from),b=pr.get(e.to); if(!a||!b)return;
      const hot=!sel||e.from===sel.id||e.to===sel.id;
      ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.strokeStyle=g.edgeColor(e.type);
      ctx.globalAlpha=(hot?.62:.08)*depthA((a.z+b.z)/2);ctx.lineWidth=Math.max(.6,1.3*(a.s+b.s)/2*1.1);ctx.stroke();});
    ctx.globalAlpha=1;
    const order=vis.filter(n=>pr.has(n.id)).sort((a,b)=>pr.get(b.id).z-pr.get(a.id).z);
    hits=[];
    order.forEach(n=>{
      const q=pr.get(n.id),r=Math.max(3,(11+Math.min(6,Math.sqrt(deg.get(n.id)||0)*1.6))*q.s*.95),focus=!sel||near.has(n.id),col=g.colorFor(n.type);
      ctx.save();ctx.globalAlpha=(focus?1:.14)*depthA(q.z);
      if(sel===n){ctx.shadowColor='#3cc9ba';ctx.shadowBlur=24;}
      ctx.beginPath();ctx.arc(q.x,q.y,r,0,Math.PI*2);ctx.fillStyle=col;ctx.fill();ctx.shadowBlur=0;
      const gr=ctx.createRadialGradient(q.x-r*.4,q.y-r*.4,r*.05,q.x,q.y,r);
      gr.addColorStop(0,'rgba(255,255,255,.75)');gr.addColorStop(.45,'rgba(255,255,255,0)');gr.addColorStop(1,'rgba(0,0,0,.45)');
      ctx.beginPath();ctx.arc(q.x,q.y,r,0,Math.PI*2);ctx.fillStyle=gr;ctx.fill();
      if(sel===n){ctx.beginPath();ctx.arc(q.x,q.y,r+4,0,Math.PI*2);ctx.lineWidth=2;ctx.strokeStyle='#3cc9ba';ctx.stroke();}
      ctx.restore();
      hits.push({n:n,x:q.x,y:q.y,r:Math.max(10,r+3)});
      if((sel&&focus)||(order.length<=40&&q.z<80)||sel===n){
        ctx.save();ctx.globalAlpha=focus?.95:.2;ctx.font='11px ui-sans-serif,system-ui,sans-serif';ctx.fillStyle='#e8f4ff';ctx.shadowColor='rgba(0,0,0,.7)';ctx.shadowBlur=4;
        ctx.fillText(g.short(n.label,30),q.x+r+6,q.y+4);ctx.restore();
      }
    });
    hits.reverse();
  }
  function frame(){
    raf=0; if(!on)return;
    const g=G();
    if(g&&ctx&&canvas.offsetParent!==null&&w>10){ sync(g); step(g); if(spin&&!drag)yaw+=.0028; draw(g); }
    raf=requestAnimationFrame(frame);
  }
  function resize(){
    if(!canvas)return; const r=wrap.getBoundingClientRect(); if(r.width<10||r.height<10)return;
    const dpr=Math.min(window.devicePixelRatio||1,2); w=r.width;h=r.height;
    canvas.width=Math.floor(w*dpr);canvas.height=Math.floor(h*dpr);canvas.style.width=w+'px';canvas.style.height=h+'px';ctx.setTransform(dpr,0,0,dpr,0,0);
  }
  function hitAt(x,y){ for(let i=0;i<hits.length;i++){const q=hits[i]; if(Math.hypot(x-q.x,y-q.y)<=q.r)return q.n;} return null; }
  function build(){
    if(canvas)return true;
    canvas=document.createElement('canvas'); canvas.id='graph-canvas-3d'; canvas.setAttribute('role','img');
    canvas.setAttribute('aria-label','3D graph. Drag to rotate, scroll to zoom. Switch to 2D for a keyboard-friendly view.');
    ctx=canvas.getContext&&canvas.getContext('2d'); if(!ctx){canvas=null;return false;}
    wrap.insertBefore(canvas,wrap.firstChild);
    canvas.addEventListener('mousedown',e=>{const r=canvas.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top,g=G(),n=hitAt(x,y);
      if(n&&g){g.select(n);}else drag={x:e.clientX,y:e.clientY,moved:false};});
    window.addEventListener('mousemove',e=>{ if(!on||!canvas)return;
      if(drag){const dx=e.clientX-drag.x,dy=e.clientY-drag.y; if(Math.abs(dx)+Math.abs(dy)>2)drag.moved=true; yaw+=dx*.006;pitch=clamp(pitch+dy*.006,-1.4,1.4);drag.x=e.clientX;drag.y=e.clientY;return;}
      const r=canvas.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top,g=G(); if(!g)return;
      g.showTooltip(x>=0&&y>=0&&x<=r.width&&y<=r.height?hitAt(x,y):null,x,y); });
    window.addEventListener('mouseup',()=>{ if(drag&&!drag.moved){const g=G(); if(g)g.select(null);} drag=null; });
    canvas.addEventListener('mouseleave',()=>{const g=G(); if(g)g.showTooltip(null);});
    canvas.addEventListener('wheel',e=>{e.preventDefault();cam=clamp(cam*(e.deltaY<0?.92:1.09),.35,2.2);},{passive:false});
    resize(); return true;
  }
  function reset3d(){ yaw=.5;pitch=-.35;cam=1; const g=G(); if(g){P.clear();seed(g.nodes);} }
  function setMode(m,save){
    if(m==='3d'&&!build())m='2d';
    on=(m==='3d'); wrap.classList.toggle('g3d-on',on);
    bar.querySelectorAll('[data-g]').forEach(b=>b.setAttribute('aria-pressed',b.dataset.g===m?'true':'false'));
    spinBtn.hidden=resetBtn.hidden=hint.hidden=!on; spinBtn.textContent=spin?'Pause':'Spin';
    if(save){want=m; try{localStorage.setItem('codecortex.graph',m);}catch(e){}}
    if(on){resize(); if(!raf)raf=requestAnimationFrame(frame);}
    else{ if(ctx&&canvas)ctx.clearRect(0,0,w,h); drag=null; const g=G(); if(g)g.showTooltip(null); if(window.CodeCortexGraph)window.CodeCortexGraph.activate(); }
  }
  bar.querySelectorAll('[data-g]').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.g,true)));
  spinBtn.addEventListener('click',()=>{spin=!spin;spinBtn.textContent=spin?'Pause':'Spin';});
  resetBtn.addEventListener('click',reset3d);
  const cbs=wrap.querySelectorAll('.graph-controls button');
  if(cbs.length===3){
    cbs.forEach(b=>b.removeAttribute('onclick'));
    cbs[0].addEventListener('click',()=>{on?(cam=clamp(cam*.85,.35,2.2)):window.CodeCortexGraph.zoom(1);});
    cbs[1].addEventListener('click',()=>{on?(cam=clamp(cam*1.18,.35,2.2)):window.CodeCortexGraph.zoom(-1);});
    cbs[2].addEventListener('click',()=>{on?reset3d():window.CodeCortexGraph.recenter();});
  }
  if(window.ResizeObserver)new ResizeObserver(()=>{ resize(); if(want==='3d'&&!on&&wrap.clientWidth>10)setMode('3d',false); }).observe(wrap);
})();

/* Collapsible sidebar. Wide screens: icon rail <-> full menu (remembered). Phones: slide-in drawer. */
(function(){
  const b=document.body, tg=document.getElementById('navToggle'), scrim=document.getElementById('navScrim');
  if(!tg)return;
  const small=()=>matchMedia('(max-width:900px)').matches;
  try{ if(localStorage.getItem('codecortex.nav')==='collapsed')b.classList.add('nav-collapsed'); }catch(e){}
  document.querySelectorAll('#nav button').forEach(x=>{ const l=x.querySelector('.lb'); if(l)x.title=l.textContent.trim(); });
  function sync(){ tg.setAttribute('aria-expanded',String(small()?b.classList.contains('nav-open'):!b.classList.contains('nav-collapsed'))); }
  tg.addEventListener('click',()=>{
    if(small())b.classList.toggle('nav-open');
    else{ b.classList.toggle('nav-collapsed'); try{localStorage.setItem('codecortex.nav',b.classList.contains('nav-collapsed')?'collapsed':'open');}catch(e){} }
    sync();
  });
  const nav=document.getElementById('nav'), more=document.getElementById('navMoreToggle');
  if(more&&nav){
    const setMore=o=>{nav.classList.toggle('more-open',o);more.setAttribute('aria-expanded',String(o));try{localStorage.setItem('codecortex.navmore',o?'1':'0');}catch(e){}};
    let o=false; try{o=localStorage.getItem('codecortex.navmore')==='1';}catch(e){}
    nav.classList.toggle('more-open',o); more.setAttribute('aria-expanded',String(o));
    more.addEventListener('click',()=>setMore(!nav.classList.contains('more-open')));
    more.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){e.preventDefault();setMore(!nav.classList.contains('more-open'));} });
  }
  if(scrim)scrim.addEventListener('click',()=>{b.classList.remove('nav-open');sync();});
  document.getElementById('nav').addEventListener('click',e=>{ if(small()&&e.target.closest('button')){b.classList.remove('nav-open');sync();} });
  document.addEventListener('keydown',e=>{ if(e.key==='Escape'&&b.classList.contains('nav-open')){b.classList.remove('nav-open');sync();} });
  addEventListener('resize',sync); sync();
})();

/* Light / dark theme. Follows the system setting until the user picks one with the ◐ button; the choice is remembered. */
(function(){
  const root=document.documentElement;
  try{ const t=localStorage.getItem('codecortex.theme'); if(t==='light'||t==='dark')root.dataset.theme=t; }catch(e){}
  const btn=document.getElementById('themeToggle'); if(!btn)return;
  btn.addEventListener('click',()=>{
    const dark=root.dataset.theme?root.dataset.theme==='dark':matchMedia('(prefers-color-scheme: dark)').matches;
    const next=dark?'light':'dark'; root.dataset.theme=next;
    try{localStorage.setItem('codecortex.theme',next);}catch(e){}
  });
})();

/* "How to read this page" ⓘ button on every page. Plain wording in both Detailed and Simple view. */
(function(){
  const H={
    start:['Checks that CodeCortex is set up and gives you prompts to try.','Green tick = done. Amber = a step is left; the line says what to do.','Open your AI app and paste one of the prompts shown here.','Items stay amber until your AI has used CodeCortex at least once.'],
    overview:['A snapshot of the latest piece of work: usage, actions taken, repeats, time and failures.','Bigger numbers mean more activity. “Repeated steps” and “Things that failed” are the ones to watch.','Ask your AI a question about your code, then press Refresh.','It says “Nothing has run yet” until your AI has used CodeCortex in the selected project.'],
    'how-it-works':['A plain explanation of what CodeCortex does and where its information comes from.','Read top to bottom. The flow at the top is the whole idea in one line.','Nothing to test on this page.','It is a static page, so it is never empty.'],
    usage:['What each request cost, and how much CodeCortex saved your AI from reading.','The table has one row per request. “Reused (cheap)” text costs far less than new text. Savings are estimates.','Ask the same question twice: “Repeat answers skipped” goes up.','Exact per-request numbers only come from Claude Code. Claude Desktop does not share them, so you see CodeCortex’s part only.'],
    graph:['A map of the work: tasks, files, functions, tests and saved notes, joined by lines.','Solid lines are things that happened. Dashed lines are links in the code. Click a dot for details.','Ask a few questions, then search for a function name to see what connects to it.','It stays empty until there is activity in the selected project. Try “All projects”.'],
    sessions:['Every piece of work your AI did, one row each.','Click a row to see the files, tests and tools it used.','Ask two different questions in separate chats: you get two rows.','Empty until your AI has used CodeCortex.'],
    memories:['Notes saved so your AI remembers them in later chats.','Newest first. Use the search box to filter.','Ask your AI: “Save to CodeCortex memory that login is handled in auth.py.”','Empty until something is saved. Press Ctrl+F5 after saving.'],
    lessons:['Mistakes and good ideas worth remembering, saved by you or learned automatically.','A “learned” tag means CodeCortex noticed it, for example a call that only worked after retries.','Ask your AI: “Save a CodeCortex lesson: …”.','Empty until a lesson is saved or a retry happens.'],
    actions:['Your follow-up to-do list.','Each card is one to-do, newest first.','Ask your AI: “Add a CodeCortex action: write a test for login.”','Empty until you add one.'],
    crystals:['A short summary kept for each finished piece of work.','Each card says what was done, which files changed and which tests ran.','Finish one task and start another.','A summary is only made when a piece of work finishes.'],
    timeline:['The steps of one piece of work, in order.','Pick the work from the menu. Read from the top to see what happened first.','Pick your latest session in the menu.','Empty until at least one piece of work has been recorded.'],
    audit:['A record of what CodeCortex was asked to do, and when.','Newest first. Useful when you need to show what happened.','Ask any question to your AI, then refresh.','Empty until CodeCortex has been used.'],
    activity:['A live feed of what is happening right now.','New lines appear at the top as your AI works.','Keep this open and ask your AI something.','Empty until your AI uses CodeCortex.'],
    profile:['A summary of one project: the files and topics your AI asked about most.','Use the menu to switch project. “Top files” and “Top concepts” show where the attention went.','Ask three or four questions about one project, then open this page.','Empty for a project until you have asked about it.'],
    replay:['Step through a past piece of work like a recording.','Use ▶ to play, ◀ and ▶| to go step by step.','Choose your latest session, then press ▶.','Empty until a piece of work has been recorded.'],
    health:['Whether each part of CodeCortex is working.','Green = working. Amber = needs a look, but is often normal. For example, Claude Desktop does not share usage data.','The Dashboard, Connection and Code map items should be green.','Amber on “Usage counting” or “Seeing what your AI does” is normal in Claude Desktop.']
  };
  const LABELS=['What this shows','How to read it','Try it','If it looks empty'];
  function build(id){
    const sec=document.getElementById(id), h=H[id]; if(!sec||!h||sec.querySelector(':scope > .info-wrap'))return;
    const wrap=document.createElement('div'); wrap.className='info-wrap no-simple';
    const btn=document.createElement('button'); btn.type='button'; btn.className='info-btn'; btn.setAttribute('aria-expanded','false');
    const ic=document.createElement('i'); ic.textContent='i'; ic.setAttribute('aria-hidden','true'); btn.appendChild(ic); btn.appendChild(document.createTextNode('How to read this page'));
    const panel=document.createElement('div'); panel.className='info-panel'; panel.hidden=true; panel.id='info-'+id; btn.setAttribute('aria-controls',panel.id);
    h.forEach((txt,i)=>{ const d=document.createElement('div'); const t=document.createElement('h4'); t.textContent=LABELS[i]; const p=document.createElement('p'); p.textContent=txt; d.appendChild(t); d.appendChild(p); panel.appendChild(d); });
    btn.addEventListener('click',()=>{ const open=panel.hidden; panel.hidden=!open; btn.setAttribute('aria-expanded',open?'true':'false'); });
    wrap.appendChild(btn); wrap.appendChild(panel); sec.insertBefore(wrap,sec.firstChild);
  }
  Object.keys(H).forEach(build);
  document.addEventListener('keydown',e=>{ if(e.key!=='Escape')return; document.querySelectorAll('.info-panel:not([hidden])').forEach(p=>{p.hidden=true; const b=document.querySelector('[aria-controls="'+p.id+'"]'); if(b)b.setAttribute('aria-expanded','false');}); });
})();
