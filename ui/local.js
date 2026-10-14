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
