/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Saurabh Verma
 *
 * "Get started" view: a plain-English setup checklist computed from what the dashboard can already see
 * (health, telemetry events, price table, memory). Each unmet item says exactly what to do next.
 * Loaded before app.js; app.js's renderAll() calls CodeCortexStart.render(). Uses app.js globals at render time.
 */
(function(){
  let pricingOk=null, autoOpened=false;
  function checkPricing(){
    if(pricingOk!==null)return; pricingOk=false;
    fetch('/pricing.json',{cache:'no-store'}).then(r=>{pricingOk=r.ok; if(window.CodeCortexStart)window.CodeCortexStart.render();}).catch(()=>{pricingOk=false;});
  }
  function item(ok,title,okText,todo,optional){
    const cls=ok?'ok':(optional?'optional':'todo');
    return `<div class="start-item ${cls}"><div class="start-mark">${ok?'✓':(optional?'○':'!')}</div><div><strong>${esc(title)}</strong><p>${ok?okText:todo}</p></div></div>`;
  }
  function render(){
    if(typeof contextEvents!=='function')return;
    const box=$('startChecklist'); if(!box)return;
    checkPricing();
    const ev=state.events||[];
    const root=state.health?.root||state.activeRoot||'';
    const mcpUsed=ev.some(e=>e.type==='context_returned'||(e.type==='tool_call'&&e.source!=='host_reported'));
    const hooks=ev.some(e=>e.source==='host_reported');
    const tokens=ev.some(e=>e.type==='token_usage');
    const host=(ev.slice().reverse().find(e=>e.host_name)||{}).host_name||'';
    box.innerHTML=[
      item(!!state.health?.ok,'CodeCortex is running','This dashboard is live on your computer. Nothing is sent anywhere else.',
        'The dashboard cannot reach CodeCortex. Restart your AI app (Claude Desktop: quit fully from the tray icon, then reopen).'),
      item(!!root,'Project folder',`CodeCortex is reading <code>${esc(root)}</code>.`,
        'No project folder yet. Claude Desktop: Settings → Extensions → CodeCortex → choose your code folder. Claude Code: start it inside your project folder.'),
      item(mcpUsed,'Connected to your AI tool',`Your AI tool${host?` (${esc(host)})`:''} has used CodeCortex.`,
        'Not used yet. In a new chat, type: <b>Show CodeCortex status</b>. If the AI says it has no such tool, reinstall the CodeCortex extension and restart the app.'),
      item(tokens,'Token & cost tracking','Exact token counts per prompt are coming in from Claude Code — see <b>Tokens &amp; cost</b>.',
        hooks?'Claude Code hooks are on; token counts appear after your next prompt finishes.'
             :'Needs <b>Claude Code</b>. In a terminal, run <code>codecortex install --host claude</code> once, then restart Claude Code. (The Claude Desktop app does not share token counts with tools — there you will only see CodeCortex\'s own share.)'),
      item(pricingOk===true,'Price table','List prices are loaded, so costs can be estimated.',
        'Price table not found. Reinstall CodeCortex, or set CODECORTEX_PRICING_FILE to your own pricing.json.'),
      item(typeof memAvailable==='function'&&memAvailable(),'Long-term memory (optional)','Connected: lessons and past findings carry across sessions.',
        'Optional. Works without it. Your team can connect a memory service later.',true)
    ].join('');
    $('startTry').innerHTML=[
      'Use CodeCortex to find where login errors are handled.',
      'Before you change the function processOrder, show me what else would be affected.',
      'Which tests should I run for the changes I just made?',
      'Here is the error I got: (paste it). Use CodeCortex to find the cause.'
    ].map(t=>`<div class="start-try">${esc(t)}</div>`).join('');

    // First visit with nothing set up yet: open this page instead of an empty overview.
    if(!autoOpened){
      autoOpened=true;
      if(!mcpUsed&&!ev.length){ const b=document.querySelector('#nav button[data-view="start"]'); if(b&&state.activeView==='overview')b.click(); }
    }
  }
  window.CodeCortexStart={render};
})();
