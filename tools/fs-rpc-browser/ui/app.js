'use strict';
const $ = id => document.getElementById(id), key = location.hash.slice(1);
let games = [], connected = false, state = {status:'checking'}, busy = false, cwd = '/', lastStorageLease = '', connectionHost = '', connectionId = '', pollError = false;
let folderHistory = [], historyIndex = -1, entries = [], selectedPath = '', view = 'storage', folderLease = '', browsing = false, revision = 0, operation = '', pendingPath = '/', downloadsStamp = '';
const storageReady=()=>connected&&state.status==='ready'&&state.mode==='storage';
const canWrite=()=>storageReady()&&folderLease===state.lease&&folderWritable&&!browsing;
async function api(route, data) {
  const controller = new AbortController(), timer = setTimeout(() => controller.abort(), data === undefined ? 15000 : 150000);
  try {
    const response = await fetch(route, {method:data === undefined ? 'GET' : 'POST', signal:controller.signal,
      headers:{'x-fs-key':key, ...(data === undefined ? {} : {'content-type':'application/json'})},
      ...(data === undefined ? {} : {body:JSON.stringify(data)})});
    const result = await response.json(); if (!response.ok) throw Error(result.error || 'Request failed'); return result;
  } catch (error) {
    if (error.name === 'AbortError') throw Error('The request timed out. Check the current status before trying again.');
    throw error;
  } finally {clearTimeout(timer);}
}
function notice(text,tone='info') {$('notice').textContent = text;$('notice').dataset.tone=tone;}
async function action(fn,label='') {
 if(busy)return;busy=true;operation=label;revision++;document.body.setAttribute('aria-busy','true');renderStatus();
 try{await fn();}catch(error){notice(error.message,'error');}
 finally{busy=false;operation='';document.body.removeAttribute('aria-busy');renderStatus();}
}
function icon(name){const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),use=document.createElementNS('http://www.w3.org/2000/svg','use');use.setAttribute('href','#'+name+'-icon');svg.setAttribute('aria-hidden','true');svg.append(use);return svg;}
function emptyFiles(title,detail){const box=document.createElement('div');box.className='empty-state';const h=document.createElement('h3'),p=document.createElement('p');h.textContent=title;p.textContent=detail;box.append(icon('folder'),h,p);$('files').replaceChildren(box);}
function setFtpPanel(open){$('ftp-panel').hidden=!open;$('ftp-toggle').setAttribute('aria-expanded',String(open));}
function formatSize(bytes){if(!Number.isFinite(bytes))return '—';const units=['B','KB','MB','GB','TB'];let i=0;while(bytes>=1024&&i<units.length-1){bytes/=1024;i++;}return (i?bytes.toFixed(bytes>=10?1:2):bytes)+' '+units[i];}
function row(name, detail, label, callback) {
  const item = document.createElement('div'); item.className = 'game';
  const text = document.createElement('div'), title = document.createElement('strong'), sub = document.createElement('small');
  title.setAttribute('translate','no'); title.textContent = name; sub.textContent = detail; text.append(title, sub); item.append(text);
  if (label) {const button = document.createElement('button'); button.textContent = label; button.onclick = () => action(callback); item.append(button);}
  return item;
}
function storageLabel(storages = []) {
  return [...new Set(storages)].map(id => ({2: 'Game card', 4: 'Console storage', 5: 'SD card'})[id] || 'Unknown storage').join(' + ') || 'Storage unknown';
}
function renderGames() {
 $('games').replaceChildren();const search=$('search').value.toLowerCase();
 for(const game of games.filter(game=>(game.name+game.title).toLowerCase().includes(search))){
  const current=state.mode==='game'&&state.title===game.title&&['ready','preparing'].includes(state.status);
  const item=row(game.name,game.title.toUpperCase()+' · '+storageLabel(game.storages),current?(state.status==='ready'?'FTP ready':'Connecting…'):'Connect FTP',async()=>{
   if(current){setFtpPanel(true);return;}
   folderLease='';lastStorageLease='';folderWritable=false;state={...state,status:'preparing',mode:'game',title:game.title,name:game.name,error:''};setFtpPanel(true);renderStatus();
   state=await api('/api/start',{title:game.title});renderGames();notice('Preparing read-only game FTP…');
  });item.classList.toggle('current',current);$('games').append(item);
 }
 if(!$('games').children.length)$('games').append(row(connected?'No matching games':'Connect to load installed games','','',null));
}
function renderStatus() {
 const ready=storageReady(),folderReady=ready&&folderLease===state.lease,sessionChecking=operation==='detect';
 $('session-status').dataset.state=sessionChecking?'checking':connected?'ready':'unavailable';
 $('session-status').textContent=sessionChecking?'Checking session…':connected?'Filesystem connected':'Filesystem unavailable';
 $('connection-title').textContent=connected?'Console filesystem connected':'Connect to your console';
 $('connection-help').textContent=connected?'Browse in this window. Use FTP access for an external file manager.':'Start a session with Filesystem access enabled in IZ2S Platform, then reconnect here.';
 const ftpState=connected?state.status:'unavailable',labels={checking:'Checking…',ready:'FTP ready',idle:'FTP disconnected',closed:'FTP disconnected',failed:'FTP failed',unavailable:'FTP unavailable',preparing:'Connecting FTP…',stopping:'Disconnecting FTP…'};
 $('ftp-short-status').textContent=labels[ftpState]||'FTP disconnected';$('ftp-indicator').dataset.state=ftpState;
 $('export-name').textContent=state.name||'No active export';$('export-status').textContent=labels[ftpState]||'Idle';$('export-status').dataset.state=ftpState;
 $('progress').textContent=(['failed','unavailable'].includes(state.status)?state.error:'')||state.progress||(state.status==='ready'?'FTP is ready. Copy the address or open a file manager.':'Connect storage or select a game first.');
 const manager=$('manager').value,needsPath=manager==='custom'&&!$('executable').value.trim();
 $('open').disabled=busy||!connected||state.status!=='ready'||!manager||needsPath;
 $('open').title=!connected?'Connect to the console first.':state.status!=='ready'?'Connect storage or a game first.':!manager?'Choose a file manager first.':needsPath?'Enter the file manager executable path.':'Open in file manager';
 $('stop').disabled=busy||!connected||!['ready','preparing'].includes(state.status);
 $('executable').parentElement.hidden=!manager||['system','explorer','finder'].includes(manager);
 $('executable').required=manager==='custom';$('executable').parentElement.querySelector('.muted').textContent=manager==='custom'?'(required)':'(optional)';
 $('detect').disabled=busy;$('refresh').disabled=busy||!connected;
 $('ftp-url').hidden=!connected||state.status!=='ready'||!state.url;$('ftp-url').textContent=state.url||'';$('ftp-url').href=state.url||'#';$('copy-ftp').hidden=$('ftp-url').hidden;
 $('mounts').textContent=(state.mounts||[]).map(name=>'/'+name).join(' · ');
 $('storage-start').hidden=view!=='storage'||ready;$('storage-start').disabled=!connected||busy||['preparing','stopping'].includes(state.status);
 $('storage-start').textContent=state.status==='preparing'&&state.mode==='storage'?'Connecting storage…':'Connect storage';
 $('browse-title').textContent=view==='games'?'Installed games':'Console storage';
 $('folder-state').textContent=view==='games'?'Read-only FTP':browsing?'Loading…':!folderReady?(ready?'Loading…':state.status==='preparing'&&state.mode==='storage'?'Connecting…':'Not connected'):folderWritable?'Read & write':'Read-only';$('folder-state').dataset.writable=String(view==='storage'&&folderReady&&folderWritable);
 $('folder-loading').hidden=!browsing;$('file-area').setAttribute('aria-busy',String(browsing));
 $('download-folder').disabled=!folderReady||busy;$('up').disabled=!folderReady||busy||cwd==='/';
 $('back').disabled=!folderReady||busy||historyIndex<=0;$('forward').disabled=!folderReady||busy||historyIndex>=folderHistory.length-1;
 $('path-input').disabled=!ready||busy;$('path-go').disabled=!ready||busy;
 for(const id of ['upload-files','upload-folder','new-folder'])$(id).disabled=!canWrite()||busy||uploading;
 $('upload-files').title=!ready?'Connect storage first.':!folderWritable?'Open SD card to upload files.':'Upload files to this folder';
 $('write-status').textContent=!connected?'Filesystem access is required to browse console files.':!ready?(state.status==='preparing'?'Connecting storage. Wait for folders to appear.':'Connect storage to browse files and upload to SD.'):!folderReady?'Loading the current storage view…':folderWritable?'SD card · Files can be uploaded, renamed and deleted.':'Read-only folder · Open SD card to upload or manage files.';
 $('write-status').dataset.writable=String(folderReady&&folderWritable);$('open-sd').hidden=!folderReady||folderWritable||!(state.mounts||[]).includes('sd');$('open-sd').disabled=busy;
 for(const button of document.querySelectorAll('#files button,#files summary')){if(button.tagName==='BUTTON')button.disabled=!folderReady||busy||uploading;else{button.setAttribute('aria-disabled',String(!folderReady||busy||uploading));button.tabIndex=!folderReady||busy?-1:0;}}
 for(const button of document.querySelectorAll('#games button'))button.disabled=!connected||busy||['preparing','stopping'].includes(state.status);
 for(const button of document.querySelectorAll('#breadcrumbs button'))button.disabled=!folderReady||busy||button.hasAttribute('aria-current');
 for(const button of document.querySelectorAll('[data-path]')){button.disabled=!connected||busy||['preparing','stopping'].includes(state.status);button.classList.toggle('active',view==='storage'&&folderReady&&(button.dataset.path==='/'?cwd==='/':cwd===button.dataset.path||cwd.startsWith(button.dataset.path+'/')));}
 if(!folderReady)$('file-count').textContent=ready?'Loading folder…':connected&&state.status==='preparing'?'Connecting to storage…':'No folder open';
}
async function download(target) {
  const result = await api('/api/download', {path: target});
  const link = document.createElement('a'); link.href = result.url; link.download = result.name;
  document.body.append(link); link.click(); link.remove();
  notice('Downloading ' + result.name + '. Your browser saves the file to its download folder.');
  await renderDownloads();
}
async function renderDownloads() {
  const records = await api('/api/downloads');
  const stamp=JSON.stringify(records);if(stamp===downloadsStamp)return;downloadsStamp=stamp;
  $('downloads').hidden = !records.length; $('download-items').replaceChildren();
  for (const record of records.slice().reverse()) {
    const size = (record.bytes / 1048576).toFixed(2) + ' MB';
    const total = record.totalBytes === null ? '' : ' / ' + (record.totalBytes / 1048576).toFixed(2) + ' MB';
    const active = ['queued', 'starting', 'downloading'].includes(record.status);
    const item = row(record.name, record.status + ' · ' + size + total + ' · ' + record.files + ' files' + (record.error ? ' · ' + record.error : ''),
      active ? 'Cancel' : '', () => api('/api/download/cancel', {id: record.id}));
    if (record.currentFile && active) {const current = document.createElement('small'); current.setAttribute('translate','no'); current.textContent = record.currentFile; item.firstChild.append(current);}
    $('download-items').append(item);
  }
}
let folderWritable = false, uploading = false, cancelUpload = false, uploadToken = null;
function fileDialog(title, text, name, label = 'Save') {
  $('dialog-title').textContent = title; $('dialog-text').textContent = text;
  $('dialog-label').hidden = name === null; $('dialog-name').required = name !== null; $('dialog-name').value = name || ''; $('dialog-name').setCustomValidity('');
  $('dialog-confirm').textContent = label; $('file-dialog').showModal();
  return new Promise(resolve => {
    $('file-dialog').onclose = () => resolve(null);
    $('dialog-cancel').onclick = () => $('file-dialog').close();
    $('file-dialog-form').onsubmit = event => {event.preventDefault();
      const value = name === null ? true : $('dialog-name').value.trim();
      if (name !== null && (!value || /[\x00-\x1f\x7f/\\:]/.test(value) || ['.', '..'].includes(value))) {$('dialog-name').setCustomValidity('Enter one file or folder name'); $('dialog-name').reportValidity(); return;}
      $('file-dialog').onclose = null; $('file-dialog').close(); resolve(value);
    };
    $('dialog-name').oninput = () => $('dialog-name').setCustomValidity('');
  });
}
async function mutateEntry(entry, target, operation) {
  if (operation === 'rename') {
    const name = await fileDialog('Rename', entry.name, entry.name); if (!name) return;
    await api('/api/sd/rename', {path: target, to: (cwd === '/' ? '' : cwd) + '/' + name, lease: state.lease});
    notice('Renamed to ' + name);
  } else {
    if (!await fileDialog('Delete ' + (entry.type === 'dir' ? 'folder' : 'file') + '?', 'Permanently delete ' + target + (entry.type === 'dir' ? ' and everything inside it?' : '?') + ' This cannot be undone.', null, 'Delete permanently')) return;
    await api('/api/sd/delete', {path: target, confirmed: true, lease: state.lease}); notice('Deleted ' + entry.name);
  }
  await browse(cwd);
}
async function uploadFiles(files) {
  if(!files.length)return;if(!canWrite())throw Error('Open a writable SD folder before uploading.');
  uploading = true; cancelUpload = false; $('upload-progress').hidden = false;
  const base = cwd, lease = state.lease, total = files.reduce((sum, file) => sum + file.size, 0); let transferred = 0;
  const directories = new Set();
  try {
    for (const file of files) {
      if (cancelUpload) throw Error('Upload cancelled');
      const relative = file.webkitRelativePath || file.name;
      if (relative.split('/').some(part => !part || ['.', '..'].includes(part) || /[\x00-\x1f\x7f\\:]/.test(part))) throw Error('Invalid upload path');
      const target = base + '/' + relative, parent = target.slice(0, target.lastIndexOf('/'));
      if (parent !== base && !directories.has(parent)) {await api('/api/sd/mkdir', {path: parent, parents: true, lease}); directories.add(parent);}
      $('upload-name').textContent = relative;
      const opened = await api('/api/sd/upload-begin', {path: target, size: file.size, lease}); uploadToken = opened.token;
      const chunkBytes = Number.isInteger(opened.chunkBytes) && opened.chunkBytes > 0 && opened.chunkBytes <= 458752 ? opened.chunkBytes : 32768;
      for (let offset = 0; offset < file.size;) {
        if (cancelUpload) throw Error('Upload cancelled');
        const bytes = new Uint8Array(await file.slice(offset, offset + chunkBytes).arrayBuffer());
        const binary = []; for (let at = 0; at < bytes.length; at += 32768) binary.push(String.fromCharCode(...bytes.subarray(at, at + 32768)));
        const data = btoa(binary.join(''));
        const written = await api('/api/sd/upload-chunk', {token: uploadToken, offset, data});
        if (written.bytes !== offset + bytes.length) throw Error('Upload acknowledgement differs from the requested range');
        offset += bytes.length; transferred += bytes.length;
        $('upload-meter').value = total ? transferred / total : 1;
        $('upload-detail').textContent = (transferred / 1048576).toFixed(2) + ' / ' + (total / 1048576).toFixed(2) + ' MB';
      }
      if (cancelUpload) throw Error('Upload cancelled');
      await api('/api/sd/upload-finish', {token: uploadToken}); uploadToken = null;
    }
    $('upload-meter').value = 1; $('upload-detail').textContent = 'Upload complete · ' + files.length + ' files'; notice('Upload complete');
  } catch (error) {
    if (uploadToken) {await api('/api/sd/upload-cancel', {token: uploadToken}).catch(cleanup => {error.message += '. Cleanup: ' + cleanup.message;}); uploadToken = null;}
    $('upload-detail').textContent = error.message; throw error;
  } finally {uploading=false;$('upload-cancel').disabled=true;if(storageReady()&&state.lease===lease)await browse(base).catch(error=>notice(error.message,'error'));}
}
async function browse(target='/',{record=true,index}={}) {
 if(!storageReady())throw Error('Connect storage before browsing.');
 const lease=state.lease,session=connectionId;browsing=true;renderStatus();
 try{
  const result=await api('/api/browse',{path:target});
  if(!connected||lease!==state.lease||session!==connectionId)throw Error('The console session changed. Reconnect before browsing.');
  const changedFolder=cwd!==result.cwd;cwd=result.cwd;entries=result.entries;folderWritable=result.writable===true;folderLease=lease;lastStorageLease=lease;selectedPath='';
  if(record&&folderHistory[historyIndex]!==cwd){folderHistory=folderHistory.slice(0,historyIndex+1);folderHistory.push(cwd);historyIndex=folderHistory.length-1;}
  else if(Number.isInteger(index))historyIndex=index;
  sessionStorage.setItem('storagePath',cwd);$('path').textContent=cwd;$('path-input').value=cwd;renderBreadcrumbs();renderFiles();if(changedFolder)$('files').scrollTop=0;
 }finally{browsing=false;renderStatus();}
}
function selectEntry(target){selectedPath=target;for(const row of $('files').children)row.classList.toggle('selected',row.dataset.path===target);$('selection-status').textContent=target.split('/').pop()||'Folders open with one click';$('selection-status').setAttribute('translate',target?'no':'yes');}
function renderFiles(){
 const query=$('file-search').value.toLowerCase(),sort=$('file-sort').value;
 const list=entries.filter(e=>e.name.toLowerCase().includes(query)).slice().sort((a,b)=>{
  const kind=(a.type==='dir'?0:1)-(b.type==='dir'?0:1);if(kind)return kind;
  if(sort==='size'&&a.type!=='dir')return (b.size||0)-(a.size||0)||a.name.localeCompare(b.name,undefined,{numeric:true});
  return a.name.localeCompare(b.name,undefined,{numeric:true,sensitivity:'base'})*(sort==='name-desc'?-1:1);
 });$('files').replaceChildren();
 for(const entry of list){
  const target=(cwd==='/'?'':cwd)+'/'+entry.name,item=document.createElement('div');item.className='file-row';item.dataset.path=target;item.dataset.kind=entry.type;item.setAttribute('role','listitem');item.classList.toggle('selected',selectedPath===target);
  const name=document.createElement('button'),caption=document.createElement('span');name.className='file-name';name.title=entry.name;caption.textContent=entry.name;caption.setAttribute('translate','no');name.append(icon(entry.type==='dir'?'folder':'file'),caption);
  name.onclick=()=>{selectEntry(target);if(entry.type==='dir')action(()=>browse(target),'browse');};
  const size=document.createElement('span');size.className='file-size';size.textContent=entry.type==='dir'?'Folder':formatSize(entry.size);
  const actions=document.createElement('div');actions.className='row-actions';const downloadButton=document.createElement('button');downloadButton.append(icon('download'));downloadButton.title=entry.type==='dir'?'Download folder as ZIP':'Download file';downloadButton.setAttribute('aria-label',downloadButton.title+': '+entry.name);downloadButton.onclick=()=>action(()=>download(target));actions.append(downloadButton);
  if(folderWritable){const menu=document.createElement('details'),summary=document.createElement('summary'),options=document.createElement('div');menu.className='row-menu';menu.ontoggle=()=>{if(menu.open){menu.classList.remove('open-up');const bounds=options.getBoundingClientRect(),list=$('files').getBoundingClientRect();menu.classList.toggle('open-up',bounds.bottom>list.bottom);}};summary.textContent='⋯';summary.setAttribute('aria-label','Actions for '+entry.name);summary.onclick=event=>{if(busy||!storageReady())event.preventDefault();};menu.append(summary,options);
   for(const operation of ['rename','delete']){const control=document.createElement('button');control.textContent=operation==='rename'?'Rename':'Delete';if(operation==='delete')control.className='danger-quiet';control.onclick=()=>{menu.open=false;action(()=>mutateEntry(entry,target,operation));};options.append(control);}actions.append(menu);}
  item.append(name,size,actions);item.ondblclick=event=>{if(entry.type==='dir'&&!event.target.closest('button,details'))action(()=>browse(target),'browse');};$('files').append(item);
 }
 if(!list.length)emptyFiles(query?'No matching files':'This folder is empty',query?'Try a different name.':folderWritable?'Upload files or drop them here to get started.':'There are no files in this folder.');
 const folders=entries.filter(e=>e.type==='dir').length;$('file-count').replaceChildren(document.createTextNode(list.length+' / '+entries.length+' '),Object.assign(document.createElement('span'),{textContent:'items'}),document.createTextNode(' · '+folders+' '),Object.assign(document.createElement('span'),{textContent:'folders'}));$('selection-status').textContent='Folders open with one click';$('selection-status').removeAttribute('translate');renderStatus();
}
async function connectStorage(target='/'){
 pendingPath=target;switchFilesView('storage');if(storageReady())return browse(target);
 if(state.mode==='storage'&&state.status==='preparing')return;
 folderLease='';lastStorageLease='';folderWritable=false;folderHistory=[];historyIndex=-1;state={...state,mode:'storage',status:'preparing',error:'',name:'SD & console storage'};emptyFiles('Connecting storage…','Keep the console session running.');renderStatus();
 state=await api('/api/start',{mode:'storage'});notice('Connecting storage. Folders will appear automatically when ready.');
}
function renderBreadcrumbs() {
  const container=$('breadcrumbs');container.replaceChildren();
  const parts=cwd.split('/').filter(Boolean),paths=[{name:'Storage',path:'/'}];
  parts.forEach((name,index)=>paths.push({name,path:'/'+parts.slice(0,index+1).join('/')}));
  for(const [index,part]of paths.entries()){
    if(index){const separator=document.createElement('span');separator.textContent='›';separator.setAttribute('aria-hidden','true');container.append(separator);}
    const button=document.createElement('button');if(index)button.setAttribute('translate','no');button.textContent=part.name;button.title=part.path;button.disabled=index===paths.length-1;
    if(button.disabled)button.setAttribute('aria-current','page');button.onclick=()=>action(()=>browse(part.path));container.append(button);
  }
}
function acceptList(result) {
  if(connectionId&&(result.connection?.id||result.session.id)!==connectionId){folderLease='';lastStorageLease='';folderWritable=false;entries=[];folderHistory=[];historyIndex=-1;emptyFiles('Console session changed','Connect storage to open the current console files.');}
  connected = true; games = result.games; connectionHost = result.session.host; connectionId = result.connection?.id || result.session.id || '';
  
  pollError=false;notice(state.status==='ready'?(state.mode==='storage'?'Console storage connected.':'FTP connected. Open it in an external file manager.'):'Filesystem connected. Choose Connect storage to open your folders.'); renderGames(); renderStatus();
}
async function detect() {
  notice('Detecting an active FS RPC session…');
  try {
    const result = await api('/api/detect', {});
    state=await api('/api/status');
    if(state.sessionAvailable===false)throw Error(state.error||'Filesystem service is unavailable.');
    acceptList(result);
  }
  catch (error) {connected = false; games = []; folderWritable=false;folderLease='';lastStorageLease='';entries=[];folderHistory=[];historyIndex=-1;emptyFiles('Console disconnected','Reconnect to the filesystem session to continue.');state={...state,status:'unavailable',error:error.message};renderGames(); renderStatus(); throw error;}
}
$('detect').onclick=()=>action(detect,'detect');
$('refresh').onclick=()=>action(async()=>{if(view==='storage'&&storageReady())await browse(cwd);else acceptList(await api('/api/list',{}));});
$('search').oninput = renderGames;
function switchFilesView(mode) {
  view=mode;sessionStorage.setItem('filesView',mode);
  for(const value of ['games','storage']){const selected=mode===value,button=$(value+'-tab');$(value+'-view').hidden=!selected;button.classList.toggle('selected',selected);button.setAttribute('aria-selected',String(selected));button.tabIndex=selected?0:-1;}renderStatus();
}
for(const mode of ['games','storage'])$(mode+'-tab').onclick=()=>switchFilesView(mode);
document.querySelector('.tabs').onkeydown=event=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;event.preventDefault();const mode=event.key==='Home'?'storage':event.key==='End'?'games':$('games-view').hidden?'games':'storage';switchFilesView(mode);$(mode+'-tab').focus();};
$('storage-start').onclick=()=>action(()=>connectStorage('/'),'connect');
$('ftp-toggle').onclick=()=>setFtpPanel($('ftp-panel').hidden);
$('open-sd').onclick=()=>action(()=>connectStorage('/sd'));
for(const button of document.querySelectorAll('[data-path]'))button.onclick=()=>action(()=>connectStorage(button.dataset.path));
$('file-search').oninput=renderFiles;$('file-sort').onchange=renderFiles;
$('path-form').onsubmit=event=>{event.preventDefault();const target=$('path-input').value.trim();if(target.startsWith('/'))action(()=>browse(target),'browse');else notice('Enter an absolute path starting with /.','error');};
$('download-folder').onclick = () => action(() => download(cwd));
$('upload-files').onclick = () => {$('upload-menu').open=false;$('file-picker').click();};
$('upload-folder').onclick = () => {$('upload-menu').open=false;$('folder-picker').click();};
for (const id of ['file-picker', 'folder-picker']) $(id).onchange = () => {const files = [...$(id).files]; $(id).value = ''; $('upload-cancel').disabled = false; action(() => uploadFiles(files));};
$('upload-cancel').onclick = () => {cancelUpload = true; $('upload-detail').textContent = 'Cancelling upload…';};
$('new-folder').onclick = () => action(async () => {if(!canWrite())throw Error('Open a writable SD folder before uploading.');const name = await fileDialog('New folder', 'Create a folder in ' + cwd, ''); if (!name) return; await api('/api/sd/mkdir', {path: cwd + '/' + name, lease: state.lease}); await browse(cwd);});
$('back').onclick=()=>action(()=>browse(folderHistory[historyIndex-1],{record:false,index:historyIndex-1}));
$('forward').onclick=()=>action(()=>browse(folderHistory[historyIndex+1],{record:false,index:historyIndex+1}));

document.addEventListener('click',event=>{if(!event.target.closest('#upload-menu'))$('upload-menu').open=false;});
document.addEventListener('keydown',event=>{if(event.key==='Escape'&&$('upload-menu').open){$('upload-menu').open=false;$('upload-menu').querySelector('summary').focus();}});
$('copy-ftp').onclick=()=>action(async()=>{try{await navigator.clipboard.writeText(state.url);notice('FTP address copied.');}catch{notice('Copy this FTP address: '+state.url);}});
$('up').onclick = () => action(() => browse(cwd.split('/').slice(0, -1).join('/') || '/'));
$('stop').onclick=()=>action(async()=>{state={...state,status:'stopping'};renderStatus();state=await api('/api/stop',{lease:state.lease});folderWritable=false;folderLease='';lastStorageLease='';entries=[];folderHistory=[];historyIndex=-1;emptyFiles('Storage disconnected','Connect storage to browse files.');renderGames();renderStatus();notice('FTP disconnected. Connect storage or a game to continue.');});
$('open').onclick = () => action(async () => {await api('/api/open', {manager: $('manager').value, executable: $('executable').value}); notice('Opened in your selected file manager.');});
async function init() {
  const result = await api('/api/settings');
  if(new URLSearchParams(location.search).get('embedded')==='1')document.body.classList.add('embedded');
  if (result.panelUrl && !document.body.classList.contains('embedded')) {
    const target = new URL(result.panelUrl);
    if (target.protocol === 'http:' && target.hostname === '127.0.0.1' && target.pathname === '/') {
      $('panel-link').href = target.href; $('panel-link').hidden = false;
    }
  }
  const blank = document.createElement('option'); blank.value = ''; blank.textContent = 'Choose a file manager'; $('manager').append(blank);
  for (const manager of result.fileManagers) {const option = document.createElement('option'); option.value = manager.id; option.textContent = manager.name; $('manager').append(option);}
  $('manager').value = result.settings.manager || '';
  $('executable').value = result.settings.executable || '';
  const paths=new Map([[$('manager').value,$('executable').value]]);
  $('manager').onchange=()=>{$('executable').value=paths.get($('manager').value)||'';$('manager-path').open=$('manager').value==='custom';renderStatus();};
  $('executable').oninput=()=>{paths.set($('manager').value,$('executable').value);renderStatus();};
  await action(detect,'detect');
  switchFilesView(sessionStorage.getItem('filesView')==='games'?'games':'storage');
  renderStatus();
}
let polling=false;
async function poll(){
 if(polling||busy)return;polling=true;const started=revision;
 try{
  const next=await api('/api/status');if(started!==revision||busy)return;
  const changed=connectionId&&next.sessionId&&connectionId!==next.sessionId;
  const recovered=['failed','unavailable'].includes(state.status)&&next.status==='ready';
  const gameChanged=next.status!==state.status||next.title!==state.title;state=next;if(gameChanged&&view==='games')renderGames();
  if(gameChanged&&next.status==='failed')notice(next.error||'FTP connection failed. Try reconnecting.','error');
  else if(gameChanged&&next.status==='ready'&&next.mode==='game')notice('FTP connected. Open it in an external file manager.');
  else if(recovered)notice('Console storage connected.');
  if(changed||next.sessionAvailable===false){
   if(connected){connected=false;games=[];folderWritable=false;folderLease='';lastStorageLease='';entries=[];folderHistory=[];historyIndex=-1;renderGames();emptyFiles('Console disconnected','Reconnect to the filesystem session to continue.');notice(changed?'The console session changed. Reconnect to continue.':next.error||'Filesystem service is unavailable.','error');}
  }else if(pollError&&next.sessionId===connectionId){connected=true;pollError=false;notice('Filesystem connection restored.');}
  renderStatus();await renderDownloads().catch(error=>notice(error.message,'error'));
  if(started!==revision||busy)return;
  if(storageReady()&&lastStorageLease!==state.lease){
   lastStorageLease=state.lease;folderHistory=[];historyIndex=-1;
   await action(async()=>{try{await browse(pendingPath||'/');}catch(error){if(pendingPath==='/')throw error;await browse('/');}notice('Console storage connected.');},'browse');pendingPath='/';
  }
 }catch(error){pollError=true;connected=false;folderWritable=false;folderLease='';lastStorageLease='';state={...state,status:'unavailable',error:error.message};renderStatus();notice(error.message,'error');}
 finally{polling=false;}
}
setInterval(poll,1000);
const dropArea=$('file-area');let dragDepth=0;
dropArea.addEventListener('dragenter',event=>{if(!event.dataTransfer?.types.includes('Files'))return;event.preventDefault();if(canWrite()&&!busy){dragDepth++;dropArea.classList.add('drag-over');}});
dropArea.addEventListener('dragover',event=>{if(event.dataTransfer?.types.includes('Files')){event.preventDefault();event.dataTransfer.dropEffect=canWrite()&&!busy?'copy':'none';}});
dropArea.addEventListener('dragleave',()=>{if(--dragDepth<=0){dragDepth=0;dropArea.classList.remove('drag-over');}});
dropArea.addEventListener('drop',event=>{event.preventDefault();dragDepth=0;dropArea.classList.remove('drag-over');if(!canWrite()||busy){notice('Open a writable SD folder before uploading.','error');return;}if([...event.dataTransfer.items].some(item=>item.webkitGetAsEntry?.()?.isDirectory)){notice('Use Upload folder to include all files and subfolders.');return;}action(()=>uploadFiles([...event.dataTransfer.files]));});
document.addEventListener('click',event=>{for(const menu of document.querySelectorAll('.row-menu[open]'))if(!menu.contains(event.target))menu.open=false;});
document.addEventListener('keydown',event=>{if($('file-dialog').open)return;if(view==='storage'&&!$('path-input').disabled&&(event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='l'){event.preventDefault();$('path-input').focus();$('path-input').select();}if(event.altKey&&event.key==='ArrowUp'&&!$('up').disabled){event.preventDefault();$('up').click();}});
init().catch(error=>{connected=false;state={...state,status:'unavailable',error:error.message};renderStatus();notice(error.message,'error');});
