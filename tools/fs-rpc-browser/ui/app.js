'use strict';
const $ = id => document.getElementById(id), key = location.hash.slice(1);
let games = [], connected = false, state = {status:'checking'}, busy = false, cwd = '/', lastStorageLease = '', connectionHost = '', connectionId = '', pollError = false;
let folderHistory = [], historyIndex = -1;
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
function notice(text) {$('notice').textContent = text;}
async function action(fn) {if (busy) return; busy = true; document.body.setAttribute('aria-busy','true'); renderStatus(); try {await fn();} catch (error) {notice(error.message);} finally {busy = false; document.body.removeAttribute('aria-busy'); renderStatus();}}
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
  $('games').replaceChildren(); const search = $('search').value.toLowerCase();
  for (const game of games.filter(game => (game.name + game.title).toLowerCase().includes(search))) {
    $('games').append(row(game.name, game.title.toUpperCase() + ' · ' + storageLabel(game.storages), 'Connect', async () => {state = await api('/api/start', {title: game.title}); renderStatus();}));
  }
  if (!$('games').children.length) $('games').append(row(connected ? 'No matching games' : 'Connect to load installed games', '', '', null));
}
function renderStatus() {
  const manager=$('manager').value,needsPath=manager==='custom'&&!$('executable').value.trim();
  $('export-name').textContent = state.name || 'Nothing connected'; $('export-status').textContent = ({checking:'Checking…',ready:'Ready',idle:'Idle',closed:'Disconnected',failed:'Failed',unavailable:'Unavailable',preparing:'Preparing…',stopping:'Stopping…'})[state.status] || state.status || 'Idle';
  $('export-status').dataset.state=state.status||'idle';
  $('progress').textContent = state.error || state.progress || (state.status==='ready'?(state.mode==='storage'?'Open SD & console storage to browse files.':'Ready to browse in your file manager.'):'Choose a game or open SD & console storage.');
  $('open').disabled = busy || !connected || state.status !== 'ready' || !manager || needsPath; $('stop').disabled = busy || !connected || !['ready', 'preparing'].includes(state.status);
  $('open').title=!connected?'Connect to the console first.':state.status!=='ready'?'Start a filesystem export first.':!manager?'Choose a file manager first.':needsPath?'Enter the file manager executable path.':'Open in file manager';
  $('executable').parentElement.hidden=!manager||['system','explorer','finder'].includes(manager);
  $('executable').required=manager==='custom';$('executable').parentElement.querySelector('.muted').textContent=manager==='custom'?'(required)':'(optional)';
  $('detect').disabled=busy;$('refresh').disabled=busy||!connected;
  $('ftp-url').hidden = state.status !== 'ready'; $('ftp-url').textContent = state.url || ''; $('ftp-url').href = state.url || '#';
  $('mounts').textContent = (state.mounts || []).map(name => '/' + name).join(' · ');
  $('storage-start').disabled = !connected || busy || ['preparing','stopping'].includes(state.status);
  const storageReady = connected && state.status === 'ready' && state.mode === 'storage';
  $('download-folder').disabled = !storageReady || busy;
  $('up').disabled = !storageReady || busy || cwd === '/';
  $('back').disabled = !storageReady || busy || historyIndex <= 0;
  $('forward').disabled = !storageReady || busy || historyIndex >= folderHistory.length - 1;
  $('upload-menu').setAttribute('aria-disabled',String(!storageReady || !folderWritable || busy || uploading));
  $('copy-ftp').hidden = state.status !== 'ready';
  for (const id of ['upload-files','upload-folder','new-folder']) $(id).disabled = !storageReady || !folderWritable || busy || uploading;
  document.querySelectorAll('#files button').forEach(button => {button.disabled = !storageReady || busy || uploading;});
  document.querySelectorAll('#games button').forEach(button => {button.disabled = !connected || busy;});
  document.querySelectorAll('#breadcrumbs button').forEach(button=>{button.disabled=!storageReady||busy||button.hasAttribute('aria-current');});
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
  if (!files.length || !folderWritable) return;
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
      for (let offset = 0; offset < file.size;) {
        if (cancelUpload) throw Error('Upload cancelled');
        const bytes = new Uint8Array(await file.slice(offset, offset + 32768).arrayBuffer());
        const data = btoa(String.fromCharCode(...bytes));
        await api('/api/sd/upload-chunk', {token: uploadToken, offset, data}); offset += bytes.length; transferred += bytes.length;
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
  } finally {uploading = false; $('upload-cancel').disabled = true; await browse(base);}
}
async function browse(target = '/', {record = true, index} = {}) {
  const result = await api('/api/browse', {path: target}); cwd = result.cwd;
  if(record && folderHistory[historyIndex] !== cwd){folderHistory=folderHistory.slice(0,historyIndex+1);folderHistory.push(cwd);historyIndex=folderHistory.length-1;}
  else if(Number.isInteger(index))historyIndex=index;
  renderBreadcrumbs();
  sessionStorage.setItem('storagePath', cwd); $('path').textContent = cwd; $('files').replaceChildren();
  $('download-folder').disabled = false; folderWritable = result.writable === true;
  for (const id of ['upload-files', 'upload-folder', 'new-folder']) $(id).disabled = !folderWritable || uploading;
  $('write-status').textContent = folderWritable ? 'SD card · Upload, rename and delete available. Existing files are never overwritten.' : 'Read-only · Open an SD folder to upload or manage files.';
  for (const entry of result.entries) {
    const target = (cwd === '/' ? '' : cwd) + '/' + entry.name;
    const item = row(entry.name, entry.type === 'dir' ? 'Folder' : entry.size.toLocaleString() + ' bytes',
      entry.type === 'dir' ? 'Open' : '', () => browse(target));
    const button = document.createElement('button'); button.className = 'download-button';
    button.textContent = entry.type === 'dir' ? 'Download ZIP' : 'Download';
    button.onclick = () => action(() => download(target)); item.append(button);
    if (folderWritable) for (const operation of ['rename', 'delete']) {
      const control = document.createElement('button'); control.textContent = operation === 'rename' ? 'Rename' : 'Delete';
      control.onclick = () => action(() => mutateEntry(entry, target, operation)); item.append(control);
    }
    $('files').append(item);
  }
  if (!result.entries.length) $('files').append(row('This folder is empty', '', '', null));
}
function renderBreadcrumbs() {
  const container=$('breadcrumbs');container.replaceChildren();
  const parts=cwd.split('/').filter(Boolean),paths=[{name:'Storage',path:'/'}];
  parts.forEach((name,index)=>paths.push({name,path:'/'+parts.slice(0,index+1).join('/')}));
  for(const [index,part]of paths.entries()){
    if(index){const separator=document.createElement('span');separator.textContent='›';separator.setAttribute('aria-hidden','true');container.append(separator);}
    const button=document.createElement('button');button.textContent=part.name;button.title=part.path;button.disabled=index===paths.length-1;
    if(button.disabled)button.setAttribute('aria-current','page');button.onclick=()=>action(()=>browse(part.path));container.append(button);
  }
}
function acceptList(result) {
  connected = true; games = result.games; connectionHost = result.session.host; connectionId = result.connection?.id || result.session.id || '';
  
  notice('Connected to ' + connectionHost + ' · ' + games.length + ' installed games'); renderGames(); renderStatus();
}
async function detect() {
  notice('Detecting an active FS RPC session…');
  try {
    const result = await api('/api/detect', {});
    state=await api('/api/status');
    if(state.sessionAvailable===false)throw Error(state.error||'Filesystem service is unavailable.');
    acceptList(result);
  }
  catch (error) {connected = false; games = []; folderWritable=false;state={...state,status:'unavailable',error:error.message};renderGames(); renderStatus(); throw error;}
}
$('detect').onclick = () => action(detect);
$('refresh').onclick = () => action(async () => acceptList(await api('/api/list', {})));
$('search').oninput = renderGames;
function switchFilesView(mode) {
  sessionStorage.setItem('filesView',mode);
  for(const value of ['games','storage']){const selected=mode===value,button=$(value+'-tab');$(value+'-view').hidden=!selected;button.classList.toggle('selected',selected);button.setAttribute('aria-selected',String(selected));button.tabIndex=selected?0:-1;}
}
for(const mode of ['games','storage'])$(mode+'-tab').onclick=()=>switchFilesView(mode);
document.querySelector('.tabs').onkeydown=event=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;event.preventDefault();const mode=event.key==='Home'?'games':event.key==='End'?'storage':$('games-view').hidden?'games':'storage';switchFilesView(mode);$(mode+'-tab').focus();};
$('storage-start').onclick = () => action(async () => {state = await api('/api/start', {mode: 'storage'}); renderStatus();});
$('download-folder').onclick = () => action(() => download(cwd));
$('upload-files').onclick = () => {$('upload-menu').open=false;$('file-picker').click();};
$('upload-folder').onclick = () => {$('upload-menu').open=false;$('folder-picker').click();};
for (const id of ['file-picker', 'folder-picker']) $(id).onchange = () => {const files = [...$(id).files]; $(id).value = ''; $('upload-cancel').disabled = false; action(() => uploadFiles(files));};
$('upload-cancel').onclick = () => {cancelUpload = true; $('upload-detail').textContent = 'Cancelling upload…';};
$('new-folder').onclick = () => action(async () => {const name = await fileDialog('New folder', 'Create a folder in ' + cwd, ''); if (!name) return; await api('/api/sd/mkdir', {path: cwd + '/' + name, lease: state.lease}); await browse(cwd);});
$('back').onclick=()=>action(()=>browse(folderHistory[historyIndex-1],{record:false,index:historyIndex-1}));
$('forward').onclick=()=>action(()=>browse(folderHistory[historyIndex+1],{record:false,index:historyIndex+1}));
$('upload-menu').querySelector('summary').onclick=event=>{if($('upload-menu').getAttribute('aria-disabled')==='true')event.preventDefault();};
document.addEventListener('click',event=>{if(!event.target.closest('#upload-menu'))$('upload-menu').open=false;});
document.addEventListener('keydown',event=>{if(event.key==='Escape'&&$('upload-menu').open){$('upload-menu').open=false;$('upload-menu').querySelector('summary').focus();}});
$('copy-ftp').onclick=()=>action(async()=>{try{await navigator.clipboard.writeText(state.url);notice('FTP address copied.');}catch{notice('Copy this FTP address: '+state.url);}});
$('up').onclick = () => action(() => browse(cwd.split('/').slice(0, -1).join('/') || '/'));
$('stop').onclick = () => action(async () => {state = await api('/api/stop', {lease: state.lease}); renderStatus();});
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
  $('manager').onchange=()=>{$('executable').value=paths.get($('manager').value)||'';renderStatus();};
  $('executable').oninput=()=>{paths.set($('manager').value,$('executable').value);renderStatus();};
  await action(detect);
  if(sessionStorage.getItem('filesView')==='storage')$('storage-tab').click();
  renderStatus();
}
let polling = false;
setInterval(async () => {
  if (polling) return; polling = true;
  try {
    state = await api('/api/status');
    if (connected && connectionId && state.sessionId && connectionId !== state.sessionId) {
      connected = false; games = []; folderWritable = false; lastStorageLease = '';
      renderGames(); $('files').replaceChildren();
      notice('The console session changed. Use Detect session to connect again.');
    }
    if (state.sessionAvailable === false) {connected = false; games = []; folderWritable = false; renderGames(); $('files').replaceChildren();}
    renderStatus(); await renderDownloads();
    if (pollError && state.sessionAvailable !== false) {pollError = false; notice(connected ? 'Connected to ' + connectionHost : 'Filesystem service is available. Use Detect session to reconnect.');}
    if (state.mode === 'storage' && state.status === 'ready' && lastStorageLease !== state.lease) {lastStorageLease = state.lease; folderHistory=[];historyIndex=-1;await browse(sessionStorage.getItem('storagePath') || '/').catch(() => browse('/')); }
  } catch (error) {pollError = true; connected = false;state={...state,status:'unavailable',error:error.message};renderStatus(); notice(error.message);} finally {polling = false;}
}, 1000);
init().catch(error => notice(error.message));
