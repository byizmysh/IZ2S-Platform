'use strict';
const fs = require('node:fs/promises'), path = require('node:path'), crypto = require('node:crypto');
const {readJson, saveJson} = require('./session/ledger.cjs');
const stopTasks = new Map();
const validId = id => typeof id === 'string' && /^[a-z0-9][a-z0-9-]{0,79}$/.test(id);
function localUrl(value) {
  const url = new URL(value);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' || url.username || url.password)
    throw Error('Module interfaces must use a local HTTP address');
  return url;
}
const folder = root => path.join(root, 'data/state/workspace-tabs');
async function register(root, {id, title, url, moduleId, kind = 'module', automatic = false, requestId, ownerPid, sessionId, controlUrl, panelInstance}) {
  if (!validId(id) || !['files','module','settings'].includes(kind) || typeof title !== 'string' || !title.trim())
    throw Error('Invalid workspace tab');
  if (moduleId && !/^[a-z0-9][a-z0-9-]{0,47}$/.test(moduleId)) throw Error('Invalid module id');
  url = localUrl(url).href;
  if(controlUrl){controlUrl=localUrl(controlUrl).href;if(!/^#[a-f0-9]{48}$/.test(new URL(controlUrl).hash))throw Error('Invalid module control');}
  const file = path.join(folder(root), id + '.json');
  const previous = await readJson(file, {});
  panelInstance ||= process.env.IZ2S_PANEL_INSTANCE || (await readJson(path.join(root,'data/state/window.json'),{})).workspaceInstance;
  if(kind!=='settings')sessionId ||= process.env.IZ2S_ATTEMPT_ID;
  if (automatic && previous.requestId === requestId && previous.url === url &&
      previous.panelInstance === panelInstance && previous.ownerPid === ownerPid && previous.sessionId === sessionId) return previous;
  const entry = {id, title:title.slice(0,100), url, moduleId, kind, state:'running', automatic,
    requestId:requestId || crypto.randomUUID(), ownerPid, sessionId, controlUrl, panelInstance, updatedAt:new Date().toISOString()};
  await saveJson(file, entry);
  return entry;
}
function alive(pid) {
  if(!Number.isInteger(pid)||pid<=1)return false;
  try{process.kill(pid,0);return true;}catch(error){if(error.code==='ESRCH')return false;if(error.code==='EPERM')return true;throw error;}
}
async function visible(root,{active,connected=false,panelInstance=process.env.IZ2S_PANEL_INSTANCE,
  probe=require('./platform/listener.cjs').listening,ownerAlive=alive,
  settingsOpen=(id)=>require('./module-settings.cjs').isOpen(root,id),
  managedModules=require('./host-modules.cjs').status()}={}) {
  const rows=await list(root),current=active?.id;
  const checked=await Promise.all(rows.map(async row=>{
    const owner=ownerAlive(row.ownerPid);
    // Keep an actual retained owner visible; removing a view is not release proof.
    if(row.state==='retained'&&owner)return row;
    if(row.panelInstance&&panelInstance&&row.panelInstance!==panelInstance)return null;
    if(row.kind==='settings'){
      if(!settingsOpen(row.moduleId)||!owner)return null;
      if(row.sessionId&&(!connected||row.sessionId!==current))return null;
    }else if(row.kind==='files'||row.controlUrl||row.sessionId){
      if(!connected||!current||(row.sessionId&&row.sessionId!==current))return null;
      if(!owner)return null;
    }else{
      const managed=managedModules.some(module=>module.id===row.moduleId);
      if(row.state==='running'&&(!owner||!managed))return null;
      if(row.state!=='running'&&(!row.panelInstance||row.panelInstance!==panelInstance))return null;
    }
    if(row.state!=='running')return row;
    const reachable=await probe(row.url);
    return reachable===false?null:reachable===null?{...row,state:'unavailable'}:row;
  }));
  return checked.filter(Boolean);
}
async function list(root) {
  const names = await fs.readdir(folder(root)).catch(error => {if (error.code === 'ENOENT') return []; throw error;});
  const rows = await Promise.all(names.filter(name => name.endsWith('.json') && validId(name.slice(0,-5)))
    .map(name => readJson(path.join(folder(root),name)).catch(() => null)));
  return rows.filter(row => {try{return row && validId(row.id) && typeof row.title==='string' && !!localUrl(row.url);}catch{return false;}}).map(row => {
    if(row.state==='running' && row.kind==='settings' && row.ownerPid===process.pid &&
      !require('./module-settings.cjs').isOpen(root,row.moduleId))return {...row,state:'stopped'};
    if (['running','retained'].includes(row.state) && Number.isInteger(row.ownerPid) && row.ownerPid > 1) {
      try {process.kill(row.ownerPid,0);} catch (error) {if (error.code === 'ESRCH') return {...row,state:row.kind==='files'?'unavailable':'stopped'};}
    }
    return row;
  }).map(row=>row.kind==='files'?{...row,title:'Super File Bros.'}:row).sort((a,b) => String(a.updatedAt||'').localeCompare(String(b.updatedAt||'')));
}
async function stopEntry(root, id) {
  if (!validId(id)) throw Error('Invalid workspace tab');
  const file = path.join(folder(root), id + '.json'), entry = await readJson(file);
  if (!entry) throw Error('Unknown tool');
  if (entry.state === 'stopped') return {ok:true,tab:entry};
  if (entry.kind === 'files') {
    const url = localUrl(entry.url);
    let response;
    try {response=await fetch(url.origin + '/api/shutdown', {method:'POST',
      headers:{origin:url.origin,'x-fs-key':url.hash.slice(1),'content-type':'application/json'}, body:'{}', signal:AbortSignal.timeout(150000)});}
    catch(error){
      if(error.cause?.code!=='ECONNREFUSED')throw error;
      await require('../tools/dolphin-fs-rpc/control.cjs').request('stop',{cancelDownloads:true});
    }
    if(response){const result = await response.json();if (!response.ok || !result.ok) throw Error(result.error || 'Super File Bros. shutdown was not confirmed');}
  } else if(entry.controlUrl){
    await require('./session-workspace.cjs').request(entry.controlUrl,'stop',entry.moduleId);
  } else if (entry.moduleId) {
    if (entry.kind !== 'settings') {
      const host=require('./host-modules.cjs');
      if(!host.status().some(row=>row.id===entry.moduleId)) {
        let alive=false;
        if(Number.isInteger(entry.ownerPid)&&entry.ownerPid>1){try{process.kill(entry.ownerPid,0);alive=true;}catch(error){if(error.code!=='ESRCH')throw error;}}
        if(alive)throw Error('This interface belongs to the console session. Use its Stop / Return to launcher control.');
      } else await host.stop(entry.moduleId);
    }
    try{await require('./module-settings.cjs').stop(root, entry.moduleId);}
    catch(error){
      if(error.resourcesRetained){
        const current=await readJson(file,entry);
        if(current.requestId===entry.requestId)await saveJson(file,{...current,state:'retained',updatedAt:new Date().toISOString()});
      }
      throw error;
    }
  } else throw Error('This interface has no module lifecycle. Use its own Stop control.');
  const current=await readJson(file,entry);
  if(current.url===entry.url && current.ownerPid===entry.ownerPid){
    const tab={...current,state:'stopped',updatedAt:new Date().toISOString()};
    await saveJson(file,tab);return {ok:true,tab};
  }
  return {ok:true,tab:current};
}
function stop(root,id) {
  const key=path.resolve(root)+'\0'+id;
  if(!stopTasks.has(key))stopTasks.set(key,Promise.resolve().then(()=>stopEntry(root,id)).finally(()=>stopTasks.delete(key)));
  return stopTasks.get(key);
}
module.exports = {register, list, visible, stop, localUrl};
