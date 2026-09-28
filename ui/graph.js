/*
 * SPDX-License-Identifier: Apache-2.0
 * CodeCortex live graph viewer.
 * See LEGAL/ and THIRD_PARTY.md for attribution.
 * Modifications Copyright (c) 2026 Saurabh Verma.
 */
(function(){
  'use strict';

  const TYPE_COLORS = {
    file:'#16A56A', symbol:'#2F80ED', test:'#D98E04', commit:'#D97706', module:'#7357E8',
    runtime_error:'#D64545', trace:'#0D9FB6', agent_run:'#7357E8', model:'#4F46E5', tool_call:'#3B82F6',
    hypothesis:'#8B5CF6', decision:'#6D5BD0', patch:'#64748B', memory:'#19AFC0'
  };
  const TYPE_GLYPHS = {file:'F',symbol:'S',test:'T',commit:'G',module:'M',runtime_error:'!',trace:'Q',agent_run:'R',model:'AI',tool_call:'›',hypothesis:'?',decision:'✓',patch:'Δ',memory:'µ'};
  const MODE_META = {
    live:{icon:'◎',title:'Live Run',desc:'What the coding agent actually did in this run.',help:'Observed runtime evidence: tool calls, reads, edits, tests, memory and proof.'},
    code:{icon:'⌘',title:'Code',desc:'Explore symbols, callers, callees and tests.',help:'Structural code evidence. Expand a symbol to inspect its neighborhood and Git/test reach.'},
    architecture:{icon:'◈',title:'Architecture',desc:'See modules, communities and boundaries.',help:'A high-level structural view for understanding system shape without opening every file.'},
    dependencies:{icon:'⇢',title:'Dependencies',desc:'Inspect imports, cycles and dependency risk.',help:'File-level dependency relationships, cycles and instability signals.'},
    memory:{icon:'◇',title:'Memory',desc:'Connect prior learning to the current run.',help:'Run-to-memory links such as recalled lessons and saved checkpoints.'}
  };
  const TYPE_SHAPES = {
    file:'rect', symbol:'circle', test:'hexagon', commit:'hexagon', module:'hexagon', runtime_error:'diamond',
    trace:'diamond', agent_run:'circle', model:'hexagon', tool_call:'circle', hypothesis:'diamond',
    decision:'diamond', patch:'rect', memory:'hexagon'
  };
  const EDGE_COLORS = {
    CALLS:'#64748B', IMPORTS:'#94A3B8', REFERENCES:'#94A3B8', CONTAINS:'#7C3AED', CYCLE:'#CC0000', CHANGED_BY:'#C2410C',
    TESTED_BY:'#B8860B', FAILED_AT:'#CC0000', INSPECTED_BY:'#2563EB', ATTEMPTED_BY:'#6B3FA0',
    REJECTED_BECAUSE:'#CC0000', FIXED_BY:'#2D6A4F', VERIFIED_BY:'#2D6A4F', REMEMBERED_AS:'#0E7490',
    RECALLED:'#0891B2', CALLED:'#2563EB', RUNS_MODEL:'#4F46E5', RETRIED:'#B45309'
  };

  const sim = {
    nodes:[], edges:[], byId:new Map(), canvas:null, ctx:null, wrap:null, tooltip:null,
    sidebar:null, running:false, raf:null, panX:0, panY:0, zoom:1, dragNode:null,
    panning:false, panStartX:0, panStartY:0, mouseX:0, mouseY:0, tickCount:0, quietTicks:0,
    filters:{}, search:'', selected:null, graphKey:'', staticNodes:new Map(), staticEdges:[], mode:'live', lens:'all', onlyFloating:false, context:{projectId:'',root:''}
  };

  function esc(s){
    const d=document.createElement('div'); d.textContent=String(s??''); return d.innerHTML;
  }
  function short(s,n=42){ s=String(s??''); return s.length>n?s.slice(0,n-1)+'…':s; }
  function colorFor(type){ return TYPE_COLORS[type] || '#64748B'; }
  function shapeFor(type){ return TYPE_SHAPES[type] || 'circle'; }
  function glyphFor(type){ return TYPE_GLYPHS[type] || '•'; }

  function makeGraph(events){
    const nodeMap=new Map(), edges=[];
    function node(id,type,label,evidence='',source='observed'){
      if(!id) return null;
      if(!nodeMap.has(id)) nodeMap.set(id,{id,type,label:label||id,evidence,source,x:0,y:0,vx:0,vy:0,r:11});
      return nodeMap.get(id);
    }
    function edge(from,to,type,evidence='',source='observed'){
      if(!from||!to||from===to) return;
      edges.push({from,to,type,evidence,source});
    }
    const latest = (events||[]).slice(-1200);
    const readCounts=new Map(), searchCounts=new Map(), toolCounts=new Map();
    latest.forEach(e=>{
      const key=String(e.target||'').trim(); if(!key)return;
      if(e.type==='file_read')readCounts.set(key,(readCounts.get(key)||0)+1);
      if(e.type==='search')searchCounts.set(key,(searchCounts.get(key)||0)+1);
      if(e.type==='tool_call')toolCounts.set(key,(toolCounts.get(key)||0)+1);
    });
    const modelSeen=new Set();
    latest.forEach((e,idx)=>{
      const runId=e.run_id || 'unknown';
      const source=e.source||'observed';
      const run=node('run:'+runId,'agent_run',runId,e.detail||'',source);
      if(e.model){ const mid='model:'+runId+':'+e.model; const m=node(mid,'model',e.model,'host/model reported',source); if(!modelSeen.has(mid)){edge(run.id,m.id,'RUNS_MODEL','model identity',source);modelSeen.add(mid);} }
      if(e.type==='run_started'){
        if(e.target){ const f=node('file:'+e.target,'file',e.target,'repository root'); edge(run.id,f.id,'INSPECTED_BY','repository'); }
        return;
      }
      if(e.type==='tool_call'){
        const tid='tool:'+runId+':'+idx;
        const repeat=(toolCounts.get(String(e.target||'').trim())||0)>1?' · repeated tool':'', detail=(e.detail||'')+repeat;
        const t=node(tid,'tool_call',e.target||'tool call',detail,source);
        edge(run.id,t.id,'CALLED',e.duration_ms!=null?String(e.duration_ms)+' ms':'',source);
        if(e.success===false){const fid='failure:'+runId+':'+idx;const f=node(fid,'runtime_error','Failed: '+short(e.target||'tool',30),e.detail||'host-reported failure',source);edge(t.id,f.id,'REJECTED_BECAUSE','tool failed',source);}
        return;
      }
      if(e.type==='file_read'){
        const count=readCounts.get(String(e.target||'').trim())||1;
        const evidence=[e.detail||'',count>1?'repeated read ×'+count:''].filter(Boolean).join(' · ');
        const f=node('file:'+String(e.target||idx),'file',e.target||'file',evidence,source);
        edge(run.id,f.id,'INSPECTED_BY',count>1?'repeated work':'',source); return;
      }
      if(e.type==='edit'){
        const f=node('file:'+String(e.target||idx),'file',e.target||'file',e.detail||'',source);
        const p=node('patch:'+runId+':'+idx,'patch','Edit '+short(e.target||'',26),e.detail||'',source);
        edge(run.id,p.id,'ATTEMPTED_BY','',source); edge(f.id,p.id,'CHANGED_BY','',source); return;
      }
      if(e.type==='test'){
        const t=node('test:'+String(e.target||idx),'test',e.target||'test',e.detail||'',source);
        edge(run.id,t.id,e.success===false?'REJECTED_BECAUSE':'VERIFIED_BY',e.success===false?'failed':'passed',source); return;
      }
      if(e.type==='memory_recall'){
        const m=node('memory-recall:'+runId+':'+idx,'memory','Recall: '+short(e.target||'memory',34),e.detail||'',source);
        edge(run.id,m.id,'RECALLED','',source); return;
      }
      if(e.type==='checkpoint'){
        const m=node('memory:'+runId+':'+idx,'memory',e.target||'Checkpoint',e.detail||'',source);
        edge(run.id,m.id,'REMEMBERED_AS','',source); return;
      }
      if(e.type==='search'){
        const count=searchCounts.get(String(e.target||'').trim())||1;
        const evidence=[e.detail||'',count>1?'repeated search ×'+count:''].filter(Boolean).join(' · ');
        const t=node('trace:'+runId+':'+idx,'trace',e.target||'Search',evidence,source);
        edge(run.id,t.id,'INSPECTED_BY',count>1?'repeated work':'',source); return;
      }
      if(e.type==='test_selection'){
        const t=node('test-selection:'+runId+':'+idx,'test',e.target||'Required tests',e.detail||'',source);
        edge(run.id,t.id,'TESTS_IDENTIFIED','selection only',source); return;
      }
      if(e.type==='proof_check'){
        const p=node('proof:'+runId+':'+idx,'decision','Proof check',e.detail||'',source);
        edge(run.id,p.id,e.success===false?'REJECTED_BECAUSE':'CHECKED_BY',e.success===false?'check failed':'analysis gate completed',source); return;
      }
      if(e.type==='run_finished'){
        run.evidence=[run.evidence,e.detail||'',e.success===false?'finished with failure':'finished'].filter(Boolean).join(' · '); return;
      }
    });
    const nodes=[...nodeMap.values()];
    // Keep the browser responsive. Prefer connected/recent nodes over an unbounded graph.
    const degree=new Map(nodes.map(n=>[n.id,0]));
    edges.forEach(e=>{degree.set(e.from,(degree.get(e.from)||0)+1);degree.set(e.to,(degree.get(e.to)||0)+1);});
    const keep=new Set(nodes.sort((a,b)=>(degree.get(b.id)||0)-(degree.get(a.id)||0)).slice(0,500).map(n=>n.id));
    return {nodes:nodes.filter(n=>keep.has(n.id)),edges:edges.filter(e=>keep.has(e.from)&&keep.has(e.to))};
  }

  function ensureDom(){
    if(sim.canvas) return;
    sim.wrap=document.getElementById('graph-canvas-wrap');
    sim.canvas=document.getElementById('graph-canvas');
    sim.sidebar=document.getElementById('graph-sidebar');
    sim.tooltip=document.getElementById('graph-tooltip');
    if(!sim.canvas||!sim.wrap||!sim.sidebar) return;
    sim.ctx=sim.canvas.getContext('2d');
    setupInteraction();
    window.addEventListener('resize',resizeCanvas);
    resizeCanvas();
  }

  function resizeCanvas(){
    if(!sim.canvas||!sim.wrap) return;
    const r=sim.wrap.getBoundingClientRect(); if(r.width<10||r.height<10) return;
    const dpr=Math.min(window.devicePixelRatio||1,2);
    sim.canvas.width=Math.floor(r.width*dpr); sim.canvas.height=Math.floor(r.height*dpr);
    sim.canvas.style.width=r.width+'px'; sim.canvas.style.height=r.height+'px';
    sim.ctx.setTransform(dpr,0,0,dpr,0,0); renderGraph();
  }

  function seedPositions(){
    if(!sim.canvas) return;
    const r=sim.canvas.getBoundingClientRect(), cx=r.width/2, cy=r.height/2;
    sim.nodes.forEach((n,i)=>{
      const a=(i/Math.max(1,sim.nodes.length))*Math.PI*2;
      const ring=50+Math.sqrt(i+1)*18;
      n.x=cx+Math.cos(a)*ring+(Math.random()-.5)*20;
      n.y=cy+Math.sin(a)*ring+(Math.random()-.5)*20;
      n.vx=n.vy=0;
    });
    sim.panX=sim.panY=0; sim.zoom=1; sim.tickCount=sim.quietTicks=0;
  }

  function visible(n){
    if(n._forceHidden) return false;
    if(sim.filters[n.type]===false) return false;
    if(!sim.search) return true;
    const q=sim.search.toLowerCase();
    return String(n.label).toLowerCase().includes(q)||String(n.evidence||'').toLowerCase().includes(q)||String(n.type).toLowerCase().includes(q);
  }

  function runSimulation(){
    sim.raf=null; if(!sim.running) return;
    const vis=sim.nodes.filter(visible), n=vis.length;
    let energy=0;
    // Capped force-directed repulsion keeps dense neighborhoods readable.
    for(let i=0;i<n;i++) for(let j=i+1;j<n;j++){
      const a=vis[i],b=vis[j]; let dx=a.x-b.x,dy=a.y-b.y; let d2=dx*dx+dy*dy;
      if(d2<4){dx=(Math.random()-.5)*2;dy=(Math.random()-.5)*2;d2=4;}
      const d=Math.sqrt(d2), force=Math.min(1.8,1200/d2);
      const fx=force*dx/d,fy=force*dy/d; a.vx+=fx;a.vy+=fy;b.vx-=fx;b.vy-=fy;
    }
    sim.edges.forEach(e=>{
      const a=sim.byId.get(e.from),b=sim.byId.get(e.to); if(!a||!b||!visible(a)||!visible(b)) return;
      const dx=b.x-a.x,dy=b.y-a.y,d=Math.max(1,Math.hypot(dx,dy));
      const ideal=100, force=(d-ideal)*0.0028; const fx=force*dx/d,fy=force*dy/d;
      a.vx+=fx;a.vy+=fy;b.vx-=fx;b.vy-=fy;
    });
    if(sim.canvas){
      const r=sim.canvas.getBoundingClientRect(),cx=r.width/2,cy=r.height/2;
      vis.forEach(a=>{a.vx+=(cx-a.x)*0.00035;a.vy+=(cy-a.y)*0.00035;});
    }
    vis.forEach(a=>{
      if(a===sim.dragNode){a.vx=a.vy=0;return;}
      a.vx*=0.87;a.vy*=0.87;a.x+=a.vx;a.y+=a.vy;energy+=Math.abs(a.vx)+Math.abs(a.vy);
    });
    sim.tickCount++; renderGraph();
    if(energy<0.05*n||sim.tickCount>1200) sim.quietTicks++; else sim.quietTicks=0;
    if(sim.quietTicks<35) sim.raf=requestAnimationFrame(runSimulation);
  }
  function wake(){sim.quietTicks=0;if(sim.running&&!sim.raf)sim.raf=requestAnimationFrame(runSimulation);}

  function screen(n){return{x:n.x*sim.zoom+sim.panX,y:n.y*sim.zoom+sim.panY};}
  function graphPoint(x,y){return{x:(x-sim.panX)/sim.zoom,y:(y-sim.panY)/sim.zoom};}
  function nodeAt(x,y){
    for(let i=sim.nodes.length-1;i>=0;i--){const n=sim.nodes[i];if(!visible(n))continue;const p=screen(n);if(Math.hypot(x-p.x,y-p.y)<=Math.max(14,n.r*sim.zoom+5))return n;} return null;
  }

  function drawShape(ctx,n,x,y,r){
    const s=shapeFor(n.type);ctx.beginPath();
    if(s==='rect')ctx.roundRect(x-r*1.25,y-r*.85,r*2.5,r*1.7,3);
    else if(s==='diamond'){ctx.moveTo(x,y-r);ctx.lineTo(x+r,y);ctx.lineTo(x,y+r);ctx.lineTo(x-r,y);ctx.closePath();}
    else if(s==='hexagon'){for(let i=0;i<6;i++){const a=Math.PI/3*i-Math.PI/6,px=x+Math.cos(a)*r,py=y+Math.sin(a)*r;i?ctx.lineTo(px,py):ctx.moveTo(px,py);}ctx.closePath();}
    else ctx.arc(x,y,r,0,Math.PI*2);
  }

  function renderGraph(){
    if(!sim.ctx||!sim.canvas)return;const r=sim.canvas.getBoundingClientRect(),ctx=sim.ctx;
    ctx.clearRect(0,0,r.width,r.height);
    // subtle grid
    ctx.save();ctx.strokeStyle='rgba(100,116,139,.08)';ctx.lineWidth=1;const gap=28;
    for(let x=((sim.panX%gap)+gap)%gap;x<r.width;x+=gap){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,r.height);ctx.stroke();}
    for(let y=((sim.panY%gap)+gap)%gap;y<r.height;y+=gap){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(r.width,y);ctx.stroke();}ctx.restore();

    const selectedNeighbors=new Set();
    if(sim.selected){selectedNeighbors.add(sim.selected.id);sim.edges.forEach(e=>{if(e.from===sim.selected.id)selectedNeighbors.add(e.to);if(e.to===sim.selected.id)selectedNeighbors.add(e.from);});}
    sim.edges.forEach(e=>{
      const a=sim.byId.get(e.from),b=sim.byId.get(e.to);if(!a||!b||!visible(a)||!visible(b))return;
      const p=screen(a),q=screen(b);const dx=q.x-p.x,dy=q.y-p.y,d=Math.max(1,Math.hypot(dx,dy));
      const selectedEdge=!sim.selected||e.from===sim.selected.id||e.to===sim.selected.id;
      ctx.beginPath();ctx.moveTo(p.x,p.y);ctx.lineTo(q.x,q.y);
      ctx.strokeStyle=EDGE_COLORS[e.type]||'#7890A4';ctx.globalAlpha=selectedEdge?.62:.10;
      const prov=String(e.provenance||'');
      const source=String(e.source||'observed');
      ctx.setLineDash(source==='structural'?[4,4]:(prov==='split'||prov==='ambiguous')?[7,4]:(prov==='binding'||prov==='import')?[2,3]:[]);
      ctx.lineWidth=Math.max(.8,(source==='observed'?1.45:1.05)*sim.zoom);ctx.stroke();ctx.setLineDash([]);
      if(selectedEdge&&sim.zoom>.48){
        const ux=dx/d,uy=dy/d,tipX=q.x-ux*Math.max(10,b.r*sim.zoom),tipY=q.y-uy*Math.max(10,b.r*sim.zoom);
        ctx.beginPath();ctx.moveTo(tipX,tipY);ctx.lineTo(tipX-ux*8-uy*4,tipY-uy*8+ux*4);ctx.lineTo(tipX-ux*8+uy*4,tipY-uy*8-ux*4);ctx.closePath();ctx.fillStyle=EDGE_COLORS[e.type]||'#7890A4';ctx.fill();
      }
      const shouldLabel=(sim.selected&&(e.from===sim.selected.id||e.to===sim.selected.id))||sim.zoom>1.35;
      if(shouldLabel){ctx.fillStyle=EDGE_COLORS[e.type]||'#64748B';ctx.globalAlpha=.78;ctx.font='9px ui-monospace,monospace';ctx.fillText(e.type,(p.x+q.x)/2+4,(p.y+q.y)/2-4);}
      ctx.globalAlpha=1;
    });

    const degree=new Map(sim.nodes.map(n=>[n.id,0]));sim.edges.forEach(e=>{degree.set(e.from,(degree.get(e.from)||0)+1);degree.set(e.to,(degree.get(e.to)||0)+1);});
    sim.nodes.forEach(n=>{
      if(!visible(n))return;const p=screen(n);const deg=degree.get(n.id)||0;const rr=Math.max(6,(n.r+Math.min(5,Math.sqrt(deg)*1.5))*sim.zoom);
      const focused=!sim.selected||selectedNeighbors.has(n.id);
      ctx.save();ctx.globalAlpha=focused?1:.18;
      if(focused){ctx.beginPath();ctx.arc(p.x,p.y,rr+5,0,Math.PI*2);ctx.fillStyle=colorFor(n.type)+'13';ctx.fill();}
      ctx.shadowColor=colorFor(n.type)+'55';ctx.shadowBlur=sim.selected===n?20:focused?8:0;
      drawShape(ctx,n,p.x,p.y,rr);ctx.fillStyle=colorFor(n.type);ctx.fill();
      ctx.lineWidth=2;ctx.strokeStyle='rgba(255,255,255,.92)';ctx.stroke();
      if(sim.selected===n){ctx.beginPath();ctx.arc(p.x,p.y,rr+4,0,Math.PI*2);ctx.lineWidth=2;ctx.strokeStyle='#19C6D4';ctx.stroke();}
      ctx.shadowBlur=0;ctx.fillStyle='#fff';ctx.font=`700 ${Math.max(8,Math.min(12,rr*.75))}px ui-sans-serif,system-ui`;ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(glyphFor(n.type),p.x,p.y+.4);ctx.restore();
      if((sim.zoom>.62||sim.selected===n)&&focused){ctx.font='11px ui-sans-serif,system-ui';ctx.textAlign='left';ctx.textBaseline='alphabetic';ctx.fillStyle=getComputedStyle(document.documentElement).getPropertyValue('--ink')||'#18212b';ctx.fillText(short(n.label,31),p.x+rr+7,p.y+4);}
    });
  }

  function renderSidebar(){
    if(!sim.sidebar)return;
    const types=[...new Set(sim.nodes.map(n=>n.type))].sort();
    const floating=sim.nodes.filter(n=>!sim.edges.some(e=>e.from===n.id||e.to===n.id)).length;
    let html='<input class="graph-search" id="ccx-graph-search" placeholder="Search nodes…" value="'+esc(sim.search)+'">';
    html+='<h3>Graph mode</h3><div class="graph-mode-row">';
    ['live','code','architecture','dependencies','memory'].forEach(mode=>{const m=MODE_META[mode];html+='<button class="graph-action '+(sim.mode===mode?'active':'')+'" data-graph-mode="'+mode+'"><span class="graph-mode-icon">'+m.icon+'</span><span class="graph-mode-title">'+m.title+'</span><span class="graph-mode-desc">'+m.desc+'</span></button>';});
    html+='</div><div class="graph-mode-help"><strong>'+MODE_META[sim.mode].title+'</strong>'+MODE_META[sim.mode].help+'</div>';
    const observed=sim.edges.filter(e=>['observed','host_reported','provider_reported'].includes(e.source||'observed')).length, structural=sim.edges.filter(e=>e.source==='structural').length;
    html+='<h3>Graph stats</h3><div class="graph-stats"><div><b>'+sim.nodes.length+'</b><span>Nodes</span></div><div><b>'+sim.edges.length+'</b><span>Edges</span></div><div><b>'+observed+'</b><span>Observed</span></div><div><b>'+structural+'</b><span>Structural</span></div><div><b>'+floating+'</b><span>Floating</span></div></div>';
    html+='<h3>Evidence lenses</h3><div class="graph-mode-row graph-lenses">';
    [['all','All evidence'],['runtime','Runtime'],['tests','Test coverage'],['git','Git churn'],['confidence','Confidence']].forEach(([lens,label])=>{html+='<button class="graph-action '+(sim.lens===lens?'active':'')+'" data-graph-lens="'+lens+'"><span class="graph-mode-title">'+label+'</span></button>';});
    html+='</div><div class="graph-lens-note">Complexity remains unavailable unless the structural metric source reports it; CodeCortex does not invent a complexity value.</div>';
    html+='<h3>Filter by type</h3><div class="graph-filters">';
    types.forEach(t=>{html+='<label><input type="checkbox" data-graph-type="'+esc(t)+'" '+(sim.filters[t]===false?'':'checked')+'><i style="background:'+colorFor(t)+'"></i>'+esc(t.replaceAll('_',' '))+'</label>';});
    html+='</div><h3>Expand static code graph</h3><div class="graph-expand-row"><input class="graph-search" id="ccx-symbol-expand" placeholder="Function or file:name"><button class="graph-action" id="ccx-expand-symbol">Expand</button></div>';
    html+='<h3>Find call path</h3><div class="graph-path-row"><input class="graph-search" id="ccx-path-from" placeholder="From"><input class="graph-search" id="ccx-path-to" placeholder="To"><button class="graph-action" id="ccx-path-run">Path</button></div>';
    html+='<div class="graph-confidence"><h3>How to read the graph</h3><div><span class="line solid"></span> observed agent/runtime action</div><div><span class="line binding"></span> structural code relationship</div><div><span class="line split"></span> ambiguous resolver evidence</div><div class="graph-read-tip">Select a node to spotlight its neighborhood. Node shape + glyph + color all carry meaning so the graph is not color-dependent.</div></div>';
    html+='<button class="graph-action" id="ccx-show-floating">Show floating nodes</button><button class="graph-action" id="ccx-recenter">Recenter</button>';
    html+='<div id="graph-selected">'+selectedHtml()+'</div>';
    sim.sidebar.innerHTML=html;
    const s=document.getElementById('ccx-graph-search'); if(s)s.oninput=()=>{sim.search=s.value;renderGraph();};
    sim.sidebar.querySelectorAll('[data-graph-type]').forEach(cb=>cb.onchange=()=>{sim.filters[cb.dataset.graphType]=cb.checked;renderGraph();});
    sim.sidebar.querySelectorAll('[data-graph-mode]').forEach(b=>b.onclick=()=>setGraphMode(b.dataset.graphMode));
    sim.sidebar.querySelectorAll('[data-graph-lens]').forEach(b=>b.onclick=()=>applyLens(b.dataset.graphLens));
    const expand=document.getElementById('ccx-expand-symbol'); if(expand)expand.onclick=()=>{const input=document.getElementById('ccx-symbol-expand');expandSymbolGraph(input?input.value:'');};
    const expandInput=document.getElementById('ccx-symbol-expand'); if(expandInput)expandInput.onkeydown=e=>{if(e.key==='Enter')expandSymbolGraph(expandInput.value);};
    const pathRun=document.getElementById('ccx-path-run'); if(pathRun)pathRun.onclick=()=>loadPath(document.getElementById('ccx-path-from')?.value||'',document.getElementById('ccx-path-to')?.value||'');
    const f=document.getElementById('ccx-show-floating'); if(f)f.onclick=()=>{sim.onlyFloating=!sim.onlyFloating;const float=new Set(sim.nodes.filter(n=>!sim.edges.some(e=>e.from===n.id||e.to===n.id)).map(n=>n.id));sim.nodes.forEach(n=>n._forceHidden=sim.onlyFloating&&!float.has(n.id));f.textContent=sim.onlyFloating?'Show all nodes':'Show floating nodes';renderGraph();};
    const rc=document.getElementById('ccx-recenter'); if(rc)rc.onclick=recenter;
  }

  function applyLens(lens){
    sim.lens=lens||'all';
    const keep=new Set();
    if(sim.lens==='runtime')sim.nodes.filter(n=>n.source!=='structural').forEach(n=>keep.add(n.id));
    else if(sim.lens==='tests'){sim.nodes.filter(n=>n.type==='test').forEach(n=>keep.add(n.id));}
    else if(sim.lens==='git'){sim.nodes.filter(n=>n.type==='commit').forEach(n=>keep.add(n.id));}
    else if(sim.lens==='confidence'){sim.edges.filter(e=>/split|ambig|heuristic|unresolved/i.test(String(e.provenance||e.evidence||''))).forEach(e=>{keep.add(e.from);keep.add(e.to);});}
    if(keep.size){sim.edges.forEach(e=>{if(keep.has(e.from)||keep.has(e.to)){keep.add(e.from);keep.add(e.to);}});}
    sim.nodes.forEach(n=>{n._forceHidden=sim.lens!=='all'&&!keep.has(n.id);});
    renderSidebar();renderGraph();
  }

  function selectedHtml(){
    const n=sim.selected;if(!n)return '<div class="graph-selected-empty">Click a node to inspect it.</div>';
    const out=sim.edges.filter(e=>e.from===n.id),inc=sim.edges.filter(e=>e.to===n.id);
    const expand=(n.type==='symbol')?'<button class="graph-action" data-expand-selected="1">Expand evidence</button>':'';
    setTimeout(()=>{const b=document.querySelector('[data-expand-selected]');if(b)b.onclick=()=>expandSymbolGraph(n.label);},0);
    let detail='<dl><dt>Outgoing</dt><dd>'+out.length+'</dd><dt>Incoming</dt><dd>'+inc.length+'</dd><dt>Source</dt><dd>'+esc(n.source||'structural')+'</dd><dt>Evidence</dt><dd>'+esc(n.evidence||'—')+'</dd>';
    if(n.meta?.file)detail+='<dt>File</dt><dd>'+esc(n.meta.file)+'</dd>';
    if(n.meta?.author)detail+='<dt>Author</dt><dd>'+esc(n.meta.author)+'</dd>';
    if(n.meta?.subject)detail+='<dt>Commit</dt><dd>'+esc(n.meta.subject)+'</dd>';
    if(n.meta?.run)detail+='<dt>Run</dt><dd>'+esc(n.meta.run)+'</dd>';
    detail+='</dl>';
    return '<div class="selected-node"><h3>Selected node</h3><strong>'+esc(n.label)+'</strong><span class="node-badge" style="border-color:'+colorFor(n.type)+';color:'+colorFor(n.type)+'">'+esc(n.type.replaceAll('_',' '))+'</span>'+detail+expand+'</div>';
  }

  function staticNodeId(row){
    const p=String(row?.p||'');
    return 'symbol:'+p+':'+String(row?.n||'unknown');
  }

  function mergeStaticGraph(payload,symbol){
    if(!payload||payload.ok!==true)return;
    const rootId='symbol:root:'+symbol;
    sim.staticNodes.set(rootId,{id:rootId,type:'symbol',label:symbol,evidence:'CodeCortex static graph + tests + git',meta:{file:payload.file||''},source:'structural',x:0,y:0,vx:0,vy:0,r:13});
    const addRows=(obj,key,direction)=>{
      const rows=(obj&&Array.isArray(obj[key]))?obj[key]:[];
      rows.forEach(row=>{
        const id=staticNodeId(row), evidence=[row.p||'',row.tested?'test-reached':'',row.prov||''].filter(Boolean).join(' · ');
        sim.staticNodes.set(id,{id,type:'symbol',label:row.n||id,evidence,meta:{file:row.p||''},source:'structural',x:0,y:0,vx:0,vy:0,r:11});
        const edge=direction==='in'?{from:id,to:rootId,type:'CALLS',evidence:'caller',provenance:row.prov||'',source:'structural'}:{from:rootId,to:id,type:'CALLS',evidence:'callee',provenance:row.prov||'',source:'structural'};
        if(!sim.staticEdges.some(e=>e.from===edge.from&&e.to===edge.to&&e.type===edge.type))sim.staticEdges.push(edge);
      });
    };
    addRows(payload.callers,'callers','in'); addRows(payload.callees,'callees','out');

    const tests=payload.tests&&Array.isArray(payload.tests.tests_to_run)?payload.tests.tests_to_run:[];
    tests.forEach((t,i)=>{
      const paths=Array.isArray(t.p)?t.p:[t.p];
      paths.filter(Boolean).forEach((p,j)=>{
        const id='test:'+p; const run=t.run||'';
        sim.staticNodes.set(id,{id,type:'test',label:p,evidence:[t.evidence||'',run].filter(Boolean).join(' · '),meta:{run},source:'structural',x:0,y:0,vx:0,vy:0,r:11});
        sim.staticEdges.push({from:rootId,to:id,type:'TESTED_BY',evidence:t.evidence||'test reach',provenance:'resolved',source:'structural'});
      });
    });
    (payload.commits||[]).forEach(c=>{
      const id='commit:'+c.sha;
      sim.staticNodes.set(id,{id,type:'commit',label:c.short||c.sha,evidence:c.subject||'',meta:{author:c.author||'',subject:c.subject||'',file:payload.file||''},source:'structural',x:0,y:0,vx:0,vy:0,r:11});
      sim.staticEdges.push({from:rootId,to:id,type:'CHANGED_BY',evidence:c.subject||'',provenance:'git',source:'structural'});
    });
    const live=makeGraph(window.CodeCortexLastEvents||[]); applyGraph(live);
  }

  async function expandSymbolGraph(symbol){
    symbol=String(symbol||'').trim(); if(!symbol)return;
    try{
      const res=await fetch('/api/graph/symbol?target='+encodeURIComponent(symbol)+(sim.context.root?'&path='+encodeURIComponent(sim.context.root):''),{cache:'no-store'});
      const data=await res.json();
      if(!data||data.ok!==true){console.warn('[CodeCortex graph] static expansion failed',data);return;}
      mergeStaticGraph(data,symbol);
      sim.selected=sim.byId.get('symbol:root:'+symbol)||sim.selected; renderSidebar(); renderGraph(); wake();
    }catch(err){console.warn('[CodeCortex graph] static expansion error',err);}
  }

  function parseXml(text){
    try{return new DOMParser().parseFromString(String(text||''),'application/xml');}catch(_){return null;}
  }
  async function setGraphMode(mode){
    sim.mode=mode||'live';
    if(sim.mode==='live'){
      const saved=sim.staticNodes;const savedEdges=sim.staticEdges;sim.staticNodes=new Map();sim.staticEdges=[];
      applyGraph(makeGraph(window.CodeCortexLastEvents||[]));sim.staticNodes=saved;sim.staticEdges=savedEdges;return;
    }
    if(sim.mode==='code'){
      applyGraph({nodes:[],edges:[]});return;
    }
    if(sim.mode==='memory'){
      const g=makeGraph(window.CodeCortexLastEvents||[]), keep=new Set(g.nodes.filter(n=>n.type==='agent_run'||n.type==='memory').map(n=>n.id));
      const saved=sim.staticNodes,savedEdges=sim.staticEdges;sim.staticNodes=new Map();sim.staticEdges=[];
      applyGraph({nodes:g.nodes.filter(n=>keep.has(n.id)),edges:g.edges.filter(e=>keep.has(e.from)&&keep.has(e.to))});sim.staticNodes=saved;sim.staticEdges=savedEdges;return;
    }
    if(sim.mode==='architecture')await loadArchitecture();
    if(sim.mode==='dependencies')await loadDependencies();
  }
  async function loadArchitecture(){
    try{
      const r=await fetch('/api/graph/architecture'+(sim.context.root?'?path='+encodeURIComponent(sim.context.root):''),{cache:'no-store'}),data=await r.json();
      if(!data?.ok)return;
      const doc=parseXml(data.communities_xml); if(!doc)return;
      sim.staticNodes.clear();sim.staticEdges=[];
      [...doc.querySelectorAll('community')].forEach(c=>{
        const id='module:'+c.getAttribute('id'), label=c.getAttribute('label')||('Module '+c.getAttribute('id'));
        sim.staticNodes.set(id,{id,type:'module',label,evidence:'size '+(c.getAttribute('size')||'?')+' · '+(c.getAttribute('dir')||''),meta:{},source:'structural',x:0,y:0,vx:0,vy:0,r:16});
        [...c.querySelectorAll('member')].forEach(m=>{
          const sid='symbol:'+String(m.getAttribute('p')||'')+':'+String(m.getAttribute('n')||'');
          sim.staticNodes.set(sid,{id:sid,type:'symbol',label:m.getAttribute('n')||sid,evidence:m.getAttribute('p')||'',meta:{file:m.getAttribute('p')||''},source:'structural',x:0,y:0,vx:0,vy:0,r:9});
          sim.staticEdges.push({from:id,to:sid,type:'CONTAINS',evidence:'Louvain community',provenance:'resolved',source:'structural'});
        });
      });
      applyGraph({nodes:[],edges:[]});
    }catch(err){console.warn('[CodeCortex graph] architecture load failed',err);}
  }
  async function loadDependencies(){
    try{
      const r=await fetch('/api/graph/dependencies'+(sim.context.root?'?path='+encodeURIComponent(sim.context.root):''),{cache:'no-store'}),data=await r.json();
      if(!data?.ok)return;
      const doc=parseXml(data.deps_xml); if(!doc)return;
      sim.staticNodes.clear();sim.staticEdges=[];
      [...doc.querySelectorAll('deps > f')].slice(0,80).forEach(f=>{
        const path=f.getAttribute('p')||''; if(!path)return; const fid='file:'+path;
        sim.staticNodes.set(fid,{id:fid,type:'file',label:path,evidence:'afferent '+(f.getAttribute('afferent')||'0')+' · instability '+(f.getAttribute('instab')||'?'),meta:{file:path},source:'structural',x:0,y:0,vx:0,vy:0,r:10});
        [...f.querySelectorAll('inc')].slice(0,20).forEach(inc=>{
          const to=inc.getAttribute('t')||''; if(!to)return; const tid='file:'+to;
          if(!sim.staticNodes.has(tid))sim.staticNodes.set(tid,{id:tid,type:'file',label:to,evidence:'dependency target',meta:{file:to},source:'structural',x:0,y:0,vx:0,vy:0,r:9});
          sim.staticEdges.push({from:fid,to:tid,type:'IMPORTS',evidence:'file dependency',provenance:'resolved',source:'structural'});
        });
      });
      [...doc.querySelectorAll('cycles > cycle')].forEach((c,ci)=>{
        const files=[...c.querySelectorAll('f')].map(x=>x.getAttribute('p')).filter(Boolean); const cid='cycle:'+ci;
        sim.staticNodes.set(cid,{id:cid,type:'runtime_error',label:'Dependency cycle '+(ci+1),evidence:'size '+(c.getAttribute('size')||files.length)+' · cost '+(c.getAttribute('cost')||'?')+' · cut '+(c.getAttribute('cut')||'—'),meta:{},source:'structural',x:0,y:0,vx:0,vy:0,r:14});
        files.forEach(path=>{const fid='file:'+path;if(!sim.staticNodes.has(fid))sim.staticNodes.set(fid,{id:fid,type:'file',label:path,evidence:'cycle member',meta:{file:path},source:'structural',x:0,y:0,vx:0,vy:0,r:10});sim.staticEdges.push({from:cid,to:fid,type:'CYCLE',evidence:'dependency SCC',provenance:'resolved',source:'structural'});});
      });
      applyGraph({nodes:[],edges:[]});
    }catch(err){console.warn('[CodeCortex graph] dependency load failed',err);}
  }

  async function loadPath(from,to){
    from=String(from||'').trim();to=String(to||'').trim();if(!from||!to)return;
    try{
      const r=await fetch('/api/graph/path?from='+encodeURIComponent(from)+'&to='+encodeURIComponent(to)+(sim.context.root?'&path='+encodeURIComponent(sim.context.root):''),{cache:'no-store'}),data=await r.json();
      if(!data?.ok)return;
      const doc=parseXml(data.path_xml); if(!doc)return;
      const steps=[...doc.querySelectorAll('path > s')]; let prev=null;
      steps.forEach(st=>{
        const id='symbol:'+String(st.getAttribute('p')||'')+':'+String(st.getAttribute('n')||'');
        sim.staticNodes.set(id,{id,type:'symbol',label:st.getAttribute('n')||id,evidence:st.getAttribute('p')||'',meta:{file:st.getAttribute('p')||''},source:'structural',x:0,y:0,vx:0,vy:0,r:12});
        if(prev)sim.staticEdges.push({from:prev,to:id,type:'CALLS',evidence:'shortest path',provenance:'resolved',source:'structural'}); prev=id;
      });
      sim.mode='code';applyGraph(makeGraph(window.CodeCortexLastEvents||[]));
    }catch(err){console.warn('[CodeCortex graph] path load failed',err);}
  }

  function applyGraph(g){
    const nodeMap=new Map(g.nodes.map(n=>[n.id,n]));
    sim.staticNodes.forEach((n,id)=>{if(!nodeMap.has(id))nodeMap.set(id,{...n});});
    const edgeKey=new Set();
    const edges=[];
    [...g.edges,...sim.staticEdges].forEach(e=>{const k=e.from+'|'+e.to+'|'+e.type;if(!edgeKey.has(k)){edgeKey.add(k);edges.push(e);}});
    sim.nodes=[...nodeMap.values()];sim.edges=edges;sim.byId=new Map(sim.nodes.map(n=>[n.id,n]));
    const oldFilters=sim.filters;sim.filters={};sim.nodes.forEach(n=>{sim.filters[n.type]=oldFilters[n.type]!==false;n._forceHidden=false;});
    sim.onlyFloating=false;sim.running=true;seedPositions();renderSidebar();wake();
  }

  function recenter(){seedPositions();renderSidebar();wake();}
  function setupInteraction(){
    const c=sim.canvas;
    c.addEventListener('mousedown',e=>{const r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;const n=nodeAt(x,y);if(n){sim.dragNode=n;sim.selected=n;renderSidebar();}else{sim.selected=null;renderSidebar();sim.panning=true;sim.panStartX=e.clientX-sim.panX;sim.panStartY=e.clientY-sim.panY;}wake();});
    window.addEventListener('mousemove',e=>{if(!sim.canvas)return;const r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;sim.mouseX=x;sim.mouseY=y;if(sim.dragNode){const p=graphPoint(x,y);sim.dragNode.x=p.x;sim.dragNode.y=p.y;wake();}else if(sim.panning){sim.panX=e.clientX-sim.panStartX;sim.panY=e.clientY-sim.panStartY;renderGraph();}const n=nodeAt(x,y);showTooltip(n,x,y);});
    window.addEventListener('mouseup',()=>{sim.dragNode=null;sim.panning=false;});
    c.addEventListener('mouseleave',()=>showTooltip(null));
    c.addEventListener('wheel',e=>{e.preventDefault();const r=c.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top,before=graphPoint(x,y);const z=e.deltaY<0?1.12:.89;sim.zoom=Math.max(.25,Math.min(3.5,sim.zoom*z));sim.panX=x-before.x*sim.zoom;sim.panY=y-before.y*sim.zoom;renderGraph();},{passive:false});
  }
  function showTooltip(n,x=0,y=0){
    if(!sim.tooltip)return;if(!n){sim.tooltip.classList.remove('visible');return;}
    sim.tooltip.innerHTML='<b>'+esc(n.label)+'</b><span style="color:'+colorFor(n.type)+'">'+esc(n.type.replaceAll('_',' '))+'</span><small>'+esc(short(n.evidence||'',100))+'</small>';
    sim.tooltip.style.left=(x+14)+'px';sim.tooltip.style.top=(y+14)+'px';sim.tooltip.classList.add('visible');
  }

  function update(events,context){
    ensureDom(); if(!sim.canvas)return;
    const next=context||{};
    const changedRoot=(next.root||'')!==(sim.context.root||'');
    sim.context={projectId:next.projectId||'',root:next.root||''};
    if(changedRoot){sim.staticNodes.clear();sim.staticEdges=[];sim.selected=null;sim.graphKey='';}
    window.CodeCortexLastEvents=events||[];
    const g=makeGraph(events||[]),key=g.nodes.map(n=>n.id).join('|')+'#'+g.edges.length+'#'+sim.staticNodes.size+'#'+sim.staticEdges.length;
    if(key===sim.graphKey){renderGraph();return;}
    sim.graphKey=key;applyGraph(g);
  }
  function activate(){ensureDom();resizeCanvas();wake();}
  function zoom(dir){const factor=dir>0?1.18:.85;sim.zoom=Math.max(.25,Math.min(3.5,sim.zoom*factor));renderGraph();}

  window.CodeCortexGraph={update,activate,recenter,zoom};
})();
