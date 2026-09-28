/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Saurabh Verma
 */
const $ = id => document.getElementById(id);
const state = {
  events: [], health: {}, telemetryInfo: {}, productivity: {}, runIndex: {}, memory: {}, activeView: 'overview',
  memoryLoadedAt: 0, selectedSession: '', activeProject: '', activeRoot: '', filters: { memories:'', lessons:'', actions:'', crystals:'' }, replay: { runId: '', source: 'local', cursor: 0, playing: false, timer: null, remoteTimeline: null }
};

function esc(v){
  const d=document.createElement('div'); d.textContent=v==null?'':String(v); return d.innerHTML;
}
function fmtInt(v){ return Number.isFinite(v) ? Math.round(v).toLocaleString() : '—'; }
function fmtTime(ts){ if(!ts) return 'Timestamp unavailable'; const d=new Date(ts); return Number.isNaN(d.getTime())?'Timestamp unavailable':d.toLocaleString(); }
function fmtDuration(ms){ if(!Number.isFinite(ms)) return '—'; if(ms<1000)return `${ms}ms`; if(ms<60000)return `${(ms/1000).toFixed(1)}s`; return `${Math.floor(ms/60000)}m ${Math.round((ms%60000)/1000)}s`; }

function provenanceLabel(source, fallback='OBSERVED'){
  const v=String(source||'').toLowerCase();
  if(v==='host_reported')return 'HOST-REPORTED';
  if(v==='provider_reported')return 'PROVIDER-REPORTED';
  if(v==='structural')return 'STRUCTURAL';
  if(v==='derived')return 'DERIVED';
  if(v==='observed')return 'OBSERVED';
  return fallback;
}
function provenanceClass(label){ return String(label||'not available').toLowerCase().replaceAll('_','-').replaceAll(' ','-'); }
function provenanceBadge(label){ label=label||'NOT AVAILABLE'; return `<span class="provenance-badge ${provenanceClass(label)}">${esc(label)}</span>`; }
function setMetricSource(id,label){ const el=$(id); if(el)el.textContent=label||'NOT AVAILABLE'; }
function runEvidence(events){
  const ev=events||[], tools=ev.filter(e=>e.type==='tool_call'), reads=ev.filter(e=>e.type==='file_read'), searches=ev.filter(e=>e.type==='search');
  const repeatedCount=list=>{const counts=new Map();list.forEach(e=>{const k=String(e.target||'').trim();if(k)counts.set(k,(counts.get(k)||0)+1)});return [...counts.values()].reduce((n,c)=>n+Math.max(0,c-1),0)};
  const edits=ev.filter(e=>e.type==='edit'), tests=ev.filter(e=>e.type==='test'), selected=ev.filter(e=>e.type==='test_selection'), proofs=ev.filter(e=>e.type==='proof_check'), recalls=ev.filter(e=>e.type==='memory_recall'), tokenEvents=ev.filter(e=>e.type==='token_usage');
  const inputTokens=tokenEvents.reduce((n,e)=>n+(Number.isFinite(e.input_tokens)?e.input_tokens:0),0), outputTokens=tokenEvents.reduce((n,e)=>n+(Number.isFinite(e.output_tokens)?e.output_tokens:0),0);
  const filesChanged=new Set(edits.map(e=>e.target).filter(Boolean));
  return {tools,reads,searches,edits,tests,selected,proofs,recalls,tokenEvents,inputTokens,outputTokens,filesChanged,repeatedReads:repeatedCount(reads),repeatedSearches:repeatedCount(searches),failures:tools.filter(e=>e.success===false).length};
}
function evidenceRow(label,value,source,state=''){
  return `<div class="evidence-row"><span>${esc(label)}</span><strong class="${esc(state)}">${esc(value)}</strong>${provenanceBadge(source)}</div>`;
}
function latestRun(events){
  const starts=events.filter(e=>e.type==='run_started');
  if(!starts.length)return null;
  const start=starts[starts.length-1];
  return {start,events:events.filter(e=>e.run_id===start.run_id)};
}
function runGroups(){
  const m=new Map();
  contextEvents().forEach(e=>{ if(!e.run_id)return; if(!m.has(e.run_id))m.set(e.run_id,[]); m.get(e.run_id).push(e); });
  return [...m.entries()].map(([id,events])=>({id,events,start:events.find(e=>e.type==='run_started'),finish:events.find(e=>e.type==='run_finished')}))
    .sort((a,b)=>((b.start?.ts_ms||0)-(a.start?.ts_ms||0)));
}
function eventTs(e){ return e.ts_ms || e.timestamp || e.ts || 0; }
function eventLabel(e){ return e.target || e.detail || e.type || 'event'; }
function arr(obj,...keys){ for(const k of keys){ if(Array.isArray(obj?.[k]))return obj[k]; } return []; }
function projectContexts(){
  const m=new Map();
  if(state.health?.project_id)m.set(state.health.project_id,{id:state.health.project_id,name:state.health.project_name||state.health.project_id,root:state.health.root||'',repo:state.health.repo_id||''});
  state.events.forEach(e=>{if(!e.project_id)return;const prev=m.get(e.project_id)||{};m.set(e.project_id,{id:e.project_id,name:e.project_name||prev.name||e.project_id,root:e.workspace_root||prev.root||'',repo:e.repo_id||prev.repo||''});});
  return [...m.values()];
}
function contextEvents(){
  const project=state.activeProject||state.health?.project_id||'';
  return state.events.filter(e=>!project||!e.project_id||e.project_id===project);
}
function scopedMemory(items){
  const project=state.activeProject||state.health?.project_id||''; if(!project)return items;
  const ctx=projectContexts().find(x=>x.id===project)||{};
  const keys=new Set([project,ctx.id,ctx.name,ctx.root,ctx.repo].filter(Boolean).map(String));
  return items.filter(x=>keys.has(String(x?.project||x?.project_id||x?.repo_id||x?.workspace_root||'')));
}
function renderContext(){
  const contexts=projectContexts(); if(!state.activeProject)state.activeProject=state.health?.project_id||contexts[0]?.id||'';
  const active=contexts.find(x=>x.id===state.activeProject)||contexts[0]||{id:'',name:'Current workspace',root:state.health?.root||'',repo:state.health?.repo_id||''};
  state.activeRoot=active.root||state.health?.root||'';
  const sel=$('projectContext'); if(sel){sel.innerHTML=contexts.map(x=>`<option value="${esc(x.id)}" ${x.id===state.activeProject?'selected':''}>${esc(x.name)}</option>`).join('')||'<option value="">Current workspace</option>';sel.value=state.activeProject;}
  if($('repoContext'))$('repoContext').textContent=active.repo||active.root||'repository —';
  const ev=contextEvents(), session=ev.slice().reverse().find(e=>e.session_id)?.session_id||state.health?.session_id||'';
  if($('sessionContext'))$('sessionContext').textContent=session?`session ${session}`:'session —';
  if($('graphContext'))$('graphContext').textContent=`Project: ${active.name||'Current workspace'}${active.root?' · Repository: '+active.root:''}`;
}
function memAvailable(){ return state.health.memory?.ok===true; }
function sessionKey(kind,id){ return `${kind}:${id}`; }
function localRunSnapshot(r){
  const ev=r.events||[]; const tools=ev.filter(e=>e.type==='tool_call'); const files=[...new Set(ev.filter(e=>['file_read','file_write','file_edit','edit'].includes(e.type)).map(e=>e.target).filter(Boolean))];
  const tests=ev.filter(e=>e.type==='test'||/test/i.test(String(e.target||e.detail||''))); const tokenEvents=ev.filter(e=>e.type==='token_usage');
  const tokens=tokenEvents.reduce((n,e)=>n+(e.input_tokens||0)+(e.output_tokens||0),0);
  return {id:r.id,title:`Run ${r.id}`,narrative:r.finish?(r.finish.success?'Completed CodeCortex run':'Failed CodeCortex run'):'Active CodeCortex run',project:r.start?.project_name||r.start?.project_id||state.activeProject||state.health.root||'',createdAt:r.finish?.ts_ms||r.start?.ts_ms||0,keyOutcomes:tools.map(e=>e.target).filter(Boolean),filesAffected:files,lessons:[],tests:tests.length,tokens,success:r.finish?.success};
}
function localCrystals(){ return runGroups().filter(r=>r.finish).map(localRunSnapshot); }
function memorySessionItems(){ return scopedMemory(arr(state.memory.sessions,'sessions').filter(s=>s&&s.id)); }

async function getJson(url){
  try{
    const r=await fetch(url,{cache:'no-store'});
    let body;
    try{ body=await r.json(); }
    catch(e){ return {ok:false,available:false,status:r.status,error:`Invalid JSON from ${url}: ${e}`}; }
    if(!r.ok) return Object.assign({ok:false,available:false,status:r.status}, body&&typeof body==='object'?body:{error:`HTTP ${r.status}`});
    return body;
  }
  catch(e){ return {ok:false,available:false,error:String(e)}; }
}

async function loadMemoryBundle(force=false){
  const now=Date.now();
  if(!force && now-state.memoryLoadedAt<10000)return;
  state.memoryLoadedAt=now;
  if(!memAvailable()){
    state.memory={available:false,error:state.health.memory?.error||'CodeCortex Memory not connected'};
    return;
  }
  const endpoints={
    sessions:'/api/memory/sessions', memories:'/api/memory/memories?latest=true&limit=500', lessons:'/api/memory/lessons',
    actions:'/api/memory/actions', frontier:'/api/memory/frontier', crystals:'/api/memory/crystals', audit:'/api/memory/audit?limit=100', replaySessions:'/api/memory/replay/sessions'
  };
  const keys=Object.keys(endpoints);
  const vals=await Promise.all(keys.map(k=>getJson(endpoints[k])));
  state.memory={available:true}; keys.forEach((k,i)=>state.memory[k]=vals[i]);
  const recent=memorySessionItems().slice().sort((a,b)=>String(b.startedAt||'').localeCompare(String(a.startedAt||''))).slice(0,5);
  const obs=await Promise.all(recent.map(x=>getJson('/api/memory/observations?sessionId='+encodeURIComponent(x.id))));
  state.memory.recentObservations=obs.flatMap(x=>arr(x,'observations'));
}

function renderOverview(){
  const run=latestRun(contextEvents());
  $('health').textContent=state.health.ok?'● service online':'● service attention';
  ['tokensSource','toolCallsSource','repeatSource','durationSource','currentToolSource','toolFailuresSource'].forEach(id=>setMetricSource(id,'NOT AVAILABLE'));
  if(!run){
    $('runId').textContent='No run telemetry yet';
    $('tokens').textContent='Not reported by host'; $('toolCalls').textContent='—'; $('repeat').textContent='—'; $('duration').textContent='—'; $('currentTool').textContent='—'; $('toolFailures').textContent='—';
    $('productivitySignal').innerHTML='<div class="empty">Observed productivity signals will appear when run telemetry is available.</div>';
    $('verificationStatus').innerHTML='<div class="empty">Impact, test and proof evidence will appear here.</div>';
  } else {
    $('runId').textContent=run.start.run_id;
    const x=runEvidence(run.events), tokens=x.inputTokens+x.outputTokens, finish=run.events.find(e=>e.type==='run_finished');
    const tokenSource=x.tokenEvents.length?provenanceLabel(x.tokenEvents.slice().reverse().find(e=>e.source)?.source,'PROVIDER-REPORTED'):'NOT AVAILABLE';
    $('tokens').textContent=x.tokenEvents.length?fmtInt(tokens):'Not reported by host'; setMetricSource('tokensSource',tokenSource);
    $('toolCalls').textContent=String(x.tools.length); setMetricSource('toolCallsSource',x.tools.length?'OBSERVED':'NOT AVAILABLE');
    $('currentTool').textContent=x.tools.length?(x.tools[x.tools.length-1].target||'Observed tool'):(finish?'Run finished':'No tool observed'); setMetricSource('currentToolSource',x.tools.length?'OBSERVED':'NOT AVAILABLE');
    $('toolFailures').textContent=String(x.failures); setMetricSource('toolFailuresSource',x.tools.length?'OBSERVED':'NOT AVAILABLE');
    const repeated=x.repeatedReads+x.repeatedSearches; $('repeat').textContent=x.reads.length||x.searches.length?String(repeated):'No repeat evidence'; setMetricSource('repeatSource',x.reads.length||x.searches.length?'DERIVED':'NOT AVAILABLE');
    $('duration').textContent=finish?.duration_ms!=null?fmtDuration(finish.duration_ms):'running'; setMetricSource('durationSource',finish?.duration_ms!=null?'OBSERVED':'NOT AVAILABLE');
    const hostObserved=run.events.some(e=>e.source==='host_reported'), mcpObserved=x.tools.length>0;
    const observation=hostObserved?'Run observed':(mcpObserved?'MCP observed':'Code intelligence only');
    const host=run.events.slice().reverse().find(e=>e.host_name)?.host_name||'Not reported';
    const model=run.events.slice().reverse().find(e=>e.model)?.model||'Not reported';
    const usage=x.tokenEvents.length?'Reported':'Not reported by host';
    $('runSignal').innerHTML=`<div class="row"><span>Observation coverage</span><strong>${esc(observation)}</strong></div><div class="row"><span>Host</span><strong>${esc(host)}</strong></div><div class="row"><span>Model</span><strong>${esc(model)}</strong></div><div class="row"><span>Events captured</span><strong>${run.events.length}</strong></div><div class="row"><span>Token telemetry</span><strong>${esc(usage)}</strong></div><div class="row"><span>Status</span><strong>${finish?(finish.success?'finished':'failed'):'running'}</strong></div>`;
    $('productivitySignal').innerHTML=[
      evidenceRow('Observed tool calls',x.tools.length,x.tools.length?'OBSERVED':'NOT AVAILABLE'),
      evidenceRow('Repeated reads',x.repeatedReads,x.reads.length?'DERIVED':'NOT AVAILABLE'),
      evidenceRow('Repeated searches',x.repeatedSearches,x.searches.length?'DERIVED':'NOT AVAILABLE'),
      evidenceRow('Failed/retried actions',x.failures,x.tools.length?'OBSERVED':'NOT AVAILABLE'),
      evidenceRow('Memory recalls',x.recalls.length,x.recalls.length?'OBSERVED':'NOT AVAILABLE'),
      evidenceRow('Files changed',x.filesChanged.size,x.edits.length?'OBSERVED':'NOT AVAILABLE'),
      evidenceRow('Tests executed',x.tests.length,x.tests.length?'HOST-REPORTED':'NOT AVAILABLE')
    ].join('');
    const impactChecked=run.events.some(e=>e.type==='tool_call'&&/impact/i.test(String(e.target||'')));
    const selectedCount=x.selected.length, executedCount=x.tests.length, proof=x.proofs.slice(-1)[0];
    $('verificationStatus').innerHTML=[
      evidenceRow('Impact analysis',impactChecked?'complete':'not observed',impactChecked?'OBSERVED':'NOT AVAILABLE',impactChecked?'verification-good':'verification-missing'),
      evidenceRow('Tests selected',selectedCount?`${selectedCount} observed`:'not observed',selectedCount?'STRUCTURAL':'NOT AVAILABLE',selectedCount?'verification-good':'verification-missing'),
      evidenceRow('Tests executed',executedCount?`${executedCount} observed`:'not observed',executedCount?'HOST-REPORTED':'NOT AVAILABLE',executedCount?'verification-good':'verification-missing'),
      evidenceRow('Proof check',proof?(proof.success===false?'failed':'passed'):'not observed',proof?'OBSERVED':'NOT AVAILABLE',proof?(proof.success===false?'verification-warn':'verification-good'):'verification-missing'),
      evidenceRow('Build status','not reported','NOT AVAILABLE','verification-missing')
    ].join('');
  }
  $('sessionCount').textContent=fmtInt(runGroups().length);
  const memories=scopedMemory(arr(state.memory.memories,'memories'));
  $('memoryCount').textContent=state.memory.available?fmtInt(memories.length):'—';
  const lessons=scopedMemory(arr(state.memory.lessons,'lessons')), actions=scopedMemory(arr(state.memory.actions,'actions')), crystals=scopedMemory(arr(state.memory.crystals,'crystals'));
  $('memorySummary').innerHTML=state.memory.available
    ? `<div class="row"><span>Lessons</span><strong>${lessons.length}</strong></div><div class="row"><span>Actions</span><strong>${actions.length}</strong></div><div class="row"><span>Crystals</span><strong>${crystals.length}</strong></div>`
    : `<div class="empty">${esc(state.memory.error||'CodeCortex Memory not connected')}</div>`;
}

function renderSessions(){
  const local=runGroups(); const remote=memorySessionItems();
  $('sessionsMeta').textContent=`${local.length} CodeCortex · ${remote.length} memory`;
  const localHtml=local.slice(0,100).map(r=>{
    const tools=r.events.filter(e=>e.type==='tool_call').length; const key=sessionKey('local',r.id);
    return `<button class="session-card ${state.selectedSession===key?'selected':''}" data-session="${esc(key)}"><div><strong>${esc(r.id)}</strong><small>CodeCortex run</small></div><div class="session-facts"><span>${tools} tools</span><span>${r.events.length} events</span><span>${r.finish?(r.finish.success?'passed':'failed'):'running'}</span></div></button>`;
  }).join('');
  const remoteHtml=remote.slice(0,100).map(x=>{const key=sessionKey('memory',x.id);return `<button class="session-card memory-card ${state.selectedSession===key?'selected':''}" data-session="${esc(key)}"><div><strong>${esc(x.project||x.id||'memory session')}</strong><small>${esc(x.id||'')}</small></div><div class="session-facts"><span>${esc(x.status||'')}</span><span>${x.observationCount||0} observations</span><span>${esc(fmtTime(x.startedAt))}</span></div></button>`}).join('');
  $('sessionsList').innerHTML=(localHtml+remoteHtml)||'<div class="empty">No sessions captured.</div>';
  document.querySelectorAll('[data-session]').forEach(b=>b.onclick=()=>selectSession(b.dataset.session));
  renderSessionDetail();
}
async function selectSession(key){ state.selectedSession=key; renderSessions(); const [kind,id]=String(key).split(':',2); if(kind==='memory'&&id){state.memory.selectedObservations=await getJson('/api/memory/observations?sessionId='+encodeURIComponent(id)); renderSessionDetail();} }
function renderSessionDetail(){
  const host=$('sessionDetail'); if(!host)return; const key=state.selectedSession;
  if(!key){host.innerHTML='<div class="empty">Select a session to inspect its evidence.</div>';return;}
  const cut=key.indexOf(':'); const kind=key.slice(0,cut), id=key.slice(cut+1);
  if(kind==='local'){
    const r=runGroups().find(x=>x.id===id); if(!r){host.innerHTML='<div class="empty">Session unavailable.</div>';return;}
    const snap=localRunSnapshot(r), x=runEvidence(r.events), toolCounts={}; r.events.filter(e=>e.type==='tool_call').forEach(e=>toolCounts[e.target]=(toolCounts[e.target]||0)+1);
    const lastHost=r.events.slice().reverse().find(e=>e.host_name), lastModel=r.events.slice().reverse().find(e=>e.model), tokenSource=x.tokenEvents.length?provenanceLabel(x.tokenEvents.slice().reverse().find(e=>e.source)?.source,'PROVIDER-REPORTED'):'NOT AVAILABLE';
    host.innerHTML=`<h3>${esc(id)}</h3><div class="mini-grid"><span>${r.events.length} events</span><span>${snap.filesAffected.length} files</span><span>${snap.tests} test events</span><span>${x.tokenEvents.length?fmtInt(x.inputTokens+x.outputTokens):'—'} tokens</span></div><div class="session-evidence"><h4>Runtime evidence</h4>${evidenceRow('Host',lastHost?.host_name||'Not reported',lastHost?'HOST-REPORTED':'NOT AVAILABLE')}${evidenceRow('Model',lastModel?.model||'Not reported',lastModel?'HOST-REPORTED':'NOT AVAILABLE')}${evidenceRow('Input tokens',x.tokenEvents.length?fmtInt(x.inputTokens):'Not reported',tokenSource)}${evidenceRow('Output tokens',x.tokenEvents.length?fmtInt(x.outputTokens):'Not reported',tokenSource)}${evidenceRow('Memory recalls',x.recalls.length,x.recalls.length?'OBSERVED':'NOT AVAILABLE')}${evidenceRow('Repeated reads',x.repeatedReads,x.reads.length?'DERIVED':'NOT AVAILABLE')}${evidenceRow('Repeated searches',x.repeatedSearches,x.searches.length?'DERIVED':'NOT AVAILABLE')}${evidenceRow('Failed actions',x.failures,x.tools.length?'OBSERVED':'NOT AVAILABLE')}</div><h4>Top tools</h4>${Object.entries(toolCounts).sort((a,b)=>b[1]-a[1]).slice(0,8).map(([k,v])=>`<div class="row"><span>${esc(k)}</span><strong>${v}</strong></div>`).join('')||'<div class="empty">No tools.</div>'}`;
  } else {
    const x=memorySessionItems().find(s=>String(s.id)===id); const obs=arr(state.memory.selectedObservations,'observations');
    host.innerHTML=`<h3>${esc(x?.project||id)}</h3><div class="mini-grid"><span>${obs.length} observations</span><span>${esc(x?.status||'')}</span></div>${obs.slice(0,12).map(o=>`<div class="row"><span>${esc(o.type||o.toolName||o.hookType||'observation')}</span><strong>${esc(o.title||o.filePath||'')}</strong></div>`).join('')||'<div class="empty">Select again to load observations.</div>'}`;
  }
}

function renderMemories(){
  let items=scopedMemory(arr(state.memory.memories,'memories')); const q=state.filters.memories.toLowerCase(); if(q)items=items.filter(m=>JSON.stringify(m).toLowerCase().includes(q)); $('memoriesMeta').textContent=state.memory.available?`${items.length} memories`:'offline';
  $('memoriesList').innerHTML=items.slice(0,300).map(m=>`<article class="memory-item"><div class="item-top"><strong>${esc(m.title||m.type||'Memory')}</strong><span class="badge">${esc(m.type||'memory')}</span></div><p>${esc(m.content||m.fact||m.text||m.narrative||'')}</p><small>${esc(m.project||'')} ${esc(fmtTime(m.updatedAt||m.createdAt))}</small></article>`).join('') || `<div class="empty">${state.memory.available?'No memories yet.':esc(state.memory.error||'CodeCortex Memory unavailable.')}</div>`;
}
function renderLessons(){
  let items=scopedMemory(arr(state.memory.lessons,'lessons')); const q=state.filters.lessons.toLowerCase(); if(q)items=items.filter(m=>JSON.stringify(m).toLowerCase().includes(q)); $('lessonsMeta').textContent=state.memory.available?`${items.length} lessons`:'offline';
  $('lessonsList').innerHTML=items.slice(0,300).map(l=>`<article class="memory-item"><div class="item-top"><strong>${esc(l.title||l.type||'Lesson')}</strong><span class="badge">${l.confidence!=null?Math.round(Number(l.confidence)*100)+'%':'lesson'}</span></div><p>${esc(l.content||l.lesson||l.description||'')}</p><small>${esc(l.project||'')} ${esc(fmtTime(l.updatedAt||l.createdAt))}</small></article>`).join('') || `<div class="empty">${state.memory.available?'No lessons yet.':esc(state.memory.error||'CodeCortex Memory unavailable.')}</div>`;
}
function renderActions(){
  let items=scopedMemory(arr(state.memory.actions,'actions')); const q=state.filters.actions.toLowerCase(); if(q)items=items.filter(m=>JSON.stringify(m).toLowerCase().includes(q)); const frontier=new Set(arr(state.memory.frontier,'actions','frontier').map(x=>String(x.id||x)));
  $('actionsMeta').textContent=state.memory.available?`${items.length} actions`:'offline';
  $('actionsList').innerHTML=items.slice(0,300).map(a=>`<article class="memory-item ${frontier.has(String(a.id))?'frontier':''}"><div class="item-top"><strong>${esc(a.title||'Action')}</strong><span class="badge status-${esc(a.status||'pending')}">${esc(a.status||'pending')}</span></div><p>${esc(a.description||'')}</p><small>priority ${esc(a.priority??'—')} ${Array.isArray(a.tags)?'· '+esc(a.tags.join(', ')):''}</small></article>`).join('') || `<div class="empty">${state.memory.available?'No actions yet.':esc(state.memory.error||'CodeCortex Memory unavailable.')}</div>`;
}
function renderCrystals(){
  const remote=scopedMemory(arr(state.memory.crystals,'crystals')); const local=localCrystals(); let items=[...local,...remote]; const q=state.filters.crystals.toLowerCase(); if(q)items=items.filter(c=>JSON.stringify(c).toLowerCase().includes(q));
  $('crystalsMeta').textContent=`${local.length} local · ${remote.length} memory`;
  $('crystalsList').innerHTML=items.slice(0,250).map(c=>`<article class="memory-item crystal"><div class="item-top"><strong>${esc(c.narrative||c.title||'Completed work')}</strong><span class="badge">${c.id&&String(c.id).startsWith('run')?'run':'snapshot'}</span></div><div class="mini-grid"><span>${(c.keyOutcomes||[]).length} tools</span><span>${(c.filesAffected||[]).length} files</span><span>${c.tests||0} tests</span><span>${c.tokens||'—'} tokens</span></div><small>${esc(c.project||'')} ${esc(fmtTime(c.createdAt))}</small></article>`).join('') || `<div class="empty">No completed run snapshots yet.</div>`;
}


function populateRunSelect(id, selected){
  const el=$(id); if(!el)return; const groups=runGroups();
  const local=groups.map(r=>`<option value="local:${esc(r.id)}" ${selected===`local:${r.id}`?'selected':''}>CodeCortex · ${esc(r.id)}</option>`).join('');
  const remote=memorySessionItems().map(s=>`<option value="memory:${esc(s.id)}" ${selected===`memory:${s.id}`?'selected':''}>Memory · ${esc(s.project||s.id)}</option>`).join('');
  el.innerHTML=(local+remote)||'<option value="">No sessions</option>';
}
async function renderTimeline(){
  const groups=runGroups(); let selected=$('timelineRun').value|| (groups[0]?`local:${groups[0].id}`:(memorySessionItems()[0]?`memory:${memorySessionItems()[0].id}`:'')); populateRunSelect('timelineRun',selected);
  if(!selected){$('timelineList').innerHTML='<div class="empty">No timeline available.</div>';return;}
  const cut=selected.indexOf(':'); const kind=selected.slice(0,cut), id=selected.slice(cut+1); let events=[];
  if(kind==='local'){ events=groups.find(r=>r.id===id)?.events||[]; }
  else { const res=await getJson('/api/memory/observations?sessionId='+encodeURIComponent(id)); events=arr(res,'observations').map(o=>({type:o.type||o.hookType||o.toolName||'observation',target:o.title||o.filePath||o.narrative||'',ts_ms:new Date(o.timestamp||o.createdAt||0).getTime(),detail:o.narrative||o.subtitle||''})); }
  const sorted=events.slice().sort((a,b)=>eventTs(a)-eventTs(b)); const base=sorted.length?eventTs(sorted[0]):0;
  $('timelineList').innerHTML=sorted.map(e=>`<div class="timeline-item"><span class="timeline-dot type-${esc(e.type)}"></span><div><strong>${esc(e.type)}</strong><p>${esc(eventLabel(e))}</p><small>+${base?Math.max(0,eventTs(e)-base):0} ms · ${esc(fmtTime(eventTs(e)))}</small></div></div>`).join('')||'<div class="empty">No events for this session.</div>';
}

function renderAudit(){
  const remote=arr(state.memory.audit,'entries'); const local=contextEvents().filter(e=>['tool_call','run_started','run_finished','token_usage'].includes(e.type)).slice(-200).reverse();
  $('auditMeta').textContent=`${local.length} local · ${remote.length} memory`;
  $('auditList').innerHTML=(local.map(e=>`<div class="audit-row"><span class="badge">${esc(e.type)}</span><strong>${esc(eventLabel(e))}</strong><small>${esc(fmtTime(eventTs(e)))}</small></div>`).join('')+remote.map(a=>`<div class="audit-row"><span class="badge">${esc(a.operation||a.type||'audit')}</span><strong>${esc(a.functionId||a.target||a.summary||'memory event')}</strong><small>${esc(fmtTime(a.timestamp||a.createdAt))}</small></div>`).join(''))||'<div class="empty">No audit evidence.</div>';
}
function renderActivity(){
  const remote=(state.memory.recentObservations||[]).map(o=>({type:o.type||o.hookType||o.toolName||'memory_observation',target:o.title||o.filePath||o.narrative||'',ts_ms:new Date(o.timestamp||o.createdAt||0).getTime()})); const events=[...contextEvents(),...remote].sort((a,b)=>eventTs(b)-eventTs(a)).slice(0,160);
  $('activityList').innerHTML=events.map(e=>`<div class="activity-item"><span class="activity-icon">${e.type==='tool_call'?'⚙':e.type==='run_finished'?'✓':e.type==='token_usage'?'#':'•'}</span><div><strong>${esc(e.type)}</strong><p>${esc(eventLabel(e))}</p><small>${esc(fmtTime(eventTs(e)))}</small></div></div>`).join('')||'<div class="empty">No activity yet.</div>';
}

async function loadProfile(){
  const sessions=arr(state.memory.sessions,'sessions');
  const projects=[...new Set(sessions.map(s=>s.project).filter(Boolean))];
  const select=$('profileProject'); const current=select.value||projects[0]||'';
  select.innerHTML=projects.map(p=>`<option value="${esc(p)}" ${p===current?'selected':''}>${esc(p)}</option>`).join('')||'<option value="">Current repository</option>';
  let remote=null; if(current&&state.memory.available)remote=await getJson('/api/memory/profile?project='+encodeURIComponent(current));
  const p=remote?.profile||remote;
  const topFiles=Array.isArray(p?.topFiles)?p.topFiles:[]; const concepts=Array.isArray(p?.topConcepts)?p.topConcepts:[];
  $('profileContent').innerHTML=`
    <article class="profile-card"><span>Project</span><strong>${esc(state.health.project_name||state.activeProject||'—')}</strong></article><article class="profile-card"><span>Repository</span><strong>${esc(state.activeRoot||state.health.root||'—')}</strong></article>
    <article class="profile-card"><span>Edition</span><strong>${esc(state.health.edition||'community')}</strong></article>
    <article class="profile-card"><span>Organization</span><strong>${esc(state.health.organization||'—')}</strong></article>
    <article class="profile-card"><span>Memory</span><strong>${memAvailable()?'connected':'offline'}</strong></article><article class="profile-card"><span>Local runs</span><strong>${runGroups().length}</strong></article><article class="profile-card"><span>Telemetry events</span><strong>${contextEvents().length}</strong></article>
    <article class="panel profile-wide"><h3>Top files</h3>${topFiles.slice(0,10).map(f=>`<div class="row"><span>${esc(f.file||f.path||'')}</span><strong>${f.frequency||0}</strong></div>`).join('')||'<div class="empty">No memory profile file data.</div>'}</article>
    <article class="panel profile-wide"><h3>Top concepts</h3>${concepts.slice(0,10).map(c=>`<div class="row"><span>${esc(c.concept||c.name||'')}</span><strong>${c.frequency||0}</strong></div>`).join('')||'<div class="empty">No memory profile concept data.</div>'}</article>`;
}

function remoteReplaySessions(){ return arr(state.memory.replaySessions,'sessions'); }
async function selectReplayValue(v){
  state.replay.runId=v; state.replay.cursor=0; state.replay.remoteTimeline=null; const cut=v.indexOf(':'); const kind=v.slice(0,cut),id=v.slice(cut+1); state.replay.source=kind;
  if(kind==='memory'&&id){const res=await getJson('/api/memory/replay/load?sessionId='+encodeURIComponent(id)); state.replay.remoteTimeline=res?.timeline||{events:[]};}
  renderReplay();
}
function replayEvents(){
  if(state.replay.source==='memory')return arr(state.replay.remoteTimeline,'events');
  const groups=runGroups(); const raw=state.replay.runId; const id=raw&&raw.startsWith('local:')?raw.slice(6):(groups[0]?.id||''); if(!state.replay.runId&&id)state.replay.runId=`local:${id}`;
  return groups.find(r=>r.id===id)?.events.slice().sort((a,b)=>eventTs(a)-eventTs(b))||[];
}
function renderReplay(){
  populateRunSelect('replayRun',state.replay.runId); const events=replayEvents(); if(state.replay.cursor>=events.length)state.replay.cursor=Math.max(0,events.length-1);
  $('replayList').innerHTML=events.map((e,i)=>`<button class="replay-event ${i===state.replay.cursor?'active':''}" data-idx="${i}"><span>${esc(e.type||e.kind||'event')}</span><small>${esc(eventLabel(e)||e.label||'')}</small></button>`).join('')||'<div class="empty">No replay events.</div>';
  const e=events[state.replay.cursor]; $('replayDetail').classList.toggle('empty',!e); $('replayDetail').innerHTML=e?`<h3>${esc(e.type||e.kind||'event')}</h3><p>${esc(eventLabel(e)||e.label||'')}</p><pre>${esc(JSON.stringify(e,null,2))}</pre>`:'Choose a session.';
  $('replayBar').style.width=events.length?`${((state.replay.cursor+1)/events.length)*100}%`:'0%'; document.querySelectorAll('.replay-event').forEach(b=>b.onclick=()=>{state.replay.cursor=Number(b.dataset.idx);renderReplay();}); $('replayPlay').textContent=state.replay.playing?'❚❚':'▶';
}
function stepReplay(delta){ const n=replayEvents().length; if(!n)return; state.replay.cursor=Math.min(n-1,Math.max(0,state.replay.cursor+delta)); renderReplay(); }
function toggleReplay(){ state.replay.playing=!state.replay.playing; if(state.replay.timer)clearInterval(state.replay.timer); state.replay.timer=null; if(state.replay.playing)state.replay.timer=setInterval(()=>{ const n=replayEvents().length; if(state.replay.cursor>=n-1){toggleReplay();return;} stepReplay(1); },700); renderReplay(); }

function renderHealth(){
  $('healthJson').textContent=JSON.stringify(state.health,null,2);
  const dep=state.health.deployment||{};
  const hostObserved=state.events.some(e=>e.source==='host_reported');
  const tokenObserved=state.events.some(e=>e.type==='token_usage');
  const statuses=[
    ['Developer console',state.health.ok,'127.0.0.1 local UI/API'],
    ['MCP',dep.mcp_active===true,dep.mcp_active===true?`${dep.mcp_transport||'connected'} · ${dep.mcp_local_only!==false?'local':'non-loopback'}`:'UI-only process'],
    ['Repository graph',state.health.capabilities?.graph===true,'deterministic local code intelligence'],
    ['Telemetry',state.telemetryInfo.available===true,state.telemetryInfo.available===true?(state.telemetryInfo.source||state.health.telemetry||'available'):(state.telemetryInfo.source||state.health.telemetry||'not configured')],
    ['Host run observation',hostObserved,hostObserved?'host lifecycle/tool events captured':'conditional: host adapter/hooks not reporting'],
    ['Token telemetry',tokenObserved,tokenObserved?'host/provider reported usage':'not reported by host'],
    ['CodeCortex Memory',memAvailable(),state.health.memory?.error||'connected'],
    ['Project',!!state.activeProject,state.health.project_name||state.activeProject||'unknown'],
    ['Repository',!!state.activeRoot,state.activeRoot||state.health.root||'unknown'],
    ['Graph UI',!!window.CodeCortexGraph,'neural graph runtime loaded'],
    ['Run snapshots',localCrystals().length>0,`${localCrystals().length} completed runs`]
  ];
  $('healthCards').innerHTML=statuses.map(([name,ok,detail])=>`<div class="status-card ${ok?'ok':'warn'}"><span>${esc(name)}</span><strong>${ok?'●':'○'} ${ok?'ready':'attention'}</strong><small>${esc(detail)}</small></div>`).join('');
  const steps=[
    ['Coding host',hostObserved?'observed':'connected through MCP','Codex / Claude / Gemini / generic MCP host'],
    ['CodeCortex MCP',dep.mcp_active?dep.mcp_transport||'active':'not active in this process','single local intelligence surface'],
    ['Code intelligence','ready','graph · impact · tests · dependencies'],
    ['Memory',memAvailable()?'connected':'optional / unavailable','project-scoped lessons and history'],
    ['Telemetry',state.telemetryInfo.available===true?'capturing':'awaiting events','observed actions only'],
    ['Developer UI','ready','health · run · graph · history · replay']
  ];
  $('wiringFlow').innerHTML=steps.map((x,i)=>`<div class="wiring-step"><div class="wiring-index">${i+1}</div><div><strong>${esc(x[0])}</strong><span>${esc(x[1])}</span><small>${esc(x[2])}</small></div></div>`).join('');
}

async function renderAll(){
  renderOverview(); renderSessions(); renderMemories(); renderLessons(); renderActions(); renderCrystals(); await renderTimeline(); renderAudit(); renderActivity(); renderReplay(); renderHealth();
  if(state.activeView==='profile')await loadProfile();
  renderContext();
  if(window.CodeCortexGraph)window.CodeCortexGraph.update(contextEvents(),{projectId:state.activeProject,root:state.activeRoot});
}
async function load(forceMemory=false){
  try{
    const [h,t,p,runs]=await Promise.all([getJson('/api/health'),getJson('/api/evidence?limit=5000'),getJson('/api/productivity'),getJson('/api/evidence/runs?limit=100')]);
    state.health=h; state.telemetryInfo=t||{}; state.productivity=p||{}; state.runIndex=runs||{}; state.events=Array.isArray(t?.events)?t.events:[]; renderContext(); await loadMemoryBundle(forceMemory); await renderAll();
  }catch(e){ state.health={ok:false,error:String(e)}; await renderAll(); }
}

document.querySelectorAll('#nav button').forEach(b=>b.onclick=async()=>{
  document.querySelectorAll('#nav button,.view').forEach(x=>x.classList.remove('active')); b.classList.add('active'); $(b.dataset.view).classList.add('active'); state.activeView=b.dataset.view;
  if(b.dataset.view==='graph'&&window.CodeCortexGraph){window.CodeCortexGraph.update(contextEvents(),{projectId:state.activeProject,root:state.activeRoot});requestAnimationFrame(()=>window.CodeCortexGraph.activate());}
  if(b.dataset.view==='profile')await loadProfile();
});
$('projectContext').onchange=()=>{state.activeProject=$('projectContext').value; const c=projectContexts().find(x=>x.id===state.activeProject); state.activeRoot=c?.root||state.health.root||''; state.selectedSession=''; renderAll();};
$('refresh').onclick=()=>load(true);
$('timelineRun').onchange=()=>renderTimeline();
$('profileProject').onchange=loadProfile;
$('replayRun').onchange=()=>selectReplayValue($('replayRun').value);
$('replayPrev').onclick=()=>stepReplay(-1); $('replayNext').onclick=()=>stepReplay(1); $('replayPlay').onclick=toggleReplay;

['memories','lessons','actions','crystals'].forEach(name=>{ const el=$(name+'Search'); if(el)el.oninput=()=>{state.filters[name]=el.value; ({memories:renderMemories,lessons:renderLessons,actions:renderActions,crystals:renderCrystals}[name])();}; });

async function runSafeCommand(){
  const name=$('commandName').value,target=$('commandTarget').value.trim(); $('commandOutput').textContent='Running…';
  try{const j=await getJson(`/api/command/${encodeURIComponent(name)}?target=${encodeURIComponent(target)}`); $('commandOutput').textContent=[j.ok?'✓ success':'✕ failed',j.stdout||'',j.stderr||'',j.error||''].filter(Boolean).join('\n');}
  catch(e){$('commandOutput').textContent=String(e);}
}
$('runCommand').onclick=runSafeCommand;
load(true); setInterval(()=>load(false),1500);
