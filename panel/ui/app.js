'use strict';
// BEGIN WORKSPACE TABS
globalThis.IZ2S_WORKSPACE = {create({document,api,notice,onSelect,invoke}) {
  const tabs=document.getElementById('workspace-tabs'),home=document.getElementById('workspace-home'),panes=document.getElementById('workspace-tools'),dock=document.getElementById('workspace-dock');
  const rows=new Map(),records=new Map(),seen=new Map(),modules=new Map(),moduleViews=new Set(),closed=new Map();
  let active='home',snapshot,available=true;
  const el=(tag,text,className)=>{const node=document.createElement(tag);if(text)node.textContent=text;if(className)node.className=className;return node;};
  const action=(label,fn,className)=>{const button=el('button',label,className);button.type='button';button.onclick=()=>Promise.resolve().then(fn).catch(error=>notice(error.message));return button;};
  const icon=name=>{const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),use=document.createElementNS(svg.namespaceURI,'use');svg.setAttribute('aria-hidden','true');use.setAttribute('href','#'+name);svg.append(use);return svg;};
  const label=(node,text)=>{if(node.dataset.sourceText!==text){node.dataset.sourceText=text;node.textContent=text;}};
  const statusText=record=>record.pending?'Stopping…':record.opening?'Opening…':record.state==='running'?'Running':record.state==='retained'?'Restart console required':record.state==='stopped'?'Stopped':'Unavailable';
  const moduleTitle=id=>{const module=modules.get(id);return module?.name+([...modules.values()].filter(item=>item.name===module?.name).length>1?' · '+(module.version||id):'');};
  try{for(const [id,value] of JSON.parse(sessionStorage.getItem('iz2s.closedTools')||'[]'))closed.set(id,value);}catch{}
  const remember=()=>{try{sessionStorage.setItem('iz2s.closedTools',JSON.stringify([...closed]));}catch{}};
  const mainWrap=el('div',null,'workspace-tab'),mainTab=action('Overview',()=>select('home'),'workspace-tab-label');
  mainWrap.dataset.tabId='home';mainTab.setAttribute('role','tab');mainTab.id='workspace-tab-home';mainTab.setAttribute('aria-controls','workspace-home');mainWrap.append(mainTab);tabs.append(mainWrap);
  function selection() {
    mainWrap.classList.toggle('active',active==='home');mainTab.setAttribute('aria-selected',String(active==='home'));mainTab.tabIndex=active==='home'?0:-1;
    for(const [id,row]of rows){const selected=id===active;row.pane.hidden=!selected;row.tab.classList.toggle('active',selected);row.button.setAttribute('aria-selected',String(selected));row.button.tabIndex=selected?0:-1;}
    for(const row of dock.children){const record=records.get(row.dataset.workspaceId),selected=row.dataset.workspaceId===active||(active.startsWith('module-info-')&&record?.kind==='module'&&record.moduleId===active.slice(12));row.classList.toggle('active',selected);const button=row.querySelector('button');if(selected)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');}
    tabs.hidden=rows.size===0;document.body.classList.toggle('workspace-active',active!=='home');
  }
  function select(id) {
    if(id!=='home'&&!rows.has(id))return;
    active=id;home.hidden=id!=='home';panes.hidden=id==='home';selection();
    onSelect?.(id==='home'?{id:'home',title:'Overview'}:{id,title:rows.get(id).title});
  }
  function close(id) {
    const row=rows.get(id);if(!row)return;
    const order=['home',...rows.keys()],previous=order[Math.max(0,order.indexOf(id)-1)];
    const record=records.get(id);if(record)closed.set(id,record.requestId);
    remember();row.pane.remove();row.tab.remove();rows.delete(id);if(active===id)select(previous);else selection();renderDock();
  }
  function make(id,title) {
    if(rows.has(id)){const row=rows.get(id);row.title=title;label(row.button,title);row.button.title=title;row.dismiss.setAttribute('aria-label','Close view: '+title);return row;}
    const tab=el('div',null,'workspace-tab'),button=action(title,()=>select(id),'workspace-tab-label'),dismiss=action('×',()=>close(id),'workspace-close'),pane=el('section',null,'workspace-pane');
    tab.dataset.tabId=id;button.setAttribute('role','tab');button.id='workspace-tab-'+id;button.setAttribute('aria-controls','workspace-pane-'+id);button.title=title;
    dismiss.setAttribute('aria-label','Close view: '+title);dismiss.title='Close view; the module keeps running';
    pane.id='workspace-pane-'+id;pane.setAttribute('role','tabpanel');pane.setAttribute('aria-labelledby',button.id);pane.hidden=true;tab.append(button,dismiss);tabs.append(tab);panes.append(pane);
    const row={tab,button,dismiss,pane,title};rows.set(id,row);selection();return row;
  }
  function shareLanguage(frame) {
    const language=globalThis.RUN21_LANGUAGE?.id;
    if(language)frame.contentWindow?.postMessage({type:'iz2s:workspace-language',language},new URL(frame.src).origin);
  }
  function renderRecord(record) {
    const row=make(record.id,record.title),ready=record.state==='running',retained=record.state==='retained';
    if(!row.content){
      row.toolbar=el('div',null,'workspace-tool-bar');row.status=el('span',null,'workspace-status');row.status.setAttribute('role','status');
      row.reload=action('Reload view',async()=>{const current=records.get(record.id);if(current?.state==='running'){if(row.frame)row.frame.src=row.frame.src;}else await startRecord(record.id);},'secondary');
      row.stop=action('Stop module',()=>stopRecord(record.id),'workspace-stop');
      const controls=el('div',null,'workspace-tool-actions');controls.append(action('Close view',()=>close(record.id),'secondary'),row.reload,row.stop);row.toolbar.append(row.status,controls);
      row.content=el('div',null,'workspace-content');row.pane.replaceChildren(row.toolbar,row.content);
    }
    label(row.status,statusText(record));row.status.dataset.state=record.state;
    label(row.reload,ready?'Reload view':'Start / open');row.reload.disabled=!!record.pending||!!record.opening||retained||(!available&&!ready)||(!ready&&!!snapshot?.preview);
    label(row.stop,record.pending?'Stopping…':record.kind==='settings'?'Stop settings':'Stop module');row.stop.disabled=!!record.pending||!!record.opening||retained||record.state==='stopped'||(!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;
    const signature=record.url+'|'+record.state;
    if(row.signature===signature)return row;
    row.signature=signature;row.content.replaceChildren();row.frame=null;
    if(!ready)row.content.append(el('p',retained?'This module could not confirm native cleanup. Restart the console before starting it again.':'The interface is stopped. Use Start / open to return.','workspace-empty'));
    else{
      const frame=el('iframe'),target=new URL(record.url);if(record.kind==='files')target.searchParams.set('embedded','1');
      frame.name='iz2s-workspace';frame.title=record.title;frame.src=target.href;frame.referrerPolicy='no-referrer';
      frame.setAttribute('allow','fullscreen; display-capture; gamepad; clipboard-write');
      frame.setAttribute('sandbox','allow-scripts allow-same-origin allow-forms allow-downloads allow-modals allow-popups allow-pointer-lock');
      frame.onload=()=>shareLanguage(frame);row.frame=frame;row.content.append(frame);
    }
    return row;
  }
  async function startRecord(id) {
    const record=records.get(id);if(!record||record.pending||record.opening||!available)return;
    const previousClose=closed.get(id);
    record.opening=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/open',{id});if(result.tab&&records.get(id)?.requestId===record.requestId){const dismissed=closed.get(id)!==previousClose;if(dismissed){closed.set(id,result.tab.requestId);remember();}openRequest(result.tab,!dismissed);}}
    finally{const current=records.get(id);if(current){current.opening=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  async function stopRecord(id) {
    const record=records.get(id);if(!record||record.pending||record.opening||!available)return;
    record.pending=true;renderDock();if(rows.has(id))renderRecord(record);
    try{const result=await api('/api/workspace/stop',{id});if(!records.has(id)||records.get(id).requestId!==record.requestId)return;const current=result.tab||records.get(id);if(!result.tab)current.state='stopped';records.set(id,{...current,title:record.title,pending:true});if(rows.has(id))renderRecord(records.get(id));}
    finally{const current=records.get(id);if(current){current.pending=false;if(rows.has(id))renderRecord(current);}renderDock();}
  }
  function openRequest(record,force=false) {
    if(!record||!/^[-a-z0-9]{1,80}$/.test(record.id))throw Error('Invalid module tab');
    const url=new URL(record.url);if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||url.username||url.password)throw Error('Module interface must be local');
    const previous=records.get(record.id);record={...record,...(record.kind==='files'?{title:'Super File Bros.'}:{}),pending:previous?.pending,opening:previous?.opening};
    records.set(record.id,record);const fresh=seen.get(record.id)!==record.requestId;seen.set(record.id,record.requestId);
    if(force||fresh&&!record.opening&&closed.get(record.id)!==record.requestId&&record.state!=='stopped'){closed.delete(record.id);remember();renderRecord(record);select(record.id);}
    else if(rows.has(record.id))renderRecord(record);
    renderDock();
  }
  function dockRow(id,title,status,callback,kind) {
    const row=el('div',null,'workspace-dock-row'),open=action('',callback,'workspace-dock-open'),text=el('span',null,'workspace-dock-text');
    row.dataset.workspaceId=id;open.title=title;text.append(el('span',title,'workspace-dock-name'));open.append(icon(kind==='files'?'i-folder':'i-grid'),text);row.append(open,el('small',status,'workspace-dock-state'));return {row,open};
  }
  function renderDock() {
    const stamp=JSON.stringify([available,!!snapshot?.preview,[...records.values()].map(r=>[r.id,r.title,r.url,r.requestId,r.state,r.pending,r.opening,r.moduleId]),[...moduleViews].map(id=>[id,moduleTitle(id),snapshot?.installed,snapshot?.moduleStates])]);
    if(dock.dataset.stamp===stamp){selection();return;}dock.dataset.stamp=stamp;dock.replaceChildren();
    for(const record of records.values()){
      const {row,open}=dockRow(record.id,record.title,statusText(record),()=>['running','retained'].includes(record.state)?openRequest(record,true):startRecord(record.id),record.kind),stop=action('Stop',()=>stopRecord(record.id),'workspace-dock-stop');
      stop.setAttribute('aria-label','Stop '+record.title);stop.title='Stop '+record.title;stop.disabled=!!record.pending||!!record.opening||['stopped','retained'].includes(record.state)||(!record.moduleId&&record.kind!=='files')||!available||!!snapshot?.preview;open.disabled=!!record.pending||!!record.opening;row.append(stop);dock.append(row);
    }
    for(const id of moduleViews){
      const module=modules.get(id);if(!module||[...records.values()].some(record=>record.moduleId===id&&record.kind==='module'))continue;
      const loaded=snapshot?.installed?.some(item=>(typeof item==='string'?item:item.id)===id),running=snapshot?.moduleStates?.some(item=>item.id===id&&item.state==='running');
      dock.append(dockRow('module-info-'+id,moduleTitle(id),running?'Running':loaded?'Loaded on console':'Not running',()=>openModule(id),'module').row);
    }
    dock.hidden=!dock.childElementCount;document.getElementById('workspace-dock-title').hidden=dock.hidden;selection();
  }
  function moduleContent(id) {
    const module=modules.get(id),row=rows.get('module-info-'+id);if(!module||!row)return;
    row.pane.replaceChildren();row.pane.classList.add('workspace-module-details');
    const loaded=snapshot?.installed?.some(item=>(typeof item==='string'?item:item.id)===id),running=snapshot?.moduleStates?.some(item=>item.id===id&&item.state==='running'),toolbar=el('div',null,'workspace-tool-bar'),controls=el('div',null,'workspace-tool-actions'),status=el('span',row.modulePending?'Operation in progress…':running?'Running':loaded?'Loaded on console':'Not running','workspace-status');
    status.dataset.state=running?'running':'stopped';status.setAttribute('role','status');row.pane.setAttribute('aria-busy',String(!!row.modulePending));toolbar.append(status,controls);
    const taskButton=(title,fn,disabled=false)=>{const button=action(title,async()=>{if(row.modulePending)return;row.modulePending=true;moduleContent(id);try{await fn();}finally{row.modulePending=false;if(rows.has('module-info-'+id))moduleContent(id);}},'secondary');button.disabled=disabled||!!row.modulePending;controls.append(button);};
    if(module.switch)taskButton(loaded?'Remove from console':'Load on console',()=>invoke(loaded?'/api/unload':'/api/load',{ids:[id]}),!available||!snapshot?.connected||!!snapshot?.preview||!!(snapshot?.platform?.residentGraphics&&snapshot?.active));
    if(module.server&&!module.switch)taskButton(running?'Stop module':'Start module',()=>invoke(running?'/api/module/stop':'/api/module/start',{id}),!available||!!snapshot?.preview);
    if(module.server?.settings)taskButton(module.server.settings.label||'Settings',async()=>{const result=await api('/api/module/settings',{id});openRequest(result.tab,true);},!available||!!snapshot?.preview);
    const content=el('div',null,'workspace-module-copy');content.append(el('h1',module.name),el('p',module.description||'','subtitle'));
    if(module.switch)content.append(el('p','This module runs on the console. Use this tab to load or remove it.','hint'));
    row.pane.append(toolbar,content);
  }
  function openModule(id) {
    if(!modules.has(id))throw Error('Module is no longer installed');
    const record=[...records.values()].find(record=>record.moduleId===id&&record.kind==='module');if(record){openRequest(record,true);return rows.get(record.id);}
    const row=make('module-info-'+id,moduleTitle(id));moduleViews.add(id);moduleContent(id);select('module-info-'+id);renderDock();return row;
  }
  function updateModules(state) {
    const stamp=JSON.stringify([state.moduleCatalog,state.moduleStates,state.installed,state.connected,state.preview,state.platform?.residentGraphics]);
    const ended=snapshot?.connected&&(!state.connected||snapshot.active?.id!==state.active?.id);
    snapshot=state;if(updateModules.stamp===stamp&&!ended)return;updateModules.stamp=stamp;
    if(ended)for(const id of [...moduleViews]){moduleViews.delete(id);close('module-info-'+id);}
    modules.clear();for(const module of state.moduleCatalog||[])modules.set(module.id,module);
    for(const id of moduleViews){if(!modules.has(id)){close('module-info-'+id);moduleViews.delete(id);}else if(rows.has('module-info-'+id)){make('module-info-'+id,moduleTitle(id));moduleContent(id);}}
    for(const [id,record]of records)if(rows.has(id))renderRecord(record);renderDock();
  }
  function setAvailable(value){if(available===value)return;available=value;for(const [id,record]of records)if(rows.has(id))renderRecord(record);for(const id of moduleViews)if(rows.has('module-info-'+id))moduleContent(id);renderDock();}
  function sync(entries){
    const present=new Set(entries.map(entry=>entry.id));
    for(const id of [...records.keys()])if(!present.has(id)){
      records.delete(id);seen.delete(id);closed.delete(id);close(id);
    }
    remember();for(const entry of entries)openRequest(entry);renderDock();
  }
  tabs.addEventListener('keydown',event=>{
    if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;
    const buttons=[...tabs.querySelectorAll('[role=tab]')],index=buttons.indexOf(document.activeElement);if(index<0)return;event.preventDefault();
    const next=event.key==='Home'?0:event.key==='End'?buttons.length-1:(index+(event.key==='ArrowRight'?1:-1)+buttons.length)%buttons.length;buttons[next].click();buttons[next].focus();
  });
  window.addEventListener('run21:languagechange',()=>{for(const row of rows.values())if(row.frame)shareLanguage(row.frame);});
  window.addEventListener('message',event=>{if(event.data?.type!=='iz2s:workspace-ready')return;for(const row of rows.values())if(row.frame&&event.source===row.frame.contentWindow&&event.origin===new URL(row.frame.src).origin)shareLanguage(row.frame);});
  select('home');return{select,openRequest,openModule,updateModules,sync,close,setAvailable};
}};
// END WORKSPACE TABS
// BEGIN GENERATED RUN21_CATALOG
const RUN21_CATALOG = (function createPolicy() {
  const hidden = new Set([
    'cpu-mailbox-lab',
    'cpu-mailbox-lab-v2',
    'cpu-mailbox-lab-v3',
    'ims-driver-cache-probe',
    'ims-gpu-pcm-cityfast',
    'ims-music-packed-lab',
    'izbrowser-check-20261101',
    'izbrowser-offline-v2-20261101',
    'izbrowser-offline-v3-20261101',
    'izbrowser-offline-v4-20261101',
    'izbrowser-wifi-20261101',
    'izdoom-native18',
    'izdoom-wide18',
    'izoff-012',
    'izsonic-transfer18',
    'ns1-core-constructor-check',
    'ns1-memory-budget-check',
    'ns1-memory-budget-extended-check',
    'ns1-native-cached-memory-check',
    'ns1-paged-core-check',
    'ns1-paged-core-current-check',
    'ns1-paged-kernel-check',
    'ns1-paged-kernel-current-check',
    'ns1-runtime-check',
    'ns1-runtime-tls-check',
    'ns1-vulkan-capabilities-check',
    'ns1-vulkan-memory-check',
    'ns1-vulkan-pool-check',
    'ps2-cpu-interpreter-check',
    'ps2-cpu-lightweight-check',
  ]);
  const hiddenEngines = new Set(['pepsipu', 'izmysh-resume']);
  const izEngines = new Set(['izmysh', 'bermthesequel']);
  const izVariants = new Set(['izlauncher', 'izpslauncher']);

  function visible(descriptor) {
    return !hidden.has(descriptor.id);
  }

  function engineAvailable(loader, variant) {
    if (!loader || hiddenEngines.has(loader.id) || loader.supported === false) return false;
    const declared = Array.isArray(loader.availableIn) ? loader.availableIn
      : Array.isArray(loader.modes) ? loader.modes : null;
    if (variant === 'original') return !declared || declared.includes('original');
    if (!izVariants.has(variant) || !izEngines.has(loader.id)) return false;
    // Older APIs declared only the common IZLauncher transport mode.
    return !declared || declared.some(mode => izVariants.has(mode));
  }

  function normalizeSnapshot(snapshot) {
    if (!snapshot || typeof snapshot !== 'object' || Array.isArray(snapshot)) return snapshot;
    const result = {...snapshot};
    if (Array.isArray(snapshot.library)) result.library = snapshot.library.filter(visible);
    if (Array.isArray(snapshot.moduleCatalog)) result.moduleCatalog = snapshot.moduleCatalog.filter(visible);
    if (snapshot.platform && Array.isArray(snapshot.platform.loaders)) {
      result.platform = {
        ...snapshot.platform,
        loaders: snapshot.platform.loaders.filter(loader => !hiddenEngines.has(loader.id)).map(loader => ({
          ...loader,
          availableIn: ['original', 'izlauncher', 'izpslauncher'].filter(variant => engineAvailable(loader, variant)),
        })),
      };
    }
    // Active sessions, installed apps, jobs, loaderId and connection state stay intact.
    return result;
  }

  return {visible, engineAvailable, normalizeSnapshot};
})();
// END GENERATED RUN21_CATALOG
if (new URLSearchParams(location.search).get('ui-preview') === 'files') {
  document.documentElement.lang = 'en';
  document.title = 'Filesystem · IZ2S preview';
  document.getElementById('panel-startup').hidden = true;
  document.getElementById('manual-files-document').hidden = false;
} else {
const $ = (id) => document.getElementById(id),
  key = location.hash.slice(1),
  selection = new Set();
let hardStopping = false;
let stopTask = null, pendingStart = false, actionGeneration = 0;
let state = null,
  serverUnavailable = false,
  step = 'launch',
  busy = false,
  timer = null,
  notificationKey = '',
  dismissed = '',
  packageStamp = '',
  requestId = null,
  finishedAttempt = '',
  setupStamp = '',
  resultStamp = '',
  pendingSetup = null,
  logFollow = true,
  logWriting = false,
  backendNotice = '';
const startupSelection = new Set();
let startupStamp = '';
// The manual-input design is a local UI preview, separate from session settings.
let inputDesign = 'arm', manualDesignStage = 1;
const advSetup = globalThis.IZ2S_ADV_SETUP.create({document, changed:()=>{if(state)renderWizard();}, openSetup:()=>chooseStep('bootstrap')});
let workspaceReady = false;
const workspace = globalThis.IZ2S_WORKSPACE.create({document,api,notice,invoke:act,onSelect:tab=>{
  const selected=document.querySelector('.view:not([hidden])')?.id||'setup';
  const title=tab.id==='home'?({setup:'Connection',apps:'App library',history:'Failed attempts'}[selected]||'IZ2S'):tab.title;
  document.querySelectorAll('.sidebar [data-view]').forEach(button=>{const current=tab.id==='home'&&button.dataset.view===selected;button.classList.toggle('active',current);if(current)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');});
  $('view-title').textContent=title;document.title=title+' · IZ2S Platform';
}});
const manualDesignConfirmed = new Set();
try {
  for (const id of JSON.parse(
    (localStorage.getItem('iz2s.startupModules') ?? localStorage.getItem('iz15.startupModules')) || '[]',
  ))
    startupSelection.add(id);
} catch {}
function saveStartup() {
  localStorage.setItem(
    'iz2s.startupModules',
    JSON.stringify([...startupSelection]),
  );
  localStorage.setItem(
    'iz2s.startupEnabled',
    $('preload-enabled').checked ? '1' : '0',
  );
}
try {
  $('preload-enabled').checked =
    (localStorage.getItem('iz2s.startupEnabled') ?? localStorage.getItem('iz15.startupEnabled')) === '1';
} catch {}
const words = {
  server: ['Server x64 PC', 'i-server'],
  arm: ['ARM PC', 'i-arm'],
  usb: ['ARM Type C - Switch', 'i-usb'],
  lan: ['Console', 'i-lan'],
};
const element = (tag, className, text) => {
  const e = document.createElement(tag);
  if (className) e.className = className;
  if (text !== undefined) e.textContent = text;
  return e;
};
function setText(node, value) {
  const text = String(value ?? '');
  if (node.textContent !== text) node.textContent = text;
}
function placeChild(parent, child, index) {
  if (parent.children[index] !== child)
    parent.insertBefore(child, parent.children[index] || null);
}
function folderButton(kind, id, text = 'Open folder') {
  const button = element('button', 'quiet folder-button');
  button.type = 'button';
  button.innerHTML = '<svg aria-hidden="true"><use href="#i-folder"/></svg>';
  button.append(element('span', '', text));
  button.setAttribute('aria-label', text);
  button.title = text;
  button.disabled = serverUnavailable || !!state?.preview;
  button.onclick = async event => {
    event.preventDefault();
    event.stopPropagation();
    button.dataset.pending = 'true';
    button.disabled = true;
    try { await api('/api/folder/open', { kind, ...(id ? { id } : {}) }); }
    catch (error) { notice(error.message); }
    finally { delete button.dataset.pending; renderLocalActionAvailability(); }
  };
  return button;
}
const date = (value) =>
  value ? new Date(value).toLocaleString(globalThis.RUN21_LANGUAGE?.locale || 'en-GB', { hour12: false }) : '';
async function api(route, data, options = {}) {
  // Only status reads have a deadline; actions may still be running on the server.
  const controller = data === undefined && options.timeoutMs > 0 ? new AbortController() : null;
  const deadline = controller ? setTimeout(() => controller.abort(), options.timeoutMs) : null;
  try {
    const res = await fetch(route, {
      method: data === undefined ? 'GET' : 'POST',
      headers: {
        'x-iz-key': key,
        ...(data === undefined ? {} : { 'content-type': 'application/json' }),
      },
      ...(data === undefined ? {} : { body: JSON.stringify(data) }),
      ...(controller ? { signal: controller.signal } : {}),
    });
    const result = await res.json();
    if (!res.ok) throw Error(result.error || 'The server did not respond');
    return result;
  } finally {
    if (deadline !== null) clearTimeout(deadline);
  }
}
function notice(message, type = 'error', identity = message) {
  if (identity === dismissed) return;
  notificationKey = identity;
  $('toast').hidden = false;
  $('toast').classList.toggle('success', type === 'success');
  $('toast-title').textContent =
    type === 'success' ? 'All set.' : 'Something needs attention.';
  $('toast-text').textContent = message;
  $('toast').querySelector('.toast-symbol').textContent =
    type === 'success' ? '✓' : '!';
}
async function act(route, data = {}) {
  if (route === '/api/stop') return stopSession();
  if (inputDesign === 'own' && !['/api/stop', '/api/hard-stop', '/api/dismiss'].includes(route)) {
    notice('This is a manual input preview. Changes are not sent to the device.');
    return;
  }
  if (busy) return;
  const generation = ++actionGeneration;
  busy = true;
  pendingStart = route === '/api/start';
  renderControls();
  try {
    const result = await api(route, data);
    if (generation === actionGeneration) await poll();
    return generation === actionGeneration ? result : undefined;
  } catch (e) {
    if (generation === actionGeneration) notice(e.message);
  } finally {
    if (generation === actionGeneration) {
      busy = false;
      pendingStart = false;
      renderControls();
    }
  }
}
function stopSession() {
  if (stopTask) return stopTask;
  // STOP bypasses queued server work. Keep it reachable during a pending
  // launch, and ignore that request's eventual UI result after cancellation.
  actionGeneration++;
  busy = true;
  pendingStart = false;
  $('screen-ready').checked = false;
  document.querySelectorAll('.up-ready,.up-source-ready').forEach(input => {input.checked = false;});
  stopTask = Promise.resolve().then(async () => {
    try {
      const result = await api('/api/stop', {});
      await poll();
      return result;
    } catch (error) {notice(error.message);}
    finally {stopTask = null; busy = false; renderControls();}
  });
  renderControls();
  return stopTask;
}
function switchView(id) {
  if (!['setup', 'apps', 'history'].includes(id)) return;
  workspace.select('home');
  const previous = document.querySelector('.view:not([hidden])')?.id;
  if (inputDesign === 'own' && id !== 'setup') {
    inputDesign = 'arm';
    if (state) { renderDevices(); renderControls(); }
  }
  document.querySelectorAll('.view').forEach((e) => (e.hidden = e.id !== id));
  document
    .querySelectorAll('.sidebar [data-view]')
    .forEach((e) => {const current=e.dataset.view===id;e.classList.toggle('active',current);if(current)e.setAttribute('aria-current','page');else e.removeAttribute('aria-current');});
  const title = { setup: 'Connection', apps: 'App library', history: 'Failed attempts' }[id] || 'IZ2S Platform';
  $('view-title').textContent = title;
  document.title = title + ' · IZ2S Platform';
  if (previous !== id) window.scrollTo({ top: 0, left: 0, behavior: 'auto' });
}
function chooseStep(value) {
  step = value;
  requestId = null;
  $('screen-ready').checked = false;
  document
    .querySelectorAll('[data-step]')
    .forEach((e) => e.classList.toggle('selected', e.dataset.step === step));
  renderControls();
  if (state) renderWizard();
}
function fieldsDirty(container, values) {
  return Object.entries(fieldValues(container)).some(
    ([name, value]) => String(value) !== String(values?.[name] ?? ''),
  );
}
function deliveryDirty() {
  return (
    !!state?.platform?.izModules &&
    fieldsDirty($('delivery-fields'), state.platform.loaderSettings)
  );
}
function renderControls() {
  document.querySelectorAll('.segmented button,[data-step]').forEach(button=>button.setAttribute('aria-pressed',String(button.classList.contains('selected'))));
  if(state?.platform)renderArmSetup(state.platform);
  renderPlatformControls();
  const setup = state?.active?.kind === 'bootstrap' ? state.active.setup : null,
    reboot = setup?.stage === 'reboot',
    active = !!state?.active,
    ready = !!state?.checks.ready,
    ack = $('screen-ready').checked,
    initial = !active || reboot,
    retryRequired = false;
  const engineAllowed = !state?.platform || RUN21_CATALOG.engineAvailable(
    state.platform.loaders.find(loader => loader.id === state.platform.loaderId),
    state.platform.variant,
  );
  const residentLaunch=initial&&(step==='launch'||reboot)&&state?.platform?.residentGraphics===true&&['izlauncher','izpslauncher'].includes(state?.platform?.variant);
  const startupIds=$('preload-enabled').checked?[...startupSelection]:[];
  const residentBad=residentLaunch?startupIds.map(id=>state.library.find(p=>p.id===id)).filter(p=>!residentEligibility(p)?.compatible):[];
  const residentAudioCount=startupIds.map(id=>(state?.library||[]).find(p=>p.id===id)).filter(p=>residentEligibility(p)?.code==='finite-audio-candidate').length;
  const residentIssue=residentLaunch?(residentAudioCount>1?'Select one qualified game for retained audio.':(state.library.some(p=>!residentEligibility(p))?'Restart the panel to enable module compatibility checks.':residentBad.length?'Remove incompatible apps from the retained set: '+residentBad.map(p=>p?.name||'unknown module').join(', ')+'.':!startupIds.length?'Select at least one compatible app at startup.':'')):'';
  $('recovery-option').hidden =
    step !== 'bootstrap' || !initial || !state?.recoverySetupAvailable;
  $('recovery-enabled').disabled =
    busy || !initial || !state?.recoverySetupAvailable;
  if (state && !state.recoverySetupAvailable)
    $('recovery-enabled').checked = false;
  $('launch-modules').hidden = step !== 'launch' && !reboot;
  $('preload-options').hidden = !$('preload-enabled').checked;
  $('preload-enabled').disabled = busy || !initial;
  $('preload-clear').disabled = busy || !initial;
  document
    .querySelectorAll('#preload-list input')
    .forEach((e) => (e.disabled = busy || !initial || e.dataset.unavailable === 'true' || (e.dataset.residentUnavailable === 'true'&&!e.checked)));
  $('preload-count').textContent = startupSelection.size + ' selected';
  $('start').disabled =
    busy ||
    !!state?.emergencyInputPending ||
    !initial ||
    !ready ||
    !ack ||
    !!state?.preview ||
    !state?.setupWizard ||
    !engineAllowed ||
    !!residentIssue ||
    deliveryDirty();
  $('start-label').textContent =
    reboot || step === 'launch'
      ? 'Load ' + selectedShellName()
      : 'Install ' +
        ($('recovery-enabled').checked && state?.recoverySetupAvailable
          ? state?.platform?.delivery?.recoveryBootstrapName || 'NS2R'
          : state?.platform?.delivery?.baseBootstrapName || 'NS2');
  $('start').hidden = !initial || retryRequired;
  $('retry').hidden = !retryRequired;
  $('session-confirmation').hidden = retryRequired;
  $('retry').textContent = state?.lastAttempt?.kind === 'bootstrap'
    ? 'Restart First Setup' : 'Return to Launch';
  $('gate-message').textContent = retryRequired
    ? state.lastAttempt.kind === 'bootstrap'
      ? 'Return to setup, then confirm the screen before continuing.'
      : 'Return to the form, then confirm the screen before loading.'
    : !engineAllowed
    ? 'Choose IZ2S Core or ns2jb by BermTheSequel, or use Core only.'
    : deliveryDirty()
    ? 'Saving session settings…'
    : residentIssue
      ? residentIssue
    : state?.emergencyInputPending
      ? 'Check the ARM PC connection. Press red STOP to retry input cancellation.'
      : !state?.setupWizard
        ? 'Restart Platform to activate the updated setup flow'
        : active && !reboot
          ? 'Leave the console on the indicated screen.'
          : state?.preview
            ? 'Preview · device commands disabled'
            : !ready
              ? 'Waiting for '+(state?.checks?.items?.length || 0)+' device checks'
              : !ack
                ? 'Confirm the console screen to continue'
                : 'All checks passed · ready to start';
  $('hard-stop').hidden = !state?.emergencyInputPending && !state?.active?.stopTimedOut;
  $('hard-stop').disabled = hardStopping || !state?.hardStopAvailable;
  $('stop').hidden = !active && !pendingStart && !stopTask;
  $('stop').disabled = !!stopTask || state?.active?.status === 'stopping';
  $('stop').textContent =
    stopTask || state?.active?.status === 'stopping'
      ? 'Disconnecting…'
      : state?.active?.kind === 'bootstrap'
        ? 'Cancel setup'
        : 'Stop and return to Launch';
  $('screen-ready').disabled = !initial || busy;
  $('screen-ready').parentElement.hidden = !initial;
  document
    .querySelectorAll('[data-step]')
    .forEach((e) => (e.disabled = active || busy));
  $('recheck').disabled = !!state?.active?.inputActive || busy;
  $('load').disabled =
    busy || !state?.connected || !selection.size || !!state?.preview;
  document.querySelectorAll('.module-host-actions button').forEach(button => {
    const source = button.closest('.local-module')?.querySelector('.local-source');
    button.disabled = busy || !!state?.preview || button.dataset.unavailable === 'true' || (!!source && !source.value.trim());
  });
  document
    .querySelectorAll(
      '.transport-choice button,.transport-choice input,.address-form input,.address-form select,.address-form button,.detected-peer',
    )
    .forEach(
      (e) =>
        (e.disabled =
          busy ||
          !state?.connections ||
          !!state?.connections?.locked ||
          !!state?.preview ||
          e.dataset.unavailable === 'true' || e.dataset.fixed === 'true'),
    );
  const hidDisabled = busy || !ready || !!state?.preview || !state?.setupWizard;
  $('verify-source').disabled = hidDisabled || !$('review-ready').checked;
  $('skip-source').disabled =
    busy || !$('review-ready').checked || !state?.setupWizard;
  $('save-source').disabled = hidDisabled;
  $('compare-source').disabled =
    busy ||
    !$('check-lines').checked ||
    !Array.from({ length: 6 }, (_, i) => $('metric-' + i).value).every((s) =>
      /^[0-9]{1,8}$/.test(s),
    );
  $('reverify-source').disabled = hidDisabled;
  $('confirm-saved').disabled = busy || !$('saved-ready').checked;
  $('retry').disabled = busy;
  $('restart-setup').disabled = busy;
  $('edit-readings').disabled = busy;
  renderManualInputDesign();
  advSetup.render({state,step,busy,serverUnavailable,manual:inputDesign==='own'});
  renderServerAvailability();
}
function renderWizard() {
  if (state?.platform && !state.platform.izModules) {
    renderPlatformControls();
    renderEngineInstall();
    return;
  }
  const row = state.active;
  if (row?.kind === 'launch') {
    step = 'launch';
    document
      .querySelectorAll('[data-step]')
      .forEach((e) =>
        e.classList.toggle('selected', e.dataset.step === 'launch'),
      );
  }
  const setup = row?.kind === 'bootstrap' ? row.setup : null,
    stage = setup?.stage || 'prepare',
    show = step === 'bootstrap' || !!setup;
  $('wizard-results').hidden = !setup?.comparison || !['mismatch', 'save'].includes(stage);
  $('wizard-steps').hidden = !show || state.platform?.configuration?.inputAdapter === 'cardputer-wifi';
  renderInstallationSlot();
  const source =
      setup ||
      ($('recovery-enabled').checked && state.recoverySetupAvailable
        ? state.recoverySource
        : state.platform?.baseSource),
    lines = source?.lines || 229,
    sourceSlot = source?.sourceSlot ?? 0,
    name =
      setup?.name ||
      ($('recovery-enabled').checked && state.recoverySetupAvailable
        ? state.platform?.delivery?.recoveryBootstrapName || 'NS2R'
        : state.platform?.delivery?.baseBootstrapName || 'NS2');
  document.querySelector('[data-phase=typing]').textContent = lines + ' lines';
  document.querySelector('[data-phase=save]').textContent = 'Save ' + name;
  for (let i = 0; i < 3; i++) {
    const part = source?.parts?.[i];
    if (part)
      document.querySelector('label[for=metric-' + i * 2 + ']').textContent =
        i + 1 + ' · lines ' + part.first + '-' + part.last;
  }
  $('check-lines').nextElementSibling.textContent =
    'The check finished without errors. SOURCE LINES shows ' +
    lines +
    ', or ' +
    (lines + 1) +
    ' with only one empty final line.';
  $('save-source').firstChild.textContent = 'Save as ' + name + ' ';
  $('saved-ready').nextElementSibling.textContent =
    name + ' was saved successfully on the console.';
  $('recovery-status').hidden = !state.recoverySetupAvailable;
  $('recovery-status').textContent =
    state.link?.state === 'waiting-for-dock'
      ? 'Connection interrupted · waiting for the console to reconnect.'
      : state.link?.protocol === 2
        ? 'Automatic connection recovery is active.'
        : state.active?.kind === 'launch'
          ? 'This session uses the original NS2. Install NS2R to enable recovery.'
          : state.bootstrapProfile?.recovery
            ? (state.platform?.delivery?.bootstrapName || 'NS2R') +
              ' is selected for the next launch.'
            : 'Connection recovery needs a one-time NS2R installation.';
  const descriptions = {
    prepare:
      'In SmileBASIC, choose Make a Program and open Direct mode (the black console screen). Slot 0 must be empty. I will enter LIST 0:1 first, then the original 229 lines.',
    typing:
      'Entering the original source into EDIT0. Keep SmileBASIC open and do not switch screens.',
    review:
      'Are all 229 source lines present, with END on the last source line? An empty line 230 after the final Enter is normal. The three-part source check is optional.',
    checking:
      'Switching to Direct automatically when needed, then entering ordinary read-only source checks. The bootstrap is not executed.',
    values:
      'Read the six numbers from the Switch. The PC cannot read or validate the console output automatically.',
    mismatch:
      'Some source readings differ. Review the marked part and line range. Download a diagnostic for analysis, correct the readings if mistyped, or start again.',
    save: 'Ready to save the source as NS2. Saving is a separate action and never starts the bootstrap.',
    saving:
      'Sending the save command in Direct mode. Confirm any on-console Save / Overwrite dialog.',
    save_confirmation:
      'Has SmileBASIC saved NS2 successfully? Confirm the result on the Switch before continuing.',
    reboot:
      'Fully restart the Switch. Then open SmileBASIC → Make a Program → Direct (the black screen). Only confirm the checkbox below after this is done.',
  };
  if (show) {
    $('step-description').textContent = (
      descriptions[stage] || descriptions.prepare
    )
      .replaceAll('229', String(lines))
      .replaceAll('230', String(lines + 1))
      .replaceAll('original ', name === 'NS2R' ? 'recovery ' : 'original ')
      .replaceAll('as NS2.', 'as ' + name + '.')
      .replaceAll('saved NS2 successfully', 'saved ' + name + ' successfully')
      .replaceAll('Slot 0', 'Slot ' + sourceSlot)
      .replaceAll('LIST 0:1', 'LIST ' + sourceSlot + ':1')
      .replaceAll('EDIT0', 'EDIT' + sourceSlot);
    $('screen-label').textContent =
      stage === 'reboot'
        ? 'I fully restarted the Switch and opened fresh SmileBASIC Direct.'
        : 'I opened fresh SmileBASIC → Make a Program → Direct, slot ' +
          sourceSlot +
          ' is empty, keyboard is English (US), Caps Lock is off.';
  } else {
    $('step-description').textContent =
      row?.kind === 'launch'
          ? 'Keep SmileBASIC open. The original launch sequence is running; no memory preparation is repeated automatically.'
          : 'Use the program already saved on your console. Fully restart the Switch and open fresh SmileBASIC Direct before launching.';
    $('screen-label').textContent = state.platform?.delivery?.preloaded
      ? 'After a full restart, the bootstrap is loaded in slot 0 and Direct is open; RUN and REMOVE have not been executed.'
      : 'I fully restarted the Switch and opened fresh SmileBASIC Direct.';
  }
  if (state.platform?.configuration?.inputAdapter === 'cardputer-wifi') {
    const capsOn = state.platform.profiles?.[state.platform.configurationId]?.cardputerCapsLock === 'on';
    const brief = {
      prepare:capsOn
        ? 'Open SmileBASIC Direct. Use an empty slot, English (US), Caps Lock on.'
        : 'Open SmileBASIC Direct. Use an empty slot, English (US), Caps Lock off.',
      typing:'ADV is entering the preloaded source. Keep SmileBASIC open.',
      review:'Check the source on Switch 2, then save it.',
      checking:'ADV is entering the optional source check.',
      values:'Enter the six numbers shown on Switch 2.',
      mismatch:'The readings differ. Check the source or enter the readings again.',
      save:'Save the bootstrap on Switch 2.',
      saving:'Wait for saving to finish on Switch 2.',
      save_confirmation:'Confirm the saved file on Switch 2.',
      reboot:'Fully restart Switch 2 and open fresh SmileBASIC Direct.',
    };
    $('step-description').textContent = show ? brief[stage] || brief.prepare : 'ADV: Wi-Fi on, USB to Switch 2. Restart Switch 2 and open fresh Direct.';
    $('screen-label').textContent = show && stage !== 'reboot'
      ? 'Direct is open; slot ' + sourceSlot + ' is empty; English (US), Caps Lock ' + (capsOn ? 'on.' : 'off.')
      : 'Switch 2 was fully restarted; fresh Direct is open.';
  }
  const stamp = (row?.id || 'none') + ':' + stage;
  if (stamp !== setupStamp) {
    const previous = setupStamp.split(':').at(-1);
    setupStamp = stamp;
    if (stage === 'review') $('review-ready').checked = false;
    if (stage === 'values') {
      $('check-lines').checked = false;
      if (previous === 'checking')
        for (let i = 0; i < 6; i++) $('metric-' + i).value = '';
    }
    if (stage === 'save_confirmation') $('saved-ready').checked = false;
    if (stage === 'reboot') {
      requestId = null;
      $('screen-ready').checked = false;
    }
  }
  $('wizard-review').hidden = !setup || stage !== 'review';
  $('wizard-values').hidden = !setup || stage !== 'values';
  $('wizard-save').hidden = !setup || stage !== 'save';
  $('wizard-saved').hidden = !setup || stage !== 'save_confirmation';
  if (setup) {
    $('review-label').textContent =
      'All ' +
      lines +
      ' source lines are present, the last source line is END, and I left the console in ' +
      (setup.screen === 'editor' ? 'the editor.' : 'Direct mode.');
    $('save-note').textContent =
      setup.verification === 'passed'
        ? 'All three parts match the entered readings. The next button enters SAVE “TXT:NS2” in Direct.'
        : 'You skipped the optional check. The received source remains unverified. The next button enters SAVE “TXT:NS2” in Direct.';
    $('save-note').textContent =
      $('save-note').textContent.replaceAll('TXT:NS2', 'TXT:' + name) +
      ' · SAVE "TXT:' +
      name +
      '",' +
      sourceSlot;
    const comparison = setup.comparison;
    if (comparison && ['mismatch', 'save'].includes(stage)) {
      $('wizard-results').hidden = false;
      const fingerprint = JSON.stringify(comparison);
      if (fingerprint !== resultStamp) {
        resultStamp = fingerprint;
        $('part-results').replaceChildren();
        for (const part of comparison.parts) {
          const box = element(
            'div',
            'part-result' + (part.matches ? '' : ' mismatch'),
          );
          box.append(
            element(
              'strong',
              '',
              `Part ${part.index} · lines ${part.first}-${part.last} · ${part.matches ? 'Matches' : 'Mismatch'}`,
            ),
            element(
              'span',
              '',
              `Characters: ${part.actual.characters} / expected ${part.characters}`,
            ),
            element(
              'span',
              '',
              `Checksum: ${part.actual.checksum} / expected ${part.checksum}`,
            ),
            element(
              'span',
              '',
              `Text storage: ${part.actual.characters * 2} / expected ${part.utf16Bytes} bytes (UTF-16, derived)`,
            ),
          );
          $('part-results').append(box);
        }
      }
      $('edit-readings').hidden = stage !== 'mismatch';
      $('restart-setup').hidden = stage !== 'mismatch';
    }
  }
  const phases = ['prepare', 'typing', 'review', 'save', 'reboot', 'launch'],
    phase = ['checking', 'values', 'mismatch'].includes(stage)
      ? 'review'
      : ['saving', 'save_confirmation'].includes(stage)
        ? 'save'
        : stage;
  document.querySelectorAll('[data-phase]').forEach((e) => {
    e.classList.toggle('current', e.dataset.phase === phase);
    e.classList.toggle(
      'done',
      phases.indexOf(e.dataset.phase) < phases.indexOf(phase),
    );
  });
  renderControls();
  renderEngineInstall();
}

const networkName = (mode) => (mode === 'wifi' ? 'Wi-Fi' : 'LAN');
async function changeConnection(id, mode, ip, modes) {
  const result = await act('/api/connection', {
    id,
    ...(mode === undefined ? {} : { mode }),
    ...(ip === undefined ? {} : { ip }),
    ...(modes === undefined ? {} : { modes }),
  });
  if (result?.ok) {
    $('screen-ready').checked = false;
    requestId = null;
    renderControls();
  }
}
function renderConsoleConnections(card, preference, locked, supported) {
  const modes = preference.modes || [preference.mode];
  card.querySelectorAll('[data-transport]').forEach((label) => {
    const selected = modes.includes(label.dataset.transport),
      input = label.querySelector('input');
    label.classList.toggle('selected', selected);
    input.checked = selected;
    input.dataset.unavailable = String(
      !supported || (selected && modes.length === 1),
    );
    input.disabled = locked || input.dataset.unavailable === 'true';
  });
  const editor = card.querySelector('.address-editor');
  if (!editor.dataset.dual) {
    editor.dataset.dual = 'true';
    const summary = element('summary');
    editor.replaceChildren(summary);
    for (const mode of ['lan', 'wifi']) {
      const form = element('form', 'address-form console-address');
      form.dataset.mode = mode;
      const label = element('label', '', networkName(mode) + ' IP'),
        input = element('input'),
        apply = element('button', '', 'Apply'),
        status = element('p', 'console-path');
      input.type = 'text';
      input.inputMode = 'decimal';
      input.autocomplete = 'off';
      input.placeholder = 'Automatic · optional address';
      input.id = 'console-address-' + mode;
      input.setAttribute(
        'aria-label',
        'Console ' + networkName(mode) + ' IP address',
      );
      label.htmlFor = input.id;
      apply.type = 'submit';
      const auto = element('button', 'quiet', 'Auto');
      auto.type = 'button';
      auto.onclick = () =>
        changeConnection('lan', mode, '', state.connections.devices.lan.modes);
      form.append(label, input, apply, auto, status);
      form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const pref = state.connections.devices.lan;
        await changeConnection(
          'lan',
          mode,
          input.value.trim(),
          pref.modes || [pref.mode],
        );
      });
      editor.append(form);
    }
  }
  const active = preference.paths?.find(
    (p) => p.ready && !p.pending && p.address && p.mode === preference.activeMode,
  );
  setText(editor.querySelector('summary'), active
    ? networkName(active.mode) + ' · ' + active.address + ' · Edit'
    : modes.map(networkName).join(' + ') + ' · Auto / manual');
  for (const form of editor.querySelectorAll('form')) {
    const mode = form.dataset.mode,
      input = form.querySelector('input'),
      path = preference.paths?.find((p) => p.mode === mode),
      enabled = modes.includes(mode),
      status = form.querySelector('.console-path'),
      value =
        preference.addresses[mode] ||
        preference.candidates?.find((p) => p.mode === mode)?.address ||
        '';
    if (document.activeElement !== input) input.value = value;
    const lastKnown=preference.lastKnownAddresses?.[mode];input.placeholder=lastKnown?'Auto · last known '+lastKnown.address:'Automatic · optional address';
    form.querySelectorAll('input,button').forEach((e) => {
      e.dataset.unavailable = String(!supported);
      e.disabled = locked || !supported;
    });
    setText(status, !supported
      ? 'Restart Platform to enable both connections'
      : !enabled
        ? 'Not selected'
        : !path
          ? (lastKnown?'Last known · '+lastKnown.address+' · not checked':'Not checked')
          : path.pending
            ? 'Automatic · waiting for Switch'+(lastKnown?' · last known '+lastKnown.address:'')
            : path.ready
              ? (mode === preference.activeMode ? 'In use' : 'Available') +
                (Number.isFinite(path.latencyMs)
                  ? ' · ' + path.latencyMs.toFixed(2) + ' ms'
                  : '')
              : 'Unavailable');
    status.title = path?.error || (lastKnown?'Verified '+lastKnown.verifiedAt+'; current connection not confirmed':'');
    status.classList.toggle('available', !!path?.ready && !path?.pending && enabled);
    status.classList.toggle(
      'active',
      mode === preference.activeMode && !!path?.ready && !path?.pending,
    );
  }
  card.querySelector('.console-paths')?.remove();
}
function renderArmSetup(meta) {
  const panel = $('arm-setup');
  panel.hidden = meta.configuration.inputAdapter !== 'ssh-gadget';
  if (panel.hidden) return;
  const source = meta.profiles?.[meta.configurationId] || {};
  const fields = { 'arm-setup-address': source.armAddress || meta.configuration.settings?.armAddress || '', 'arm-setup-user': source.armUser || 'root', 'arm-setup-key': source.sshIdentity || '' };
  if(panel.dataset.configuration!==meta.configurationId){panel.dataset.configuration=meta.configurationId;panel.open=!state.checks.items.find(x=>x.id==='arm')?.ready;}
  for(const [id,value] of Object.entries(fields)) if(document.activeElement!==$(id) && !$(id).dataset.edited) $(id).value=value;
  const ready=state.checks.items.find(x=>x.id==='arm')?.ready;
  $('arm-setup-status').textContent=ready?'Input device ready · setup guide remains available':'Install the input package before the first connection';
  const locked=serverUnavailable||busy||!!meta.locked||!!state.preview;
  panel.querySelectorAll('input,button').forEach(e=>{if(!['arm-method-ssh','arm-method-manual','arm-package-download'].includes(e.id))e.disabled=locked;});
  $('arm-install-target').textContent='Target folder: '+(meta.configuration.inputDirectory||'/opt/iz2s/arm-input')+' · service: iz2s-discovery.service';
  $('arm-install').disabled ||= !$('arm-install-confirm').checked;
}
function renderDeviceDetail(container, row) {
  const detail = String(row.detail || 'Not checked');
  if (!container.querySelector('.detail-text')) {
    const text = element('span', 'detail-text');
    const help = element('button', 'detail-help', '?');
    help.type = 'button';
    const tooltip = element('span', 'detail-tooltip');
    tooltip.id = 'device-help-' + row.id;
    tooltip.setAttribute('role', 'tooltip');
    help.setAttribute('aria-describedby', tooltip.id);
    help.setAttribute('aria-label', 'Connection details');
    help.setAttribute('aria-expanded', 'false');
    help.append(tooltip);
    help.onclick = (event) => {
      event.stopPropagation();
      const open = help.getAttribute('aria-expanded') !== 'true';
      document.querySelectorAll('.detail-help[aria-expanded="true"]').forEach(button => button.setAttribute('aria-expanded', 'false'));
      help.setAttribute('aria-expanded', String(open));
    };
    help.onkeydown = (event) => {
      if (event.key === 'Escape') { help.setAttribute('aria-expanded', 'false'); help.blur(); }
    };
    container.replaceChildren(text, help);
  }
  const extended = detail.length > 70 || detail.includes('\n');
  const first = detail.split(/\.(?:\s|$)|\n/)[0];
  setText(container.querySelector('.detail-text'), extended
    ? (first.length <= 55 ? first.replace(/[.;]$/, '') + '.' : row.pending ? 'Waiting for connection.' : row.ready ? 'Ready.' : 'Setup required.')
    : detail);
  container.querySelector('.detail-help').hidden = !extended;
  setText(container.querySelector('.detail-tooltip'), detail);
}
document.addEventListener('click', (event) => {
  if (!event.target.closest('.detail-help')) document.querySelectorAll('.detail-help[aria-expanded="true"]').forEach(button => button.setAttribute('aria-expanded', 'false'));
  document.querySelectorAll('.device-picker[open]').forEach(picker => {
    if (!picker.contains(event.target)) picker.open = false;
  });
  const files = $('connection-files');
  if (files.open && !files.contains(event.target)) files.open = false;
});
$('connection-files').addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    $('connection-files').open = false;
    $('connection-files').querySelector('summary').focus();
  }
});
function renderDevicePicker(card, row) {
  if (!['server', 'arm'].includes(row.id)) return;
  const meta = state.platform,
    architecture = meta.host?.architecture,
    serverName = architecture === 'arm64' ? 'Server ARM64PC' : architecture === 'x64' ? 'Server x64 PC' : 'Server',
    inputName = inputDesign === 'own' ? 'Your input device' : meta.configuration.inputAdapter === 'ssh-gadget' ? 'ARM PC' : meta.configuration.hardware?.input?.name || 'ARM PC',
    choices = row.id === 'server'
      ? [
          { id: 'x64', name: 'Server x64 PC', note: architecture === 'x64' ? 'Current server' : 'Start IZ2S on an x64 PC', available: architecture === 'x64' },
          { id: 'arm64', name: 'Server ARM64PC', note: architecture === 'arm64' ? 'Current server' : 'Start IZ2S on an ARM64 PC', available: architecture === 'arm64' },
        ]
      : [
          { id: 'arm', name: 'ARM PC', note: 'USB keyboard input', available: !state.active && !busy },
          { id: 'cardputer', name: 'Cardputer ADV', note: 'Wi-Fi → USB keyboard', available: !state.active && !busy },
          ...(state.preview ? [{ id: 'own', name: 'Manual input · preview', note: 'Interface demonstration only', available: !state.active && !busy }] : []),
        ];
  let picker = card.querySelector('.device-picker');
  if (!picker) {
    picker = element('details', 'device-picker');
    const summary = element('summary');
    summary.setAttribute('aria-label', row.id === 'server' ? 'Server device' : 'Choose an input device');
    const label = element('span', 'device-picker-label'), chevron = element('span', 'device-picker-chevron', '⌄');
    chevron.setAttribute('aria-hidden', 'true');
    summary.append(label, chevron);
    const options = element('div', 'device-picker-options');
    options.setAttribute('role', 'group');
    options.setAttribute('aria-label', row.id === 'server' ? 'Server devices' : 'Input devices');
    picker.append(summary, options);
    picker.addEventListener('keydown', event => {
      if (event.key === 'Escape') { picker.open = false; summary.focus(); }
    });
    picker.addEventListener('toggle', () => {
      if (picker.open) document.querySelectorAll('.device-picker[open]').forEach(other => {
        if (other !== picker) other.open = false;
      });
    });
    card.querySelector('h3').after(picker);
  }
  card.querySelector('h3').hidden = true;
  setText(picker.querySelector('.device-picker-label'), row.id === 'server' ? serverName : inputName);
  const stamp = JSON.stringify([choices, inputDesign, meta.configurationId]);
  if (picker.dataset.choices === stamp) return;
  picker.dataset.choices = stamp;
  const options = picker.querySelector('.device-picker-options');
  options.replaceChildren();
  for (const choice of choices) {
    const button = element('button', 'device-picker-option');
    button.type = 'button';
    button.disabled = !choice.available;
    button.setAttribute('aria-label', choice.name + ' · ' + choice.note);
    const selected = row.id === 'server' ? choice.available : choice.id === (inputDesign === 'own' ? 'own' : meta.configurationId === 'pc-cardputer-adv' ? 'cardputer' : 'arm');
    button.setAttribute('aria-pressed', String(selected));
    const text = element('span');
    text.append(element('strong', '', choice.name), element('small', '', choice.note));
    button.append(text, element('span', 'device-picker-mark', selected ? '✓' : ''));
    button.onclick = async () => {
      if (row.id === 'arm') {
        if (choice.id !== 'own') {
          inputDesign = 'arm';
          const configurationId = choice.id === 'cardputer' ? 'pc-cardputer-adv'
            : meta.configuration.inputAdapter === 'ssh-gadget' ? meta.configurationId : 'pc-arm';
          if (configurationId !== meta.configurationId) await act('/api/launch-settings', {configurationId});
        } else inputDesign = choice.id;
        manualDesignStage = 1;
        manualDesignConfirmed.clear();
        $('manual-fs-preview').checked = state.platform.loaderSettings?.fsRpcMode === true;
        renderDevices();
        renderControls();
      }
      picker.open = false;
      picker.querySelector('summary').focus();
    };
    options.append(button);
  }
}
function renderManualInputDesign() {
  const meta = state?.platform;
  if (!meta) return;
  if (state.active || state.emergencyInputPending || !meta.izModules) inputDesign = 'arm';
  const shown = inputDesign === 'own', wasShown = !$('manual-input-design').hidden;
  $('manual-input-design').hidden = !shown;
  if (shown) $('iz-flow').hidden = true;
  else if (wasShown) {
    $('iz-flow').hidden = !meta.izModules;
    renderEngineInstall();
  }
  const arm = $('device-arm'), usb = $('device-usb');
  if (usb) usb.hidden = shown;
  $('devices').style.setProperty('--device-count', state.checks.items.filter(row => !shown || row.id !== 'usb').length);
  if (arm) {
    arm.classList.toggle('manual-preview', shown);
    let note = $('manual-device-description');
    if (!note) {
      note = element('p', 'manual-device-description', 'Your input method · Connection is not checked.');
      note.id = 'manual-device-description';
      arm.querySelector('.device-heading').append(note);
    }
    note.hidden = !shown;
    const row = state.checks.items.find(item => item.id === 'arm');
    if (row) renderDevicePicker(arm, row);
  }
  document.querySelector('.network-caption').hidden = shown;
  $('hardware-options').hidden = shown;
  if (!shown) return;
  $('arm-setup').hidden = true;
  $('cardputer-setup').hidden = true;
  $('engine-install').hidden = true;
  $('installation-options').hidden = true;
  $('recheck').disabled = true;
  document.querySelectorAll('.transport-choice input,.transport-choice button,.address-form input,.address-form select,.address-form button,.detected-peer')
    .forEach(control => { control.disabled = true; });
  document.querySelectorAll('[data-variant],[data-loader]').forEach(button => { button.disabled = true; });
  const withFiles = $('manual-fs-preview').checked, total = withFiles ? 7 : 6;
  document.querySelector('.manual-design-steps').style.setProperty('--manual-step-count', total);
  if (manualDesignStage > total) manualDesignStage = total;
  $('manual-files-step').hidden = !withFiles;
  const stages = {
    1: ['Preparation', 'This previews the manual input interface. Buttons only change the preview stage.'],
    2: ['Confirm manual input', 'The panel does not read or verify the entered data.', 'I checked that the manually entered data is correct.'],
    3: ['Verification', 'Follow the selected engine instructions to verify the result yourself.'],
    4: ['Confirm save', 'This switch records your confirmation in the preview.', 'I checked that the data was saved correctly.'],
    5: ['Continue', 'The next preview stage. No commands are sent to the device.'],
    6: ['Confirm result', 'You verify the result. This preview cannot confirm the device state.', 'I checked the result and it is correct.'],
    7: ['Filesystem', 'Open an empty file manager preview in another tab. Device connections and file reads are disabled.'],
  };
  const [title, description, confirmation] = stages[manualDesignStage];
  setText($('manual-stage-counter'), 'STAGE ' + manualDesignStage + ' / ' + total);
  setText($('manual-stage-title'), title);
  setText($('manual-stage-description'), description);
  $('manual-confirm-row').hidden = !confirmation;
  setText($('manual-confirm-label'), confirmation || '');
  $('manual-stage-confirm').checked = manualDesignConfirmed.has(manualDesignStage);
  $('manual-next').disabled = !!confirmation && !manualDesignConfirmed.has(manualDesignStage);
  $('manual-next').hidden = manualDesignStage === 7;
  $('manual-files-link').hidden = manualDesignStage !== 7;
  setText($('manual-next'), manualDesignStage === total ? 'Return to the beginning' : 'Next stage');
  setText($('manual-engine-folder'), meta.loaders.find(loader => loader.id === meta.loaderId)?.folder || 'Find this path in Files → Engine.');
  document.querySelectorAll('[data-manual-stage]').forEach(button => {
    const selected = Number(button.dataset.manualStage) === manualDesignStage;
    button.classList.toggle('selected', selected);
    if (selected) button.setAttribute('aria-current', 'step');
    else button.removeAttribute('aria-current');
  });
}
document.querySelectorAll('[data-manual-stage]').forEach(button => {
  button.onclick = () => { manualDesignStage = Number(button.dataset.manualStage); renderManualInputDesign(); };
});
$('manual-fs-preview').onchange = renderManualInputDesign;
$('manual-stage-confirm').onchange = () => {
  if ($('manual-stage-confirm').checked) manualDesignConfirmed.add(manualDesignStage);
  else manualDesignConfirmed.delete(manualDesignStage);
  renderManualInputDesign();
};
$('manual-next').onclick = () => {
  if (inputDesign !== 'own' || state?.active || state?.emergencyInputPending || $('manual-next').disabled) return;
  if (manualDesignStage !== 7) { manualDesignStage = manualDesignStage === 6 ? ($('manual-fs-preview').checked ? 7 : 1) : manualDesignStage + 1; renderManualInputDesign(); }
};
$('manual-exit').onclick = () => { inputDesign = 'arm'; renderDevices(); renderControls(); };
function renderDevices() {
  const config = state.connections;
  for(const card of [...$('devices').children])if(!state.checks.items.some(row=>card.id==='device-'+row.id))card.remove();
  $('devices').style.setProperty('--device-count',state.checks.items.length);
  for (const [index, row] of state.checks.items.entries()) {
    const deviceInfo = words[row.id] || [row.label || 'Device', document.getElementById(row.icon)?.tagName.toLowerCase() === 'symbol' ? row.icon : 'i-server'];
    let card = $('device-' + row.id);
    const preference = config?.devices?.[row.id];
    if (!card) {
      card = element('article', 'device');
      card.id = 'device-' + row.id;
      const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg'),
        use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
      use.setAttribute('href', '#' + deviceInfo[1]);
      svg.setAttribute('aria-hidden', 'true');
      svg.append(use);
      const heading = element('div', 'device-heading');
      heading.append(
        svg,
        element('span', 'state'),
        element('h3', '', deviceInfo[0]),
        element('p', 'device-detail'),
        element('p', 'device-ping'),
      );
      card.append(heading);
      if (row.id !== 'usb') {
        const group = element('div', 'transport-choice');
        group.setAttribute('role', 'group');
        group.setAttribute(
          'aria-label',
          deviceInfo[0] + ' network connection',
        );
        for (const mode of ['lan', 'wifi']) {
          const button = element(
            row.id === 'lan' ? 'label' : 'button',
            'transport-option',
          );
          button.dataset.transport = mode;
          button.title = networkName(mode);
          if (row.id === 'lan') {
            const input = element('input');
            input.type = 'checkbox';
            input.setAttribute(
              'aria-label',
              'Enable console ' + networkName(mode),
            );
            input.addEventListener('change', () => {
              const current = state.connections.devices.lan.modes || [
                  state.connections.devices.lan.mode,
                ],
                modes = input.checked
                  ? [...new Set([...current, mode])]
                  : current.filter((m) => m !== mode);
              if (!modes.length) {
                input.checked = true;
                return;
              }
              changeConnection(row.id, undefined, undefined, modes);
            });
            button.append(input, element('span', 'transport-tick', '✓'));
          } else {
            button.type = 'button';
            button.setAttribute('aria-label', networkName(mode));
            button.addEventListener('click', () =>
              changeConnection(row.id, mode),
            );
          }
          const icon = document.createElementNS(
              'http://www.w3.org/2000/svg',
              'svg',
            ),
            symbol = document.createElementNS(
              'http://www.w3.org/2000/svg',
              'use',
            );
          symbol.setAttribute('href', mode === 'wifi' ? '#i-wifi' : '#i-lan');
          icon.setAttribute('aria-hidden', 'true');
          icon.append(symbol);
          button.append(icon);
          group.append(button);
        }
        const editor = element('details', 'address-editor'),
          summary = element('summary'),
          form = element('form', 'address-form'),
          label = element(
            'label',
            '',
            row.id === 'server' ? 'Listen on this PC' : 'Device IP address',
          );
        const input = element(row.id === 'server' ? 'select' : 'input');
        input.id = 'address-' + row.id;
        input.setAttribute('aria-label', deviceInfo[0] + ' IP address');
        if (row.id !== 'server') {
          input.type = 'text';
          input.inputMode = 'decimal';
          input.autocomplete = 'off';
          input.placeholder = 'Automatic · or enter IP';
          const list = element('datalist');
          list.id = 'addresses-' + row.id;
          input.setAttribute('list', list.id);
          form.append(list);
        }
        const apply = element('button', '', 'Apply');
        apply.type = 'submit';
        label.htmlFor = input.id;
        const automatic = element('button', 'quiet', 'Auto');
        automatic.type = 'button';
        automatic.onclick = () =>
          act('/api/connection', {
            id: row.id,
            mode: state.connections.devices[row.id].mode,
            automatic: true,
          });
        form.append(label, input, apply, automatic);
        form.addEventListener('submit', async (event) => {
          event.preventDefault();
          const pref = state.connections?.devices[row.id];
          if (!pref) return;
          await changeConnection(row.id, pref.mode, input.value.trim());
        });
        const suggestions = element('div', 'detected-peers');
        editor.append(summary, suggestions, form);
        card.append(group, editor);
      } else {
        const fixed = element('div', 'fixed-transport');
        fixed.title = 'USB · direct cable';
        fixed.setAttribute('aria-label', 'USB · direct cable');
        const usb = svg.cloneNode(true);
        fixed.append(usb);
        card.append(fixed);
      }
    }
    placeChild($('devices'), card, index);
    setText(card.querySelector('h3'), row.label || deviceInfo[0]);
    renderDevicePicker(card, row);
    card.classList.toggle('ready', row.ready);
    card.classList.toggle('pending', !!row.pending);
    setText(card.querySelector('.state'), row.ready
      ? row.pending
        ? 'WAITING'
        : '✓ READY'
      : state.preview
        ? 'PREVIEW'
        : '○ WAITING');
    const detailRow = row.id === 'usb' && state.platform?.configuration?.inputAdapter === 'cardputer-wifi'
      ? {...row, detail: globalThis.IZ2S_ADV_SETUP.usbDescription(state, state.checks.items.find(item => item.id === 'arm'), row)}
      : row;
    renderDeviceDetail(card.querySelector('.device-detail'), detailRow);
    const ping = card.querySelector('.device-ping');
    ping.hidden = false;
    setText(ping, Number.isFinite(row.latencyMs)
      ? 'Last ping · ' + row.latencyMs.toFixed(2) + ' ms'
      : '\u00a0');
    if (preference) {
      const locked = busy || !!config.locked || !!state.preview;
      const cardputer = row.id === 'arm' && state.platform.configuration.inputAdapter === 'cardputer-wifi';
      const addressEditor = card.querySelector('.address-editor');
      addressEditor.hidden = cardputer;
      let cardputerAddress = card.querySelector('.cardputer-address');
      if (cardputer && !cardputerAddress) {
        cardputerAddress = element('button', 'cardputer-address quiet');
        cardputerAddress.type = 'button';
        cardputerAddress.onclick = () => {
          $('hardware-options').open = true;
          $('hardware-options').querySelector('[name="cardputerEndpoint"]')?.focus();
        };
        card.append(cardputerAddress);
      }
      if (cardputerAddress) cardputerAddress.hidden = !cardputer;
      card.querySelectorAll('[data-transport]').forEach(button => { button.hidden = cardputer && button.dataset.transport !== 'wifi'; button.dataset.fixed = String(cardputer); });
      if (cardputer) {
        const wifi = card.querySelector('[data-transport="wifi"]');
        wifi.classList.add('selected');
        wifi.setAttribute('aria-pressed', 'true');
        wifi.disabled = true;
        const settings = state.platform.profiles?.[state.platform.configurationId] || {};
        setText(cardputerAddress, (settings.cardputerEndpoint || 'http://iz2s-adv.local') + ' · Edit');
        cardputerAddress.disabled = locked;
        continue;
      }
      if (row.id === 'lan') {
        renderConsoleConnections(
          card,
          preference,
          locked,
          config.dualConsoleSupported,
        );
        card
          .querySelector('.device-heading use')
          .setAttribute(
            'href',
            '#' +
              (preference.activeMode === 'wifi' ||
              (preference.modes?.length !== 2 && preference.mode === 'wifi')
                ? 'i-wifi'
                : 'i-lan'),
          );
        continue;
      }
      card.querySelectorAll('[data-transport]').forEach((button) => {
        const selected = button.dataset.transport === preference.mode;
        button.classList.toggle('selected', selected);
        button.setAttribute('aria-pressed', String(selected));
        button.disabled = locked;
      });
      const icon =
        row.id === 'lan'
          ? preference.mode === 'wifi'
            ? 'i-wifi'
            : 'i-lan'
          : deviceInfo[1];
      card.querySelector('use').setAttribute('href', '#' + icon);
      const editor = card.querySelector('.address-editor'),
        input = $('address-' + row.id);
      setText(editor.querySelector('summary'), preference.address
        ? (preference.automatic ? 'Auto · ' : '') +
          preference.address +
          ' · Edit'
        : 'Auto / manual · ' + networkName(preference.mode));
      if (document.activeElement !== input) {
        if (row.id === 'server') {
          const options = preference.options.filter(
              (o) => o.mode === preference.mode,
            ),
            stamp = JSON.stringify([
              options,
              preference.address,
              preference.automatic,
            ]);
          if (input.dataset.stamp !== stamp) {
            input.dataset.stamp = stamp;
            input.replaceChildren();
            const automatic = element('option', '', 'Automatic');
            automatic.value = '';
            input.append(automatic);
            if (
              preference.address &&
              !options.some((o) => o.address === preference.address)
            ) {
              const option = element(
                'option',
                '',
                preference.address ||
                  'No active ' + networkName(preference.mode) + ' adapter',
              );
              option.value = preference.address;
              input.append(option);
            }
            for (const item of options) {
              const option = element(
                'option',
                '',
                item.address + ' · ' + item.name,
              );
              option.value = item.address;
              input.append(option);
            }
            input.value = preference.automatic ? '' : preference.address;
          }
        } else if (input.dataset.mode !== preference.mode || !editor.open)
          input.value = preference.address;
        input.dataset.mode = preference.mode;
      }
      if (row.id !== 'server') {
        const list = $('addresses-' + row.id),
          stamp = JSON.stringify(preference.options);
        if (list.dataset.stamp !== stamp) {
          list.dataset.stamp = stamp;
          list.replaceChildren();
          for (const item of preference.options) {
            const option = element('option');
            option.value = item.address;
            option.label = item.name || item.address;
            list.append(option);
          }
        }
      }
      const suggestions = editor.querySelector('.detected-peers'),
        choices =
          row.id === 'server'
            ? []
            : preference.options.filter(
                (p) => p.address !== state.connections.devices.server.address,
              ),
        choiceStamp = JSON.stringify([preference.mode, choices]);
      if (suggestions.dataset.stamp !== choiceStamp) {
        suggestions.dataset.stamp = choiceStamp;
        suggestions.replaceChildren();
        if (choices.length) {
          suggestions.append(
            element('span', '', 'Detected on ' + networkName(preference.mode)),
          );
          for (const item of choices.slice(0, 5)) {
            const choose = element(
              'button',
              'detected-peer',
              item.name ? item.name + ' · ' + item.address : item.address,
            );
            choose.type = 'button';
            choose.addEventListener('click', () =>
              changeConnection(
                row.id,
                state.connections.devices[row.id].mode,
                item.address,
              ),
            );
            suggestions.append(choose);
          }
        }
      }
      input.disabled = locked;
      editor
        .querySelectorAll('button')
        .forEach((button) => (button.disabled = locked));
    } else if (row.id !== 'usb') {
      card
        .querySelectorAll('button,input,select')
        .forEach((e) => (e.disabled = true));
    }
  }
  const consoleState = state.consoleState,
    known = state.connected && consoleState && !consoleState.stale;
  const modeLine = $('console-mode-state');
  modeLine.hidden = !known;
  if (known) {
    const mode =
        consoleState.mode === 'docked'
          ? 'TV mode'
          : consoleState.mode === 'handheld'
            ? 'Portable mode'
            : 'Output mode unknown',
      network =
        consoleState.network.type === 'ethernet'
          ? 'Ethernet'
          : consoleState.network.type === 'wifi'
            ? 'Wi-Fi'
            : 'network unknown',
      display = consoleState.display;
    modeLine.textContent =
      mode +
      ' · ' +
      display.width +
      ' × ' +
      display.height +
      ' · ' +
      network +
      (consoleState.network.connected ? '' : ' (disconnected)') +
      (consoleState.warning ? ' · Ethernet is not connected.' : '');
    modeLine.classList.toggle('warning', !!consoleState.warning);
  }
  const modes = config?.devices;
  $('network-summary').textContent = modes
    ? ['server', 'arm', 'lan'].filter(id=>state.checks.items.some(row=>row.id===id))
        .map(
          (id) =>
            (state.checks.items.find(row=>row.id===id)?.label||words[id][0]) +
            ' ' +
            (id === 'arm' && state.platform.configuration.inputAdapter === 'cardputer-wifi' ? ['wifi'] : modes[id].modes || [modes[id].mode]).map(networkName).join(' + '),
        )
        .join('  ·  ')
    : 'Restart Platform to enable connection choices.';
  $('network-help').textContent = config?.locked
    ? 'Connections are locked for this session. Stop it before changing networks.'
    : state.platform.configuration.inputAdapter === 'cardputer-wifi'
      ? 'Cardputer receives commands over Wi-Fi and types on Switch over USB. Use the IP shown on its screen.'
    : config?.discovery?.ambiguous
      ? 'Several ARM services found. Choose the required device, or enter its IP.'
      : 'Automatic discovery uses your local network. Manual addresses take priority.';
}

function residentEligibility(pkg) {
  return state?.platform?.residentFiniteAudio ? pkg?.residentAudio : pkg?.resident;
}
function selectedShellName() {
  return state?.platform?.shells?.find(shell => shell.id === state.platform.variant)?.name ||
    (state?.platform?.variant === 'izpslauncher' ? 'IZPSlauncher' : 'IZlauncher');
}

function renderStartupModules() {
  const resident=state.platform?.residentGraphics===true&&['izlauncher','izpslauncher'].includes(state.platform?.variant);
  const stamp = JSON.stringify(
    [resident,state.platform?.residentFiniteAudio,state.library.map((p) => [p.id, p.name, p.version, p.icon, p.supported,p.resident,p.residentAudio])],
  );
  if (stamp === startupStamp) return;
  startupStamp = stamp;
  const valid = new Set(state.library.filter(p => p.supported !== false).map(p => p.id));
  for (const id of startupSelection)
    if (!valid.has(id)) startupSelection.delete(id);
  $('preload-list').replaceChildren();
  for (const pkg of state.library) {
    const eligibility=residentEligibility(pkg);
    const label = element('label', 'preload-item'),
      input = element('input'),
      image = element('img'),
      text = element('span');
    input.type = 'checkbox';
    input.checked = startupSelection.has(pkg.id);
    input.dataset.unavailable = String(pkg.supported === false);
    input.dataset.residentUnavailable=String(resident&&eligibility?.compatible!==true);
    input.disabled = pkg.supported === false || (resident&&eligibility?.compatible!==true&&!input.checked);
    if(resident&&eligibility?.compatible!==true)label.title=eligibility?.userReason||'Restart the panel to check compatibility.';
    input.setAttribute('aria-label', 'Load ' + pkg.name + ' at startup');
    input.addEventListener('change', () => {
      input.checked
        ? startupSelection.add(pkg.id)
        : startupSelection.delete(pkg.id);
      saveStartup();
      renderControls();
    });
    image.src = pkg.icon;
    image.alt = '';
    text.append(
      element('strong', '', pkg.name),
      element('small', '', pkg.version),
    );
    if(resident)text.append(element('small','',eligibility?.userReason||'Compatibility has not been checked.'));
    label.append(input, image, text);
    $('preload-list').append(label);
  }
}
$('preload-enabled').addEventListener('change', () => {
  saveStartup();
  renderControls();
});
$('preload-clear').addEventListener('click', () => {
  startupSelection.clear();
  document
    .querySelectorAll('#preload-list input')
    .forEach((e) => (e.checked = false));
  saveStartup();
  renderControls();
});
function renderLibrary() {
  const errors = $('module-errors');
  if (errors) {
    errors.hidden = !state.moduleErrors?.length;
    errors.textContent = (state.moduleErrors || []).map(row => (row.id || row.path || 'Module') + ': ' + row.error).join(' · ');
  }
  const available = new Set(state.library.filter(pkg => pkg.supported !== false).map(pkg => pkg.id));
  for (const id of selection) if (!available.has(id)) selection.delete(id);
  const stamp = JSON.stringify(
    state.library.map((p) => [p.id, p.version, p.bytes, p.name, p.icon, p.description, p.supported, p.contents]),
  );
  if (stamp !== packageStamp) {
    packageStamp = stamp;
    $('library').replaceChildren();
    if(!state.library.length){const empty=element('div','empty');empty.append(element('h2','','No modules installed'),element('p','','Open the Modules folder and add an IZM package.'));$('library').append(empty);}
    $('module-host-actions').dataset.stamp = '';
    for (const pkg of state.library) {
      const card = element('article', 'app-card'),
        label = element('label', 'app-select'),
        input = element('input'),
        img = element('img');
      card.dataset.appId = pkg.id;
      input.type = 'checkbox';
      input.disabled = pkg.supported === false;
      if (input.disabled) selection.delete(pkg.id);
      input.checked = selection.has(pkg.id);
      input.setAttribute('aria-label', 'Select ' + pkg.name);
      input.addEventListener('change', () => {
        if (input.checked) selection.add(pkg.id);
        else selection.delete(pkg.id);
        renderControls();
      });
      img.src = pkg.icon;
      img.alt = '';
      label.append(
        input,
        img,
        element('strong', '', pkg.name),
        element(
          'small',
          '',
          pkg.version + ' / IZM ' + (pkg.bytes / 1024).toFixed(1) + ' KiB',
        ),
        element('p', '', pkg.description),
      );
      card.append(label, element('div', 'module-host-actions'));
      if (pkg.contents) {
        const details = element('details', 'module-contents');
        details.append(
          element('summary', '', 'Package contents'),
          element(
            'p',
            '',
            'Switch sources: ' +
              (pkg.contents.switchSourceBytes / 1024).toFixed(1) +
              ' KiB · Server sources: ' +
              (pkg.contents.serverSourceBytes / 1024).toFixed(1) +
              ' KiB. Server code stays on this PC.' +
              (pkg.contents.upstreamSourceBytes
                ? ' Upstream sources: ' +
                  (pkg.contents.upstreamSourceBytes / 1048576).toFixed(1) +
                  ' MiB.'
                : '') +
              (pkg.contents.moduleDependencies?.length
                ? ' Required modules: ' +
                  pkg.contents.moduleDependencies.join(', ') +
                  '.'
                : '') +
              ((pkg.contents.externalDependencies || []).length
                ? ' Downloaded separately: ' +
                  (pkg.contents.externalDependencies || []).join(', ') +
                  '.'
                : ''),
          ),
        );
        details.onclick = (e) => e.stopPropagation();
        card.append(details);
      }
      const actions=element('div','module-actions');card.append(actions);
      if(pkg.module?.server?.settings){
        const configure=element('button','secondary module-configure',pkg.module.server.settings.label||'Settings');
        configure.dataset.unavailable=String(pkg.supported===false);
        configure.type='button';configure.disabled=serverUnavailable||state.preview===true||pkg.supported===false;
        configure.title='Configure this module before launch; preferences are kept between sessions';
        configure.onclick=async()=>{
          configure.dataset.pending='true';configure.disabled=true;
          try{if(!workspaceReady)throw Error('Reopen IZ2S to enable module tabs.');const value=await api('/api/module/settings',{id:pkg.module.id});workspace.openRequest(value.tab,true);}
          catch(error){notice(error.message);}finally{delete configure.dataset.pending;renderLocalActionAvailability();}
        };
        actions.append(configure);
      }
      const openModule=element('button','secondary','Open module');openModule.type='button';openModule.onclick=()=>workspace.openModule(pkg.module?.id||pkg.id);actions.append(openModule);
      actions.append(folderButton('module', pkg.module?.id || pkg.id, 'Open module folder'));
      const unload=element('button','secondary module-unload','Remove from console');
      unload.type='button';unload.title='Unload this module from the console menu. The PC package remains available.';
      unload.onclick=async()=>{if(!window.confirm('Remove '+pkg.name+' from the console menu? You can load it again from this PC.'))return;selection.delete(pkg.id);await act('/api/unload',{ids:[pkg.id]});};
      actions.append(unload);
      card.append(element('small','app-memory',''));
      $('library').append(card);
    }
  }
  const installed = new Set((state.installed || []).map(app => typeof app === 'string' ? app : app.id));
  for (const card of $('library').querySelectorAll('.app-card')) {
    const loaded = installed.has(card.dataset.appId);
    card.classList.toggle('installed', loaded);
    card.dataset.installed = String(loaded);
    card.querySelector('img').title = loaded ? 'Loaded on console' : 'Not loaded on console';
    const retained=state.residentMemory?.apps?.find(app=>app.id===card.dataset.appId),memoryLabel=card.querySelector('.app-memory');
    if(memoryLabel){memoryLabel.hidden=!retained;setText(memoryLabel,retained?(state.connected?'Retained in RAM':'Previously retained')+' · '+(retained.bufferBytes/1048576).toFixed(1)+' MiB':'');}
    const unload=card.querySelector('.module-unload'),pending=state.jobs.some(job=>job.packageId===card.dataset.appId&&['queued','loading','publishing'].includes(job.status));
    unload.hidden=!loaded;unload.disabled=busy||state.preview||!state.connected||pending||!!(state.platform?.residentGraphics&&state.active);
    setText(unload,pending?'Operation queued…':'Remove from console');
  }
  renderModuleHostActions();
  setText($('app-count'), state.library.length);
  const libraryConnection = $('library-connection');
  libraryConnection.classList.toggle('online', !!state.connected);
  const connectionLabel = state.connected ? 'Switch 2 connected' : 'Switch 2 not connected';
  if (libraryConnection.querySelector('span').textContent !== connectionLabel)
    libraryConnection.querySelector('span').textContent = connectionLabel;
  $('library-note').textContent = state.platform?.residentGraphics && state.active
    ? 'Compatible apps remain in memory for this session. '+(state.residentMemory?'Session memory: '+(state.residentMemory.usedBytes/1048576).toFixed(1)+' / '+(state.residentMemory.budgetBytes/1048576).toFixed(0)+' MiB. ':'')+'Add apps from the menu while the PC is available. Select the full set before a retained-audio session.'
    : state.connected
    ? 'Switch 2 connected · ' + selection.size + ' selected'
    : 'Connect Switch 2 and start ' + selectedShellName() + ' to load apps.';
  renderJobs();
  const url = state.streamUrl;
  try {
    const u = new URL(url);
    if (u.hostname !== '127.0.0.1' || u.protocol !== 'http:') throw Error();
    $('stream-link').href = url;
    $('stream-link').hidden = !state.connected;
    $('stream-link').onclick=async event=>{event.preventDefault();try{if(!workspaceReady)throw Error('Reopen IZ2S to enable module tabs.');const result=await api('/api/workspace/attach',{url});workspace.openRequest(result.tab,true);}catch(error){notice(error.message);}};
  } catch {
    $('stream-link').hidden = true;
  }
}
function renderModuleHostActions() {
  const hostList = $('module-host-actions'),
    targets = [...document.querySelectorAll('.module-host-actions')],
    existing = new Map(targets.flatMap(target => [...target.children].map(row => [row.dataset.hostActionKey, row]))),
    desired = new Map(targets.map(target => [target, []])),
    cards = new Map([...$('library').children].map(card => [card.dataset.appId, card]));
  for (const module of state.moduleCatalog || []) {
    const target = cards.get(module.id)?.querySelector('.module-host-actions') || hostList;
    for (const pkg of module.server?.localPackages || []) {
      const record = state.hostModules?.[pkg.id];
      if (record?.installed) continue;
      const key = 'runtime:' + module.id + ':' + pkg.id;
      let box = existing.get(key);
      if (!box) {
        box = element('details', 'local-module module-setup'); box.dataset.hostActionKey = key;
        const text = element('div', 'module-setup-body');
        text.append(element('p', 'hint'));
        box.append(element('summary', '', 'PC setup'), text, element('button', 'secondary'));
      }
      const text = box.querySelector('.module-setup-body'), button = box.querySelector('button');
      setText(text.querySelector('.hint'), pkg.importSource
        ? 'Choose the local game folder. Saved games stay in the original installation.'
        : record?.missingFiles?.length ? 'Local runtime files are missing. Reinstall this component.'
        : 'External component is downloaded separately from the IZM package.');
      let input = box.querySelector('.local-source');
      if (pkg.importSource) {
        if (!input) {
          input = element('input', 'local-source'); input.type = 'text'; input.dataset.runtime = pkg.id;
          input.placeholder = 'Absolute runtime folder'; text.append(input);
        }
        input.setAttribute('aria-label', module.name + ' runtime folder');
        input.oninput = () => {
          button.disabled = serverUnavailable || busy || state.preview || state.hostModules?.[pkg.id]?.supported === false || !input.value.trim();
        };
      } else if (input) { input.remove(); input = null; }
      button.type = 'button';
      setText(button, pkg.importSource ? 'Import local runtime' : 'Install on this PC');
      button.dataset.unavailable = String(record?.supported === false);
      button.disabled = serverUnavailable || busy || state.preview || record?.supported === false || (!!input && !input.value.trim());
      button.onclick = () => act('/api/install-host', {id:pkg.id, ...(input ? {source:input.value.trim()} : {})});
      desired.get(target).push(box);
    }
    if (module.server && !module.switch) {
      const key = 'host:' + module.id;
      let box = existing.get(key);
      if (!box) {
        box = element('div', 'local-module'); box.dataset.hostActionKey = key;
        box.append(element('strong'), element('button', 'secondary'));
      }
      const button = box.querySelector('button'), running = state.moduleStates?.some(item => item.id === module.id);
      setText(box.querySelector('strong'), module.name);
      setText(button, running ? 'Stop' : 'Start');
      button.type = 'button'; button.disabled = serverUnavailable || busy || state.preview;
      button.onclick = () => act(running ? '/api/module/stop' : '/api/module/start', {id:module.id});
      desired.get(target).push(box);
    }
  }
  const retained = new Set([...desired.values()].flat());
  for (const target of targets) {
    for (const row of [...target.children]) if (!retained.has(row)) row.remove();
    desired.get(target).forEach((row, index) => placeChild(target, row, index));
    target.hidden = !target.childElementCount;
  }
}
function renderJobs() {
  const container = $('jobs'), existing = new Map([...container.children].map(row => [row.dataset.jobId, row]));
  const rank = job => ['loading', 'publishing'].includes(job.status) ? 0 : job.status === 'queued' ? 1 : 2;
  const jobs = [...state.jobs].sort((a, b) => rank(a) - rank(b) || b.created.localeCompare(a.created)).slice(0, 8);
  const retained = new Set(jobs.map(job => job.id));
  for (const [id, row] of existing) if (!retained.has(id)) row.remove();
  jobs.forEach((job, index) => {
    let row = existing.get(job.id);
    if (!row) {
      row = element('div', 'job'); row.dataset.jobId = job.id;
      const progress = element('progress'); progress.max = 100;
      row.append(element('strong'), element('span', 'job-status'), progress, element('small', 'job-detail'));
    }
    const className = 'job ' + job.status + (job.progress ? ' with-progress' : '');
    if (row.className !== className) row.className = className;
    setText(row.querySelector('strong'), job.name);
    setText(row.querySelector('.job-status'), ['complete', 'ready'].includes(job.status)
      ? job.operation === 'remove' ? 'Removed from console' : 'Loaded' : job.status);
    const progress = row.querySelector('progress'), detail = row.querySelector('.job-detail');
    progress.hidden = detail.hidden = !job.progress;
    if (job.progress) {
      progress.setAttribute('aria-label', 'Loading ' + job.name);
      if (Number.isFinite(job.progress.percent)) {
        if (!progress.hasAttribute('value') || progress.value !== job.progress.percent) progress.value = job.progress.percent;
      } else if (progress.hasAttribute('value')) progress.removeAttribute('value');
      const labels = {verify:'Checking files', prepare:'Preparing on PC', graphics:'Preparing graphics', install:'Installing on PC', transfer:'Updating console menu', ready:'Ready in menu', failed:'Load failed'};
      let text = labels[job.progress.phase] || job.progress.label || job.status;
      if (Number.isFinite(job.progress.totalBytes) && job.progress.totalBytes > 0) {
        const size = n => ((Number.isFinite(n) ? n : 0) / 1048576).toFixed(1) + ' MiB';
        text += ' · ' + size(job.progress.completedBytes) + ' of ' + size(job.progress.totalBytes) + ' · remaining ' + size(job.progress.remainingBytes);
      }
      if (Number.isFinite(job.progress.percent)) text += ' · ' + job.progress.percent + '%';
      setText(detail, text);
    }
    row.title = job.error || '';
    placeChild(container, row, index);
  });
}
let historyStamp = '';
let savedLogAttempt = null;
let savedLogGeneration = 0;
function logTime(value) {
  return new Date(value).toLocaleTimeString(globalThis.RUN21_LANGUAGE?.locale || 'en-GB', {
    hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit',
    fractionalSecondDigits: 3,
  });
}
function logMessage(event) {
  const owner = [event.source, event.stream].filter(Boolean).join('/');
  const elapsed = Number.isFinite(event.elapsedMs) ? ` +${event.elapsedMs}ms` : '';
  const pid = event.pid ? ` pid=${event.pid}` : '';
  return (owner ? `[${owner}${pid}${elapsed}] ` : '') + event.message;
}
async function downloadSessionLog(id) {
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(id));
    const text = value.events.map((e) => e.at + '  ' + logMessage(e)).join('\n');
    const url = URL.createObjectURL(new Blob([text + '\n'], { type: 'text/plain;charset=utf-8' }));
    const link = element('a');
    link.href = url;
    link.download = 'IZ2S-session-' + id + '.log';
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch (e) { notice(e.message); }
}
function invalidateSavedLog() {
  savedLogGeneration++;
  savedLogAttempt = null;
  $('download-saved-log').disabled = true;
  $('log-dialog').setAttribute('aria-busy', 'false');
}
$('log-dialog').addEventListener('close', () => {
  if (!$('log-dialog').open) invalidateSavedLog();
});
$('log-dialog').addEventListener('cancel', invalidateSavedLog);
async function openSavedLog(attempt) {
  const generation = ++savedLogGeneration,
    dialog = $('log-dialog'),
    download = $('download-saved-log'),
    created = date(attempt.created);
  savedLogAttempt = null;
  download.disabled = true;
  setText($('saved-log-title'), 'Session log' + (created ? ' · ' + created : ''));
  setText($('saved-log-attempt'),
    (attempt.kind === 'bootstrap' ? 'Bootstrap installation' : 'Launcher session') + ' · ' + attempt.id);
  setText($('saved-log'), 'Loading saved log…');
  dialog.setAttribute('aria-busy', 'true');
  if (!dialog.open) dialog.showModal();
  try {
    const value = await api('/api/log?id=' + encodeURIComponent(attempt.id));
    if (generation !== savedLogGeneration || !dialog.open) return;
    setText($('saved-log'), value.events.map((event) => event.at + '  ' + logMessage(event)).join('\n') ||
      'No further details were recorded.');
    savedLogAttempt = attempt.id;
    download.disabled = false;
  } catch (error) {
    if (generation !== savedLogGeneration || !dialog.open) return;
    setText($('saved-log'), 'Could not load this log: ' + error.message);
  } finally {
    if (generation === savedLogGeneration && dialog.open) dialog.setAttribute('aria-busy', 'false');
  }
}
function renderHistory() {
  const stamp = JSON.stringify(state.history);
  $('fail-count').textContent = state.history.length;
  if (stamp === historyStamp) return;
  historyStamp = stamp;
  $('attempts').replaceChildren();
  if (!state.history.length) {
    const box = element('div', 'empty');
    box.append(
      element('h2', '', 'No failed attempts'),
      element('p', '', 'Session diagnostics appear here after a failed or cancelled attempt.'),
    );
    $('attempts').append(box);
  }
  for (const attempt of state.history) {
    const row = element('article', 'attempt'),
      body = element('div', 'body');
    body.append(
      element(
        'h3',
        '',
        attempt.kind === 'bootstrap'
          ? 'Bootstrap installation'
          : 'Launcher session',
      ),
      element('time', '', date(attempt.created)),
      element(
        'p',
        '',
        attempt.error ||
          'Input cancelled. Restart the console before trying again.',
      ),
    );
    const view = element('button', '', 'View log'),
      remove = element('button', 'delete', 'Delete log');
    view.addEventListener('click', () => openSavedLog(attempt));
    remove.addEventListener('click', () =>
      act('/api/delete', { id: attempt.id }),
    );
    row.append(body, view, folderButton('session', attempt.id, 'Open log folder'), remove);
    $('attempts').append(row);
  }
}
function renderActivity() {
  const list = state.log.filter((e) => $('log-details').checked || e.type !== 'detail').slice(-250),
    stamp = JSON.stringify(list),
    log = $('log');
  $('download-session-log').disabled = !(state.active || state.lastAttempt)?.id;
  if (log.dataset.stamp === stamp) return;
  logWriting = true;
  log.dataset.stamp = stamp;
  setText($('log-count'), list.length + ' events');
  const existing = new Map([...log.children].map(row => [row.dataset.logKey, row])), occurrences = new Map();
  const entries = list.map(item => {
    const signature = JSON.stringify(item), ordinal = occurrences.get(signature) || 0;
    occurrences.set(signature, ordinal + 1);
    return {item, key:signature + ':' + ordinal};
  });
  const retained = new Set(entries.map(entry => entry.key));
  if (!list.length) retained.add('empty');
  for (const [key, row] of existing) if (!retained.has(key)) row.remove();
  entries.forEach(({item, key}, index) => {
    let row = existing.get(key);
    if (!row) {
      row = element('div', 'log-row ' + item.type); row.dataset.logKey = key;
      row.append(element('time', '', logTime(item.at)), element('span', '', logMessage(item)));
    }
    placeChild(log, row, index);
  });
  if (!list.length) {
    if (!existing.has('empty')) {
      const empty = element('p', 'hint', 'No active attempt. Device checks do not send console commands.');
      empty.dataset.logKey = 'empty'; placeChild(log, empty, 0);
    }
  }
  requestAnimationFrame(() => {
    if (logFollow && log.clientHeight) log.scrollTop = log.scrollHeight;
    logWriting = false;
  });
}

function renderConnectionStatus() {
  const stopping = state?.active?.status === 'stopping';
  const connectionMode = serverUnavailable
    ? ['disconnected', 'Server unavailable']
    : state?.simulated
      ? ['preview', 'Simulated devices']
      : state?.preview
        ? ['preview', 'Design preview']
        : stopping
          ? ['checking', 'Stopping…']
          : state?.active && state?.link?.state === 'waiting-for-dock'
            ? ['connecting', 'Reconnecting…']
            : state?.active?.kind === 'bootstrap'
              ? ['checking', 'Setup in progress…']
              : state?.connected
                ? ['connected', 'Console connected']
                : state?.active
                  ? ['connecting', 'Connecting…']
                  : ['disconnected', 'Not connected'];
  setText($('mode'), connectionMode[1]);
  $('mode').dataset.state = connectionMode[0];
  $('mode').classList.toggle('preview', !serverUnavailable && !!state?.preview);
  $('connection').classList.toggle('online', !serverUnavailable && !!state?.connected && !stopping);
  $('connection').classList.toggle('working', !serverUnavailable && (!!state?.active && (!state?.connected || stopping)));
  const text = connectionMode[1];
  if ($('connection').lastChild.textContent !== text)
    $('connection').lastChild.textContent = text;
}
function renderLocalActionAvailability() {
  for (const control of document.querySelectorAll('.folder-button,[data-folder],.module-configure'))
    control.disabled = serverUnavailable || !!state?.preview || control.dataset.pending === 'true' || control.dataset.unavailable === 'true';
}
function renderServerAvailability() {
  workspace.setAvailable(!serverUnavailable);
  document.querySelector('.nav-dot').hidden=serverUnavailable||!state?.connected;
  renderLocalActionAvailability();
  $('panel-main').classList.toggle('server-unavailable', serverUnavailable);
  if (!serverUnavailable) return;
  renderConnectionStatus();
  for (const card of document.querySelectorAll('#devices > .device')) {
    setText(card.querySelector('.state'), 'UNVERIFIED');
    setText(card.querySelector('.device-ping'), 'Server unavailable');
  }
  const libraryConnection = $('library-connection');
  libraryConnection.classList.remove('online');
  setText(libraryConnection.querySelector('span'), 'Server unavailable · device status is outdated');
  setText($('library-note'), 'Reconnect to the server to refresh device and transfer status.');
  setText($('gate-message'), 'Server unavailable · waiting for a fresh status update.');
  $('delivery-rpc-status').dataset.state = 'unavailable';
  setText($('delivery-rpc-label'), 'RPC status unavailable');
  $('console-mode-state').hidden = true;
  $('application-recovery').hidden = true;
  $('stream-link').hidden = true;
  if (state?.active) setText($('progress-title'), 'Session status unavailable');
  $('dolphin-open').disabled = true;
  setText($('dolphin-status'), 'Server unavailable · FS status cannot be checked.');
  document.querySelectorAll('#start,#load,#retry,#engine-download,#engine-rescan,#arm-install,#hardware-form button,.module-host-actions button,.module-unload,.module-configure,.folder-button,[data-folder],[data-variant],[data-loader],.address-form button,.transport-choice button,.transport-choice input,.detected-peer,.up-start,.up-save,.up-confirm')
    .forEach(control => { control.disabled = true; });
}
function render() {
  const attempt = state.active;
  workspace.updateModules(state);
  renderPlatform();
  const ended = state.lastAttempt;
  if (ended && ended.id !== finishedAttempt) {
    finishedAttempt = ended.id;
    requestId = null;
    $('screen-ready').checked = false;
    document.querySelectorAll('.up-ready,.up-source-ready').forEach(input => {input.checked = false;});
  }
  renderDevices();
  renderStartupModules();
  renderLibrary();
  renderHistory();
  renderActivity();
  renderWizard();
  renderManualInputDesign();
  renderConnectionStatus();
  const applicationRecovery = state.applicationRecovery;
  $('application-recovery').hidden = !applicationRecovery;
  if (applicationRecovery) {
    const labels = {
      'waiting-for-client': 'Waiting for the console app to reconnect.',
      'waiting-for-network': 'Connection lost. Waiting for the app to reconnect.',
      connected: 'App state restored. Control connection is checked separately.',
      stopped: 'App recovery channel stopped.',
    };
    $('application-recovery').textContent = labels[applicationRecovery.state] || 'App recovery state is unknown.';
  }
  $('progress-box').hidden = !attempt;
  $('progress-title').textContent = attempt
    ? {
        starting: attempt.inputActive?'Entering launch commands…':state.link?.state==='connected'||attempt.launchStage==='initializing'?'Console connected · preparing launcher':attempt.launchStage==='waiting-console'?'Input complete · waiting for console':'Preparing launcher',
        typing: 'Entering bootstrap.bas',
        running: 'Launcher is running',
        offline: 'Connection lost · launcher retained',
        stopping: 'Stopping safely…',
        complete: 'Setup complete',
        review: 'Source entered · awaiting your review',
        checking: 'Reading the source check',
        saving: 'Sending the save command',
        awaiting_restart: 'Waiting for a full console restart',
        failed: 'Attempt failed',
        cancelled: 'Input cancelled',
        stopped: 'Session stopped',
      }[attempt.status] || attempt.status
    : '';
  if (attempt?.progress) {
    $('progress').value = attempt.progress.line;
    $('progress').max = attempt.progress.total;
    $('progress-value').textContent =
      attempt.progress.line + ' / ' + attempt.progress.total + ' lines';
  } else {
    if (['complete', 'running', 'offline'].includes(attempt?.status))
      $('progress').value = 229;
    else $('progress').removeAttribute('value');
    $('progress-value').textContent = '';
  }
  $('progress-note').textContent =
    attempt?.status==='starting'&&(state.link?.state==='connected'||attempt.launchStage==='initializing')
      ? 'The console connection is established. FS RPC and graphics initialization are running; follow the session log below.'
    : attempt?.status==='starting'&&attempt.launchStage==='waiting-console'
      ? 'The input device finished its job. If SmileBASIC shows no text, check the USB keyboard connection and firmware. If the text is present, check the console screen and network.'
    : attempt?.kind === 'bootstrap'
      ? 'Source entry uses the original HID timings. Console execution and save results require your confirmation.'
      : attempt?.status === 'offline'
        ? state.platform?.residentGraphics
          ? 'The app set remains in memory until STOP. New modules require the control connection to be restored.'
          : 'A loaded local app may continue without Wi-Fi. Host commands and return to the launcher require a connection. STOP ends this retained session.'
      : state.connected
        ? 'You can close this window. Your console session will keep running.'
        : attempt?.status === 'failed'
          ? 'Restart the console before your next attempt.'
          : state.platform?.delivery?.preloaded
            ? 'RUN → prepare memory once → connect'
            : 'LOAD “TXT:' +
              (state.platform?.delivery?.bootstrapName ||
                (state.bootstrapProfile?.recovery ? 'NS2R' : 'NS2')) +
              '” → RUN → prepare memory once → connect';
  if (state.notification) {
    backendNotice = state.notification.at || state.notification.message;
    notice(state.notification.message, state.notification.type, backendNotice);
  } else if (backendNotice) {
    if (notificationKey === backendNotice) $('toast').hidden = true;
    backendNotice = '';
  }
  renderServerAvailability();
}
let platformMenuStamp = '',
  configurationStamp = '';
const upstreamSteps = { izmysh: 'launch' };
function fieldsFor(schema) {
  if (Array.isArray(schema)) return schema;
  if (schema?.fields) return schema.fields;
  return Object.entries(schema?.properties || schema || {}).map(([id, v]) => ({
    id,
    ...v,
  }));
}
function buildFields(container, schema, values) {
  container.replaceChildren();
  for (const field of fieldsFor(schema)) {
    const id = field.id || field.key || field.name;
    if (!id) continue;
    const label = element('label', 'configuration-field'),
      input = field.options ? element('select') : element('input');
    input.name = id;
    input.dataset.kind = field.type || 'string';
    if (field.options)
      for (const value of field.options) {
        const o = element(
          'option',
          '',
          typeof value === 'object' ? value.label : value,
        );
        o.value = typeof value === 'object' ? value.value : value;
        input.append(o);
      }
    else
      input.type = ['boolean', 'checkbox'].includes(field.type)
        ? 'checkbox'
        : field.type === 'number'
          ? 'number'
          : field.type === 'password' ? 'password' : 'text';
    if (input.type === 'checkbox') {
      input.classList.add('panel-switch');
      input.setAttribute('role', 'switch');
    }
    input.autocomplete = 'off';
    const value = values?.[id] ?? field.default ?? '';
    if (input.type === 'checkbox') input.checked = !!value;
    else input.value = value;
    label.append(element('span', '', field.label || field.title || id), input);
    if (field.description || field.help)
      label.append(element('small', 'hint', field.description || field.help));
    container.append(label);
  }
}
function fieldValues(container) {
  const value = {};
  for (const input of container.querySelectorAll('input,select'))
    value[input.name] =
      input.type === 'checkbox'
        ? input.checked
        : input.dataset.kind === 'number'
          ? Number(input.value)
          : input.value.trim();
  return value;
}
function upstreamPanel(id) {
  return document.querySelector('[data-upstream-panel="' + id + '"]');
}
function renderAutomaticRpcStatus(meta) {
  const loader = meta.loaders.find(item => item.id === meta.loaderId);
  const status = $('delivery-rpc-status'),
    automatic = meta.izModules && loader?.settingsSchema?.some(item => item.key === 'rpcMode'),
    dirty = deliveryDirty(),
    fs = state.active ? state.fsRpc : null;
  status.hidden = !automatic;
  if (!automatic) {
    setText($('delivery-rpc-note'), dirty ? 'Saving session settings…' : 'Applies to First Setup and Launch.');
    return;
  }
  status.dataset.state = fs?.state || 'automatic';
  setText($('delivery-rpc-label'), fs?.state === 'ready'
    ? 'Native + FS RPC · Active'
    : fs?.state === 'starting' ? 'FS RPC · Connecting'
    : fs?.state === 'failed' ? 'FS RPC · Unavailable' : 'Native RPC · Automatic');
  const note = dirty ? 'Saving session settings…'
    : fs?.state === 'ready' ? 'FS endpoint is active for this session.'
    : fs?.state === 'starting' ? 'Waiting for the FS endpoint.'
    : fs?.state === 'failed' ? fs.error || 'FS endpoint is unavailable for this session.'
    : state.active ? 'Session settings are locked until this session ends.'
    : meta.loaderSettings?.fsRpcMode ? 'FS endpoint is enabled for the next session. Applies to First Setup and Launch.'
    : 'Native RPC is automatic. Enable FS endpoint before starting a session.';
  setText($('delivery-rpc-note'), note);
}
function renderPlatform() {
  const meta = state?.platform;
  if (!meta) return;
  const shellId = meta.izModules ? meta.variant || 'izlauncher' : null;
  const shellName = meta.shells?.find(item => item.id === shellId)?.name || (meta.izModules ? 'IZlauncher' : 'Core only');
  for (const button of document.querySelectorAll('[data-folder="shell"]')) {
    button.hidden = !shellId;
    button.querySelector('span').textContent = shellName + ' folder';
    button.setAttribute('aria-label', shellName + ' folder');
    button.title = 'Open the selected shell folder';
  }
  $('footer-shell').textContent = shellName;
  const locked = busy || meta.locked,
    selected = meta.loaderId;
  const engineChoices = meta.loaders.filter(loader => meta.variant === 'original' || RUN21_CATALOG.engineAvailable(loader, meta.variant));
  const inventoryStamp = JSON.stringify(
    engineChoices.map((l) => [l.id, l.name, l.integrationStatus]),
  );
  const tabs = document.querySelector('.loader-tabs');
  tabs.hidden = !engineChoices.length;
  tabs.style.setProperty('--engine-count', engineChoices.length);
  if (tabs.dataset.inventory !== inventoryStamp) {
    tabs.dataset.inventory = inventoryStamp;
    tabs.replaceChildren();
    engineChoices.forEach((loader, i) => {
      const b = element('button');
      b.type = 'button';
      b.dataset.loader = loader.id;
      b.setAttribute('role', 'tab');
      b.append(
        element('span', '', String(i + 1).padStart(2, '0')),
        element('strong', '', loader.name),
      );
      b.append(element('small', 'engine-integration-status', loader.integrationStatus || ''));
      b.onclick = () => act('/api/launch-settings', { loaderId: loader.id });
      tabs.append(b);
    });
  }
  const shellSelect = $('launch-runtime'),
    shellStamp = JSON.stringify((meta.shells || []).map((p) => [p.id, p.name]));
  if (shellSelect.dataset.inventory !== shellStamp) {
    shellSelect.dataset.inventory = shellStamp;
    shellSelect.replaceChildren();
    for (const v of [
      ...(meta.shells || []),
      { id: 'original', name: 'Core only' },
    ]) {
      const b = element('button', '', v.name);
      b.type = 'button';
      b.dataset.variant = v.id;
      b.onclick = () => act('/api/launch-settings', { variant: v.id });
      shellSelect.append(b);
    }
  }
  document.querySelector('[data-step=bootstrap]').hidden =
    meta.loaders.find((l) => l.id === selected)?.menu?.sourceSetup === false;
  document.querySelector('[data-view=apps]').disabled = !meta.izModules;
  document.querySelector('[data-view=apps]').title = meta.izModules
    ? ''
    : 'Select IZlauncher or IZPSlauncher to use IZM modules';
  if (!meta.izModules && !$('apps').hidden) switchView('setup');
  $('iz-flow').hidden = !meta.izModules;
  const label = meta.loaders.find((l) => l.id === selected)?.name || selected;
  setText($('engine-folder-name'), label);
  setText($('engine-folder-path'), meta.loaders.find(loader => loader.id === selected)?.folder || 'Folder path is unavailable. Use the Engine button above.');
  const errors = $('extension-errors');
  if (errors) {
    errors.hidden = !meta.extensionErrors?.length;
    errors.textContent = (meta.extensionErrors || [])
      .map((e) => e.id + ': ' + e.error)
      .join(' · ');
  }
  $('delivery-summary').textContent = label;
  document.querySelector('[data-variant=original]').textContent = 'Core only';
  const deliverySchema = (
    meta.loaders.find((l) => l.id === selected)?.settingsSchema || []
  ).filter(
    (f) =>
      (!f.runtimes || f.runtimes.includes('izlauncher')) &&
      f.key !== 'holdRpc' &&
      f.key !== 'mode' &&
      f.key !== 'basicArity',
  );
  const deliveryForm = $('delivery-options'),
    deliverySchemaStamp = JSON.stringify([selected, deliverySchema]),
    deliveryStamp = JSON.stringify(meta.loaderSettings);
  if (deliveryForm.dataset.schema !== deliverySchemaStamp) {
    buildFields($('delivery-fields'), deliverySchema.map(field => field.key === 'fsRpcMode'
      ? {...field, description: '', help: ''} : field), meta.loaderSettings);
    const fsInput = $('delivery-fields').querySelector('[name=fsRpcMode]');
    if (fsInput) {
      fsInput.setAttribute('aria-describedby', 'delivery-rpc-note');
    }
    deliveryForm.dataset.schema = deliverySchemaStamp;
    delete deliveryForm.dataset.stamp;
  }
  if (deliveryForm.dataset.stamp !== deliveryStamp) {
    for (const input of $('delivery-fields').querySelectorAll('input,select')) {
      const field = deliverySchema.find(item => (item.id || item.key || item.name) === input.name),
        saved = meta.loaderSettings?.[input.name] ?? field?.default ?? '',
        value = input.type === 'checkbox' ? !!saved : saved,
        draft = input.type === 'checkbox' ? input.checked : input.value;
      // Preserve an unsaved edit when another panel updates the saved settings.
      if (input.dataset.savedValue === undefined || String(draft) === input.dataset.savedValue) {
        if (input.type === 'checkbox') input.checked = value;
        else input.value = value;
      }
      input.dataset.savedValue = String(value);
    }
    deliveryForm.dataset.stamp = deliveryStamp;
  }
  $('delivery-options').hidden = !meta.izModules || !deliverySchema.length;
  $('delivery-options')
    .querySelectorAll('input,select,button')
    .forEach((e) => (e.disabled = locked));
  $('launch-runtime').hidden = false;
  document.querySelectorAll('[data-loader]').forEach((button) => {
    button.classList.toggle('selected', button.dataset.loader === selected);
    const entry = meta.loaders.find(loader => loader.id === button.dataset.loader);
    const available = RUN21_CATALOG.engineAvailable(entry, meta.variant);
    const integrationStatus = button.querySelector('.engine-integration-status');
    if (integrationStatus) {
      const installing = button.dataset.loader === selected && state.active?.kind === 'bootstrap';
      integrationStatus.textContent = installing && state.active.progress
        ? 'Slot ' + (state.active.setup?.sourceSlot ?? 0) + ' · ' + state.active.progress.line + '/' + state.active.progress.total
        : available ? entry?.integrationStatus || '' : 'Core only mode';
    }
    button.setAttribute(
      'aria-selected',
      String(button.dataset.loader === selected),
    );
    button.disabled = locked || !available;
    button.classList.toggle('unavailable', !available);
    button.setAttribute('aria-disabled', String(locked || !available));
    button.title = available ? '' : 'Available in Core only mode';
  });
  document.querySelectorAll('[data-variant]').forEach((button) => {
    button.classList.toggle(
      'selected',
      button.dataset.variant === meta.variant,
    );
    button.disabled = locked;
  });
  const configuration = meta.configurations.find(
      (c) => c.id === meta.configurationId,
    ),
    stamp = JSON.stringify([
      meta.configurationId,
      configuration?.settingsSchema,
      configuration?.fields,
      meta.profiles?.[meta.configurationId],
    ]);
  if (
    stamp !== configurationStamp &&
    !$('hardware-form').contains(document.activeElement)
  ) {
    configurationStamp = stamp;
    buildFields(
      $('hardware-fields'),
      configuration?.settingsSchema || configuration?.fields || [],
      meta.profiles?.[meta.configurationId] || {},
    );
  }
  $('hardware-form')
    .querySelectorAll('input,select,button')
    .forEach((e) => (e.disabled = locked));
  renderArmSetup(meta);
  const needs = meta.configuration.setupRequired;
  $('configuration-note').textContent =
    (needs ? 'Setup required. ' : '') +
    (Array.isArray(meta.configuration.requirements)
      ? meta.configuration.requirements
          .map((x) => (typeof x === 'string' ? x : x.message || x.label || ''))
          .filter(Boolean)
          .join(' · ')
      : meta.configuration.requirements || '');
  const menuStamp = JSON.stringify(
    meta.loaders.map((l) => [l.id, l.menu, l.settingsSchema]),
  );
  if (menuStamp !== platformMenuStamp) {
    platformMenuStamp = menuStamp;
    $('upstream-menus').replaceChildren();
    for (const loader of meta.loaders) {
      const id = loader.id;
      upstreamSteps[id] ??= 'launch';
      const panel = element('section', 'installation upstream-menu');
      panel.dataset.upstreamPanel = id;
      const heading = element('div', 'section-heading'),
        title = element('div'),
        tabs = element('div', 'segmented');
      title.append(
        element('span', 'eyebrow', 'CONNECTION CORE'),
        element('h2', '', loader.menu?.title || loader.name || id),
      );
      for (const value of loader.menu?.sourceSetup === false
        ? ['launch']
        : ['bootstrap', 'launch']) {
        const button = element(
          'button',
          '',
          value === 'launch' ? 'Launch' : 'First setup',
        );
        button.type = 'button';
        button.dataset.upstreamStep = value;
        button.onclick = () => {
          upstreamSteps[id] = value;
          panel.querySelector('.up-ready').checked = false;
          renderPlatformControls();
        };
        tabs.append(button);
      }
      heading.append(title, tabs);
      panel.append(
        heading,
        element(
          'p',
          'subtitle',
          loader.menu?.description ||
            loader.menu?.subtitle ||
            loader.description ||
            '',
        ),
      );
      const fields = element('form', 'configuration-form loader-settings'),
        fieldBox = element('div');
      fieldBox.className = 'loader-fields';
      fields.append(fieldBox);
      fields.oninput = () => {
        panel.querySelector('.up-ready').checked = false;
        renderPlatformControls();
      };
      const saveSettings = async () => {
        const meta = state?.platform;
        if (busy || meta?.locked || meta?.loaderId !== id || !fieldsDirty(fieldBox, meta.loaderSettings)) return;
        const loaderSettings = {...meta.loaderSettings, ...fieldValues(fieldBox)};
        const result = await act('/api/launch-settings', {loaderSettings});
        if (state?.platform?.loaderId !== id) return;
        if (result?.ok) state.platform.loaderSettings = loaderSettings;
        // Restore confirmed settings after an error; a draft is never presented
        // as the settings that the next launch will actually use.
        buildFields(fieldBox, (loader.settingsSchema || []).filter(field => !field.runtimes || field.runtimes.includes('original')), state.platform.loaderSettings);
        delete panel.dataset.fieldsStamp;
        renderPlatformControls();
      };
      fields.onchange = saveSettings;
      fields.onsubmit = event => {event.preventDefault(); void saveSettings();};
      panel.append(fields);
      const note = element('p', 'subtitle up-description'),
        source = element('button', 'quiet up-source', 'Download source .bas');
      source.type = 'button';
      source.onclick = async () => {
        try {
          const response = await fetch(
            '/api/loader-source?id=' + encodeURIComponent(id),
            {
              headers: { 'x-iz-key': key },
            },
          );
          if (!response.ok) throw Error((await response.json()).error);
          const url = URL.createObjectURL(await response.blob()),
            link = element('a');
          link.href = url;
          link.download = id + '-bootstrap.bas';
          link.click();
          setTimeout(() => URL.revokeObjectURL(url), 1000);
        } catch (e) {
          notice(e.message);
        }
      };
      const ack = element('label', 'ack'),
        ready = element('input'),
        readyText = element(
          'span',
          '',
          'Switch is fully restarted and SmileBASIC Direct is open.',
        );
      ready.type = 'checkbox';
      ready.className = 'up-ready';
      readyText.className = 'up-ready-label';
      ready.onchange = renderPlatformControls;
      ack.append(ready, readyText);
      const actions = element('div', 'launch-row'),
        start = element('button', 'primary up-start', 'Launch'),
        saveSource = element('button', 'secondary up-save', 'Save source'),
        confirm = element(
          'button',
          'secondary up-confirm',
          'Source saved · continue',
        ),
        stop = element('button', 'soft-stop up-stop', 'Stop and return to Launch'),
        hard = element('button', 'hard-stop up-hard', '! STOP'),
        status = element('span', 'hint up-status');
      start.onclick = () =>
        act('/api/start', {
          kind: upstreamSteps[id] || 'launch',
          requestId: crypto.randomUUID(),
          screenReady: ready.checked,
          sourceReady: panel.querySelector('.up-source-ready').checked,
          moduleIds: [],
        });
      saveSource.onclick = () =>
        act('/api/setup', { action: 'save', requestId: crypto.randomUUID() });
      confirm.onclick = () =>
        act('/api/setup', {
          action: 'saved',
          requestId: crypto.randomUUID(),
          savedConfirmed: true,
        });
      stop.onclick = () => act('/api/stop', {});
      hard.onclick = async () => {
        if (hardStopping) return;
        hardStopping = true;
        try {
          await api('/api/hard-stop', {});
          await poll();
        } catch (e) {
          notice(e.message);
        } finally {
          hardStopping = false;
          renderPlatformControls();
        }
      };
      actions.append(start, saveSource, confirm, stop, hard, status);
      const sourceAck = element('label', 'ack source-preparation'),
        sourceReady = element('input'),
        sourceText = element('span');
      sourceReady.type = 'checkbox';
      sourceReady.className = 'up-source-ready';
      sourceReady.onchange = renderPlatformControls;
      sourceText.className = 'up-source-label';
      sourceAck.append(sourceReady, sourceText);
      panel.append(note, source, sourceAck, ack, actions);
      $('upstream-menus').append(panel);
    }
  }
  for (const loader of meta.loaders) {
    const panel = upstreamPanel(loader.id);
    if (!panel) continue;
    panel.hidden = meta.izModules || loader.id !== selected;
    const fieldBox = panel.querySelector('.loader-fields'),
      fieldsStamp = JSON.stringify([
        loader.id,
        loader.settingsSchema,
        selected === loader.id ? meta.loaderSettings : {},
      ]);
    if (
      panel.dataset.fieldsStamp !== fieldsStamp &&
      !panel.querySelector('.loader-settings').contains(document.activeElement)
    ) {
      panel.dataset.fieldsStamp = fieldsStamp;
      buildFields(
        fieldBox,
        (loader.settingsSchema || []).filter(f => !f.runtimes || f.runtimes.includes('original')),
        selected === loader.id ? meta.loaderSettings : {},
      );
    }
    panel.querySelector('.loader-settings').hidden = !fieldBox.children.length;
  }
  const inputName =
    meta.configurationId === 'arm-only'
      ? 'ARM PC · local input'
      : meta.configurationId === 'pc-cardputer-adv'
        ? 'Cardputer ADV'
        : 'ARM PC';
  words.arm[0] = inputName;
  words.usb[0] = inputName === 'ARM PC' ? 'ARM Type C - Switch' : 'USB · ' + inputName + ' → Switch';
  for (const row of state.checks.items) {
    const card = $('device-' + row.id);
    if (card && words[row.id])
      setText(card.querySelector('h3'), row.label || words[row.id][0]);
  }
  renderPlatformControls();
  renderEngineInstall();
}
function renderInstallationSlot() {
  if(state?.platform?.configuration?.externalWorkflow){$('installation-options').hidden=true;return;}
  const meta = state?.platform,
    installation = meta?.installation;
  const firstSetup = meta?.izModules
    ? step === 'bootstrap'
    : upstreamSteps[meta?.loaderId] === 'bootstrap';
  $('installation-options').hidden = !installation || !firstSetup;
  if (!installation) return;
  $('install-slot').value = String(installation.slot);
  $('install-slot').disabled = busy || !!meta.locked;
  $('install-slot-commands').textContent =
    installation.list + ' → source entry → ' + installation.save;
  $('install-slot-warning').querySelector('span').textContent = 'Installation can overwrite code in slot ' + installation.slot + ' and the saved file TXT:' + installation.name + '. Back up anything you need first.';
}
function renderEngineInstall() {
  const meta = state?.platform;
  if (!meta) return;
  if(meta.configuration.externalWorkflow){$('engine-install').hidden=true;return;}
  const missing = meta.engineInstalled === false;
  $('engine-install').hidden = !missing;
  const selectedEngine = meta.loaders.find((e) => e.id === meta.loaderId);
  $('engine-source').href = selectedEngine?.upstream?.repository || '#';
  $('engine-install-note').textContent =
    'Download the engine source from its published repository, or add it manually. The engine runs only when you launch a session.';
  $('engine-install-folder').textContent =
    'Manual installation: copy the complete repository into engines/' +
    (selectedEngine?.directory || meta.loaderId) + '/upstream/, then choose Check manual folder.';
  $('engine-install-folder').title = meta.engineInstallation?.folder || '';
  $('engine-download').disabled = serverUnavailable || busy || meta.locked || state.preview;
  $('engine-rescan').disabled = serverUnavailable || busy || meta.locked;
  if (missing) {
    $('iz-flow').hidden = true;
    $('installation-options').hidden = true;
    for (const p of document.querySelectorAll('[data-upstream-panel]'))
      p.hidden = true;
  }
}
function renderPlatformControls() {
  renderInstallationSlot();
  const meta = state?.platform;
  if (!meta) return;
  const locked = serverUnavailable || busy || meta.locked;
  document.querySelectorAll('[data-variant]').forEach(button => { button.disabled = locked; });
  document.querySelectorAll('[data-loader]').forEach(button => {
    const available = RUN21_CATALOG.engineAvailable(meta.loaders.find(loader => loader.id === button.dataset.loader), meta.variant);
    button.disabled = locked || !available;
    button.setAttribute('aria-disabled', String(locked || !available));
  });
  document.querySelectorAll('#delivery-options input,#delivery-options select,#delivery-options button,#hardware-form input,#hardware-form select,#hardware-form button')
    .forEach(control => { control.disabled = locked; });
  renderAutomaticRpcStatus(meta);
  for (const panel of document.querySelectorAll('[data-upstream-panel]')) {
    const id = panel.dataset.upstreamPanel,
      phase = upstreamSteps[id] || 'launch',
      preparation = state.sourcePreparation,
      active = state.active,
      locked = busy || meta.locked,
      stage = active?.setup?.stage;
    const fields = panel.querySelector('.loader-fields'),
      draft = fieldValues(fields),
      dirty = id === meta.loaderId && fieldsDirty(fields, meta.loaderSettings),
      preloaded = draft.bootstrapMode === 'loaded';
    const sourceRequired =
      phase === 'launch' && !preloaded && !preparation?.ready;
    panel.querySelectorAll('[data-upstream-step]').forEach((button) => {
      button.classList.toggle(
        'selected',
        button.dataset.upstreamStep === phase,
      );
      button.disabled = locked;
    });
    panel
      .querySelectorAll(
        '.loader-settings input,.loader-settings select,.loader-settings button',
      )
      .forEach((e) => (e.disabled = locked));
    panel.querySelector('.source-preparation').hidden = !sourceRequired;
    panel.querySelector('.up-source-label').textContent =
      'I confirm: the selected bootstrap has been saved as ' +
      (draft.bootstrapName || preparation?.name || 'NS2') +
      '.';
    panel.querySelector('.up-source-ready').disabled = locked;
    panel.querySelector('.up-source').hidden = !preparation?.name;
    panel.querySelector('.up-ready-label').textContent =
      phase === 'bootstrap'
        ? 'Fresh SmileBASIC Direct is open; slot ' +
          (preparation?.sourceSlot ?? 0) +
          ' is empty; English (US) keyboard, Caps Lock off.'
        : preloaded
          ? 'After a full restart, the bootstrap is loaded in slot 0 and Direct is open; RUN and REMOVE have not been executed.'
          : 'After a full restart, SmileBASIC Direct is open; RUN and REMOVE have not been executed.';
    if (meta.configuration?.inputAdapter === 'cardputer-wifi' &&
        meta.profiles?.[meta.configurationId]?.cardputerCapsLock === 'on') {
      const label = panel.querySelector('.up-ready-label');
      label.textContent = label.textContent.replace('Caps Lock off.', 'Caps Lock on.');
    }
    panel.querySelector('.up-description').textContent =
      phase === 'launch'
        ? preloaded
          ? 'Source is already in slot 0. The engine will provide the connection commands.'
          : 'The panel will load the saved file ' +
            (draft.bootstrapName || preparation?.name || 'bootstrap') +
            ' with LOAD confirmation, then enter RUN → REMOVE → START.'
        : 'Source will be entered into empty slot ' +
          (preparation?.sourceSlot ?? 0) +
          '. Save it separately as ' +
          (preparation?.name || 'NS2') +
          ', then restart Switch. Other slots remain unchanged.';
    const start = panel.querySelector('.up-start');
    start.textContent =
      phase === 'launch' ? 'Launch' : 'Enter source';
    start.hidden = !!active || pendingStart || !!stopTask;
    start.disabled =
      busy ||
      dirty ||
      !!state.preview ||
      !RUN21_CATALOG.engineAvailable(meta.loaders.find(loader => loader.id === id), meta.variant) ||
      !state.checks.ready ||
      !panel.querySelector('.up-ready').checked ||
      (sourceRequired && !panel.querySelector('.up-source-ready').checked) ||
      !!state.emergencyInputPending;
    panel.querySelector('.up-ready').disabled = locked;
    panel.querySelector('.up-save').hidden = stage !== 'review';
    panel.querySelector('.up-save').disabled = busy;
    panel.querySelector('.up-confirm').hidden = stage !== 'save_confirmation';
    panel.querySelector('.up-confirm').disabled = busy;
    panel.querySelector('.up-stop').hidden = !active && !pendingStart && !stopTask;
    panel.querySelector('.up-stop').disabled =
      !!stopTask || active?.status === 'stopping';
    panel.querySelector('.up-hard').hidden =
      !active && !state.emergencyInputPending;
    panel.querySelector('.up-hard').disabled = hardStopping;
    panel.querySelector('.up-status').textContent = active
      ? active.inputActive
        ? 'Entering commands…'
        : stage === 'review'
          ? 'Review the source on Switch, then save.'
          : stage === 'save_confirmation'
            ? 'Confirm saving on Switch before continuing.'
            : active.status
      : state.emergencyInputPending
        ? 'Input cancellation needs confirmation.'
        : dirty
          ? 'Saving session settings…'
          : state.preview
            ? 'Preview · device commands disabled'
            : !state.checks.ready
              ? 'Configure the connections to continue.'
              : !panel.querySelector('.up-ready').checked
                ? 'Confirm the console screen to continue'
                : sourceRequired && !panel.querySelector('.up-source-ready').checked
                  ? 'Confirm the saved SmileBASIC program to continue.'
                  : 'Ready to launch.';
  }
  renderServerAvailability();
}
document
  .querySelectorAll('[data-loader]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { loaderId: button.dataset.loader })),
  );
document
  .querySelectorAll('[data-variant]')
  .forEach(
    (button) =>
      (button.onclick = () =>
        act('/api/launch-settings', { variant: button.dataset.variant })),
  );
$('hardware-form').onsubmit = (e) => {
  e.preventDefault();
  const profile=fieldValues($('hardware-fields'));
  if(profile.cardputerEndpoint){
    const address=profile.cardputerEndpoint.trim();
    profile.cardputerEndpoint=address.includes('://')?address:'http://'+address;
  }
  act('/api/launch-settings', {
    configurationId: state.platform.configurationId,
    profile,
  });
};

let polling = null;
let initialRenderComplete = false;
let dolphinAutoAttempt = null;
let dolphinOpening = false;
let dolphinMessage = '';
function dolphinReady() {
  return !serverUnavailable && !!state?.connected && state?.active?.status === 'running' && state?.fsRpc?.state === 'ready' && !state?.preview;
}
function renderDolphin() {
  const button = $('dolphin-open'), status = $('dolphin-status');
  if (!button || !status) return;
  const ready = dolphinReady();
  button.disabled = !ready || dolphinOpening;
  button.title = ready ? 'Open Super File Bros. inside IZ2S. Choose a file manager there when needed.' : 'Available after FS RPC connects';
  setText(status, serverUnavailable ? 'Server unavailable · FS status cannot be checked.' : dolphinOpening ? 'Opening Super File Bros. tab…' : ready
    ? dolphinMessage || 'FS RPC is connected. Open the Super File Bros. tab.'
    : state?.fsRpc?.state === 'starting' ? 'Waiting for FS RPC to connect.'
    : 'Available when FS RPC is connected.');
}
async function openDolphin(automatic = false) {
  if (!dolphinReady() || dolphinOpening) return;
  if (!workspaceReady) {if(!automatic)notice('Reopen IZ2S to enable module tabs.');return;}
  const attemptId = state.active.id;
  dolphinOpening = true;
  renderDolphin();
  try {
    const request = {attemptId, automatic};
    let result;
    try { result = await api('/api/rpc/open', request); }
    catch (error) {
      // The session owner can predate this UI update; its folder route loads lazily.
      if (!/Unknown action|Not found/.test(error.message)) throw error;
      result = await api('/api/folder/open', {kind: 'rpc-browser', ...request});
    }
    if(result.tab)workspace.openRequest(result.tab,!automatic);
    dolphinMessage = result.skipped ? '' : 'Super File Bros. tab is open.';
  } catch (error) {
    dolphinMessage = error.message;
    notice('Super File Bros.: ' + error.message);
  } finally {
    dolphinOpening = false;
    renderDolphin();
  }
}
if ($('dolphin-open')) $('dolphin-open').onclick = () => openDolphin();
let fsRpcLogProof = {id: null, state: null, checked: 0};
async function readFsRpcProof(snapshot) {
  // A panel already open during an update can keep serving its previous API.
  // Read that session's persisted proof instead of restarting a live FS owner.
  if (snapshot.fsRpc !== undefined || snapshot.platform?.loaderId !== 'bermthesequel') return;
  const id = snapshot.active?.id;
  if (!id) return;
  if (fsRpcLogProof.id !== id) fsRpcLogProof = {id, state: null, checked: 0};
  if (fsRpcLogProof.state !== 'ready' && Date.now() - fsRpcLogProof.checked > 10000) {
    fsRpcLogProof.checked = Date.now();
    try {
      const result = await api('/api/log?id=' + encodeURIComponent(id), undefined, {timeoutMs: 15000});
      const messages = (result.events || []).map(event => event.message || '');
      if (messages.some(message => message === 'FS RPC ready (--rpc): native FS execution verified; endpoint retained.'))
        fsRpcLogProof.state = 'ready';
      else if (messages.includes('Preparing held FS RPC before graphics.')) fsRpcLogProof.state = 'starting';
    } catch { /* The ordinary connection state still controls availability. */ }
  }
  if (fsRpcLogProof.state) snapshot.fsRpc = {state: fsRpcLogProof.state, source: 'session-proof-log'};
}
function poll() {
  if (polling) return polling;
  if (!initialRenderComplete) {
    $('startup-retry').disabled = true;
    setText($('startup-message'), 'Connecting to the local server.');
  }
  polling = (async () => {
    let readingState = true;
    try {
      const snapshot = await api('/api/state', undefined, {timeoutMs: 15000});
      readingState = false;
      state = RUN21_CATALOG.normalizeSnapshot(snapshot);
      await readFsRpcProof(state);
      try {const tools=await api('/api/workspace',undefined,{timeoutMs:5000});workspaceReady=true;workspace.sync(tools.tabs||[]);} catch {workspaceReady=false;}
      if (serverUnavailable && notificationKey === 'server-unavailable') $('toast').hidden = true;
      serverUnavailable = false;
      render();
      renderDolphin();
      if (!initialRenderComplete) {
        initialRenderComplete = true;
        $('panel-sidebar').hidden = false;
        $('panel-main').hidden = false;
        $('panel-startup').hidden = true;
      }
      if (workspaceReady && dolphinReady() && dolphinAutoAttempt !== state.active.id) {
        dolphinAutoAttempt = state.active.id;
        dolphinMessage = '';
        void openDolphin(true);
      }
    } catch (e) {
      if (!readingState) {
        console.error('IZ2S panel rendering failed', e);
        const message = 'The panel could not display the server response. Try again or reopen IZ2S.';
        if (!initialRenderComplete) {
          setText($('startup-message'), message);
          $('startup-retry').hidden = false;
          $('startup-retry').disabled = false;
        } else notice(message, 'error', 'panel-render-failed');
        return;
      }
      serverUnavailable = true;
      if (!initialRenderComplete) {
        setText($('startup-message'), 'The local server is unavailable. Try again or reopen IZ2S.');
        $('startup-retry').hidden = false;
        $('startup-retry').disabled = false;
        return;
      }
      renderServerAvailability();
      notice(
        'The local server is unavailable. Displayed device data is outdated; the panel will retry automatically.',
        'error',
        'server-unavailable',
      );
    }
  })().finally(() => { polling = null; });
  return polling;
}
document
  .querySelectorAll('[data-view]')
  .forEach((e) =>
    e.addEventListener('click', () => switchView(e.dataset.view)),
  );
document
  .querySelectorAll('[data-step]')
  .forEach((e) =>
    e.addEventListener('click', () => chooseStep(e.dataset.step)),
  );
$('recovery-enabled').addEventListener('change', () => {
  requestId = null;
  renderWizard();
});
$('screen-ready').addEventListener('change', renderControls);
$('recheck').addEventListener('click', () => act('/api/check', {}));
$('startup-retry').addEventListener('click', () => poll());
$('start').addEventListener('click', () => {
  requestId ||= crypto.randomUUID();
  act('/api/start', {
    kind: state?.active?.setup?.stage === 'reboot' ? 'launch' : step,
    screenReady: $('screen-ready').checked,
    requestId,
    recovery: $('recovery-enabled').checked,
    moduleIds: $('preload-enabled').checked ? [...startupSelection] : [],
  });
});
$('stop').addEventListener('click', async () => {
  const result=await act('/api/stop', {});
  if(result?.ok){requestId=null;pendingSetup=null;$('screen-ready').checked=false;chooseStep('launch');}
});
$('load').addEventListener('click', () =>
  act('/api/load', { ids: [...selection] }),
);
$('about').addEventListener('click', () => $('about-dialog').showModal());
document
  .querySelectorAll('[data-close]')
  .forEach((e) =>
    e.addEventListener('click', () => $(e.dataset.close).close()),
  );
document.querySelectorAll('dialog').forEach((e) =>
  e.addEventListener('click', (event) => {
    if (event.target === e) {
      const r = e.getBoundingClientRect();
      if (
        event.clientX < r.left ||
        event.clientX > r.right ||
        event.clientY < r.top ||
        event.clientY > r.bottom
      )
        e.close();
    }
  }),
);
$('dismiss').addEventListener('click', () => {
  dismissed = notificationKey;
  $('toast').hidden = true;
  api('/api/dismiss', {}).catch(() => {});
});
document.querySelector('.brand').addEventListener('click', (e) => {
  e.preventDefault();
  switchView('setup');
});
chooseStep('launch');
poll().finally(() => (timer = setInterval(poll, 2000)));
// No unload/visibility handler: closing or hiding Chrome never ends the session.

async function setupAction(action, extra = {}) {
  if (!pendingSetup || pendingSetup.action !== action)
    pendingSetup = { action, requestId: crypto.randomUUID() };
  const result = await act('/api/setup', { ...pendingSetup, ...extra });
  if (result) pendingSetup = null;
  return result;
}
async function retrySetup(targetStep) {
  const previousKind = (state?.active || state?.lastAttempt)?.kind;
  const nextStep = typeof targetStep === 'string' ? targetStep
    : previousKind === 'bootstrap' ? 'bootstrap'
    : previousKind === 'launch' ? 'launch' : step;
  const result = await act('/api/retry', {});
  if (result?.ok) {
    requestId = null;
    pendingSetup = null;
    setupStamp = '';
    resultStamp = '';
    $('toast').hidden = true;
    dismissed = notificationKey;
    backendNotice = '';
    $('screen-ready').checked = false;
    $('review-ready').checked = false;
    $('check-lines').checked = false;
    $('saved-ready').checked = false;
    for (let i = 0; i < 6; i++) $('metric-' + i).value = '';
    chooseStep(nextStep);
  }
}
$('verify-source').addEventListener('click', () =>
  setupAction('verify', { reviewed: $('review-ready').checked }),
);
$('skip-source').addEventListener('click', () =>
  setupAction('skip', { reviewed: $('review-ready').checked }),
);
$('reverify-source').addEventListener('click', () =>
  setupAction('verify', { reviewed: true }),
);
$('compare-source').addEventListener('click', () =>
  setupAction('compare', {
    values: Array.from({ length: 6 }, (_, i) => $('metric-' + i).value),
    linesConfirmed: $('check-lines').checked,
  }),
);
$('edit-readings').addEventListener('click', () => setupAction('edit-values'));
$('save-source').addEventListener('click', () => setupAction('save'));
$('confirm-saved').addEventListener('click', () =>
  setupAction('saved', { savedConfirmed: $('saved-ready').checked }),
);
$('retry').addEventListener('click', () => retrySetup());
$('restart-setup').addEventListener('click', () => retrySetup('bootstrap'));
for (const id of [
  'review-ready',
  'check-lines',
  'saved-ready',
  ...Array.from({ length: 6 }, (_, i) => 'metric-' + i),
])
  $(id).addEventListener('input', renderControls);
$('export-source').addEventListener('click', async () => {
  try {
    const row = state.active || state.lastAttempt,
      data = await api('/api/setup-report?id=' + encodeURIComponent(row.id)),
      url = URL.createObjectURL(
        new Blob([data.text], { type: 'text/plain;charset=utf-8' }),
      ),
      a = element('a');
    a.href = url;
    a.download = 'IZlauncher-source-check-' + row.id + '.txt';
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch (e) {
    notice(e.message);
  }
});
$('log').addEventListener('scroll', () => {
  if (!logWriting && $('log').clientHeight)
    logFollow =
      $('log').scrollHeight - $('log').scrollTop - $('log').clientHeight < 40;
});
$('log-details').addEventListener('change', renderActivity);
$('download-session-log').addEventListener('click', () => {
  const id = (state.active || state.lastAttempt)?.id;
  if (id) downloadSessionLog(id);
});
$('download-saved-log').addEventListener('click', () => {
  if (savedLogAttempt) downloadSessionLog(savedLogAttempt);
});
document.querySelector('.activity').addEventListener('toggle', (event) => {
  if (event.target.open && logFollow)
    $('log').scrollTop = $('log').scrollHeight;
});

$('hard-stop').addEventListener('click', async () => {
  if (hardStopping) return;
  hardStopping = true;
  renderControls();
  try {
    await api('/api/hard-stop', {});
    await poll();
  } catch (e) {
    notice(e.message, 'error');
  } finally {
    hardStopping = false;
    renderControls();
  }
});


$('delivery-options').oninput = () => {
  $('screen-ready').checked = false;
  renderControls();
};
$('delivery-options').onsubmit = e => e.preventDefault();
$('delivery-options').onchange = async () => {
  if (!deliveryDirty() || busy || state.platform.locked) return;
  const loaderId = state.platform.loaderId,
    loaderSettings = {...state.platform.loaderSettings, ...fieldValues($('delivery-fields'))};
  const result = await act('/api/launch-settings', {loaderSettings});
  if (result?.ok && state.platform.loaderId === loaderId)
    state.platform.loaderSettings = loaderSettings;
  // A failed save restores the last confirmed value instead of leaving a draft.
  for (const input of $('delivery-fields').querySelectorAll('input,select'))
    delete input.dataset.savedValue;
  delete $('delivery-options').dataset.stamp;
  renderPlatform();
  renderControls();
};

$('install-slot').onchange = () => {
  $('screen-ready').checked = false;
  document.querySelectorAll('.up-ready').forEach((e) => (e.checked = false));
  act('/api/launch-settings', { installSlot: Number($('install-slot').value) });
};

$('engine-download').onclick = () =>
  act('/api/engine/install', { id: state.platform.loaderId });
$('engine-rescan').onclick = () => act('/api/engine/rescan', {});

for(const id of ['arm-setup-address','arm-setup-user','arm-setup-key']) $(id).oninput=()=>{$(id).dataset.edited='true';};
$('arm-install-confirm').onchange=()=>{if(state?.platform)renderArmSetup(state.platform);};
for(const mode of ['ssh','manual']) $('arm-method-'+mode).onclick=()=>{for(const item of ['ssh','manual']){$('arm-method-'+item).classList.toggle('selected',mode===item);$('arm-method-'+item).setAttribute('aria-pressed',String(mode===item));$('arm-'+item+'-pane').hidden=mode!==item;}};
$('arm-install-form').onsubmit=async(event)=>{
  event.preventDefault();
  $('arm-install-result').textContent='Installing the verified package over SSH…';
  const result=await act('/api/input/install',{address:$('arm-setup-address').value.trim(),user:$('arm-setup-user').value.trim(),identity:$('arm-setup-key').value.trim(),confirm:$('arm-install-confirm').checked});
  if(result?.ok){$('arm-install-result').textContent=result.keyboard?'Installed. Check the console USB connection before launch.':'Installed. Connect/configure the board’s USB keyboard gadget, then check again.';for(const id of ['arm-setup-address','arm-setup-user','arm-setup-key'])delete $(id).dataset.edited;}else $('arm-install-result').textContent='Installation was not confirmed. Check the error above, or use the manual package.';
};
$('arm-check').onclick=()=>act('/api/check', {});
$('arm-package-download').onclick=async()=>{try{const response=await fetch('/api/input-package',{headers:{'x-iz-key':key}});if(!response.ok)throw Error((await response.json()).error);const url=URL.createObjectURL(await response.blob()),link=element('a');link.href=url;link.download='iz2s-arm-input.tar.gz';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}catch(error){notice(error.message);}};


window.addEventListener('run21:languagechange', () => {
  if (!state) return;
  historyStamp = '';
  $('log').dataset.stamp = '';
  renderHistory();
  renderActivity();
});

for (const placeholder of document.querySelectorAll('[data-folder]')) {
  const button = folderButton(placeholder.dataset.folder, null, placeholder.textContent.trim());
  button.id = placeholder.id;
  button.dataset.folder = placeholder.dataset.folder;
  button.className += ' ' + placeholder.className;
  button.onclick = async event => {
    event.preventDefault();
    event.stopPropagation();
    const kind = placeholder.dataset.folder;
    const id = kind === 'configuration' ? state?.platform?.configurationId
      : ['engine', 'engine-source'].includes(kind) ? state?.platform?.loaderId
      : kind === 'shell' ? state?.platform?.variant || 'izlauncher' : undefined;
    button.dataset.pending = 'true';
    button.disabled = true;
    try { await api('/api/folder/open', { kind, ...(id ? { id } : {}) }); }
    catch (error) { notice(error.message); }
    finally { delete button.dataset.pending; renderLocalActionAvailability(); }
  };
  placeholder.replaceWith(button);
}

async function downloadCardputerPackage() {
  const response=await fetch('/api/cardputer/package',{headers:{'x-iz-key':key}});
  if(!response.ok)throw Error((await response.json()).error);
  const url=URL.createObjectURL(await response.blob()),link=element('a');link.href=url;link.download='iz2s-cardputer-adv-sd.zip';link.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
}
$('cardputer-install').onclick=async()=>{
  $('cardputer-setup-result').textContent='Copying to the ADV SD card…';
  const result=await act('/api/cardputer/install',{});
  $('cardputer-setup-result').textContent=result?.verified?'Copied. Start IZ2S-ADV-0.21.bin from M5Launcher.':'Copy failed. Open M5Launcher USB storage and mount its SD card.';
};
$('cardputer-file').onclick=async()=>{
  const result=await act('/api/cardputer/prepare',{});
  if(result?.ok){try{await downloadCardputerPackage();$('cardputer-setup-result').textContent='Extract the ZIP to the SD card, then start IZ2S-ADV-0.21.bin in M5Launcher.';}catch(error){notice(error.message);}}
};
$('cardputer-check').onclick=()=>act('/api/check',{});
$('adv-usb-check').onclick=()=>act('/api/check',{});
$('adv-address').onclick=()=>{$('hardware-options').open=true;$('hardware-options').querySelector('[name="cardputerEndpoint"]')?.focus();};

} // Ordinary panel; the standalone files preview above never starts polling.
