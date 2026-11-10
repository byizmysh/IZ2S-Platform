'use strict';
// PS1 view over the existing Super File Bros. service. It owns no FS engine.
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http');
const crypto=require('node:crypto'),assert=require('node:assert/strict');
const MAX_CHUNK_BYTES=0x70000,LEGACY_CHUNK_BYTES=32768,REQUEST_BYTES=0xa0000;
const ROOT_SD='/sd/IZ2S/Homebrew/Modules/izpsx';
const AREAS=Object.freeze({games:ROOT_SD+'/games',bios:ROOT_SD+'/bios',saves:ROOT_SD+'/saves'});
const read=async p=>JSON.parse(await fs.readFile(p));
const pause=ms=>new Promise(r=>setTimeout(r,ms));
function target(area,name,{directory=false}={}){
 assert(Object.hasOwn(AREAS,area),'Unknown PS1 folder');
 assert(typeof name==='string'&&name&&name.split('/').every(part=>part&&part!=='.'&&part!=='..'&&!/[\x00-\x1f\x7f\\:]/.test(part)),'Invalid file or folder name');
 const value=AREAS[area]+'/'+name;assert(Buffer.byteLength(value)<769,'The file name is too long');
 if(!directory&&area==='games')assert(/\.(cue|bin|img|pbp|sbi|sub|ccd)$/i.test(name),'Choose CUE, BIN, IMG or PBP game files');
 return value;
}
function directory(area,relative='') {assert(Object.hasOwn(AREAS,area),'Unknown PS1 folder');assert(typeof relative==='string');return relative?target(area,relative,{directory:true}):AREAS[area];}
async function guardFor(root){
 const window=await read(path.join(root,'data/state/window.json')),auth=await read(path.join(root,'data/state/panel-auth.json'));
 const origin=new URL(window.url).origin;
 assert(new URL(origin).hostname==='127.0.0.1');
 const response=await fetch(origin+'/api/state',{headers:{'x-iz-key':auth.token,Origin:origin},signal:AbortSignal.timeout(10000)});
 assert(response.ok,'Unable to check the launcher session');const s=await response.json();
 if(s.active?.status!=='running'||!s.fsRpc?.verified)return {ready:false,reason:'Open IZlauncher on the console.'};
 const directory=s.active.runDirectory;
 assert(directory===path.join(root,'data/sessions',s.fsRpc.session));
 const runtime=await read(path.join(directory,'app-runtime.json'));
 if(runtime.unsafe||runtime.restartRequired||runtime.boundaryFailure)return {ready:false,reason:'Console cleanup is not confirmed. Return to the launcher.'};
 if(runtime.active){
  if(Number.isSafeInteger(runtime.active.ownerPid)&&runtime.active.ownerPid>0)try{process.kill(runtime.active.ownerPid,0);}catch(e){if(e.code==='ESRCH')return {ready:false,reason:'The previous console session stopped. Restart the console application and reconnect IZ Panel.'};}
  return {ready:false,session:s.fsRpc.session,reason:'Press Minus to open the IZPSX menu and choose Exit IZPSX emulator.'};
 }
 try{await fs.access(path.join(directory,'native-owner.json'));return {ready:false,reason:'Game resources are still in use. Wait for the game to close normally.'};}
 catch(e){if(e.code!=='ENOENT')throw e;}
 return {ready:true,session:s.fsRpc.session,directory};
}
async function create({root,services,backend,guard=()=>guardFor(root),uiHome=path.join(__dirname,'ui'),dataDirectory}){
 const sharedBackend=!backend;backend||=require(path.join(root,'tools/dolphin-fs-rpc/control.cjs'));
 const checkpoint=dataDirectory&&path.join(dataDirectory,'uploads.json');
 const token=crypto.randomBytes(32).toString('hex'),uploads=new Map(),stopAttempts=new Set();
 let origin,serial=Promise.resolve(),lease=null,leaseSession=null,ownedLease=null,ownedSession=null,closing=false,closeTask,activeTransfer=null,saveTask=Promise.resolve(),cached={};
 function saveUploads(){if(!checkpoint)return Promise.resolve();const snapshot=JSON.stringify([...uploads]);return saveTask=saveTask.catch(()=>{}).then(async()=>{await fs.mkdir(dataDirectory,{recursive:true});await fs.writeFile(checkpoint+'.tmp',snapshot,{mode:0o600});await fs.rename(checkpoint+'.tmp',checkpoint);});}
 if(checkpoint){try{for(const [t,r]of JSON.parse(await fs.readFile(checkpoint,'utf8')))uploads.set(t,{...r,closeAttempted:false,lastActivity:0,error:null});}catch(e){if(e.code!=='ENOENT')throw e;}}
 const reply=(res,code,data)=>{res.writeHead(code,{'content-type':'application/json; charset=utf-8'});res.end(JSON.stringify(data));};
 async function admission(session){const value=await guard();assert(value.ready,value.reason||'Console busy');
  assert(typeof value.session==='string'&&value.session,'The console session is unavailable');
  if(session)assert(value.session===session,'The console session has changed');return value;}
 function matching(state,expectedLease,session){
  assert(typeof expectedLease==='string'&&expectedLease&&state.lease===expectedLease&&state.session===session,'The file session has changed');
 }
 async function storage({recover=false}={}){
  const consoleState=await admission();let state=sharedBackend?await require(path.join(root,'tools/dolphin-fs-rpc/refresh.cjs')).ensure({root,backend,admit:()=>admission(consoleState.session)}):await backend.request('status');
  if(stopAttempts.has(state.lease))assert(state.status==='closed','The file session exit has not been confirmed');
  if(state.status!=='ready'||state.mode!=='storage'||state.session!==consoleState.session){
   const reconnect=['failed','closed','idle'].includes(state.status);
   assert(recover||reconnect||!uploads.size,'Wait for the upload to finish');
   assert((reconnect||!state.uploads)&&!state.downloads,'SD storage is still transferring files. Wait for completion.');
   const shared=['ready','preparing'].includes(state.status);
   // Join an in-progress storage export. A game FTP export is switched to SD
   // by the same broker, which serializes and checks outstanding transfers.
   if(state.status==='stopping'||state.status==='preparing'&&state.mode!=='storage'){
    const until=Date.now()+25000;
    while(['preparing','stopping'].includes(state.status)&&Date.now()<until){await pause(100);await admission(consoleState.session);state=await backend.request('status');}
    assert(['ready','idle','closed'].includes(state.status),state.error||'SD storage is still connecting. Wait for completion.');
   }
   if(state.mode!=='storage'||!['ready','preparing'].includes(state.status)){
    assert(['idle','closed','ready','failed'].includes(state.status),state.error||'The file service is still finishing its previous operation. Wait and reconnect.');
    await admission(consoleState.session);
    const started=await (typeof backend.requestRaw==='function'?backend.requestRaw('start-storage',{},300000):backend.request('start-storage'));
    assert(typeof started.lease==='string'&&started.lease,'The file session has not confirmed ownership');
    matching(started,started.lease,consoleState.session);assert(started.mode==='storage','A different file mode is open');
    if(!shared){ownedLease=started.lease;ownedSession=consoleState.session;}
    state=started;
   }
   const expectedLease=state.lease;matching(state,expectedLease,consoleState.session);
   const until=Date.now()+25000;
   while(state.status!=='ready'&&Date.now()<until){
    await pause(100);await admission(consoleState.session);state=await backend.request('status');
    matching(state,expectedLease,consoleState.session);assert(state.mode==='storage','A different file mode is open');
    if(state.status==='failed')throw Error(state.error||'Unable to open the SD card');
   }
   assert(state.status==='ready','The SD connection timed out. Check its status before trying again.');
  }
  matching(state,state.lease,consoleState.session);await admission(consoleState.session);lease=state.lease;leaseSession=state.session;return state;
 }
 async function sameUpload(item){await admission(item.session);const state=await backend.request('status');
  matching(state,item.lease,item.session);assert(state.status==='ready'&&state.mode==='storage','This previous upload cannot be resumed.');
  await admission(item.session);return state;}
 async function stopLease(expectedLease,expectedSession){
  await admission(expectedSession);const before=await backend.request('status');matching(before,expectedLease,expectedSession);
  assert(!before.uploads&&!before.downloads,'SD storage is still transferring files');
  assert(before.status==='ready'||before.status==='closed','The file session cannot be closed yet');
  if(before.status==='closed')return;
  assert(!stopAttempts.has(expectedLease),'This file session has already been asked to close. Do not repeat the request.');
  await admission(expectedSession);
  stopAttempts.add(expectedLease);
  const stopped=await backend.request('stop',{lease:expectedLease});
  assert(!stopped.skipped&&stopped.status==='closed'&&stopped.lease===expectedLease&&stopped.session===expectedSession,'The file session has not confirmed that it closed');
  const after=await backend.request('status');assert(after.status==='closed'&&after.lease===expectedLease&&after.session===expectedSession&&!after.uploads&&!after.downloads,'The file session is still busy');
 }
 async function cancelUpload(token,item){
  assert(!item.closeAttempted,'The upload exit has not been confirmed. Do not repeat cleanup.');
  const current=await admission(),state=await backend.request('status');
  if(state.status!=='ready'||state.lease!==item.lease||current.session!==item.session){const next=await storage({recover:true});await backend.request('sd-upload-resume',{token,path:target(item.area,item.name),size:item.bytes,fingerprint:item.fingerprint,lease:next.lease});item.lease=next.lease;item.session=next.session;}
  await sameUpload(item);item.closeAttempted=true;
  const result=await backend.request('sd-upload-cancel',{token});assert(result.ok===true,'Upload cancellation has not been confirmed');
  uploads.delete(token);await saveUploads();return result;
 }
 async function files(area,relative=''){
  assert(Object.hasOwn(AREAS,area));const cwd=directory(area,relative);await storage();
  const result=await backend.request('browse',{path:cwd});assert(result.cwd===cwd&&Array.isArray(result.entries));
  const data={area,relative,path:cwd,entries:result.entries.filter(e=>['file','dir'].includes(e.type)&&!e.name.startsWith('.ns2-upload-')).map(e=>({name:e.name,size:e.size,type:e.type})),writable:result.writable===true&&area!=='saves'};
  cached[area]=data;return data;
 }
 async function perform(route,data){
  if(route==='/api/connect'){await storage();return files(data.area||'games',data.relative||'');}
  if(route==='/api/files')return files(data.area,data.relative||'');
  if(route==='/api/disconnect'){
   assert(!uploads.size,'Wait for the upload to finish');
   if(ownedLease){await stopLease(ownedLease,ownedSession);ownedLease=null;ownedSession=null;lease=null;leaseSession=null;return {ok:true};}
   lease=null;leaseSession=null;return {ok:true,shared:true};
  }
  if(route==='/api/upload/begin'){
   assert(!uploads.size,'Wait for the current upload to finish');assert(data.area!=='saves','Save cards are read-only in this manager');const dest=target(data.area,data.name);if(data.area==='bios')assert(data.name==='scph101.bin'&&data.size===524288,'Upload your 512 KB BIOS as scph101.bin');
   assert(Number.isSafeInteger(data.size)&&data.size>0,'Invalid file size');const state=await storage();
   await admission(state.session);
   const parent=path.posix.dirname(dest);await backend.request('sd-mkdir',{path:parent,parents:true,lease});await admission(state.session);
   const opened=await backend.request('sd-upload-begin',{path:dest,size:data.size,lease,fingerprint:data.fingerprint});
   assert(typeof opened.token==='string'&&opened.token);const chunkBytes=Number.isInteger(opened.chunkBytes)&&opened.chunkBytes>0&&opened.chunkBytes<=MAX_CHUNK_BYTES?opened.chunkBytes:LEGACY_CHUNK_BYTES;opened.chunkBytes=chunkBytes;uploads.set(opened.token,{chunkBytes,area:data.area,lease,session:state.session,bytes:data.size,offset:0,name:data.name,fingerprint:data.fingerprint||null,closeAttempted:false,lastActivity:Date.now()});await saveUploads();return opened;
  }
  if(route.startsWith('/api/upload/')){
   const item=uploads.get(data.token);assert(item,'The upload is closed or belongs to another window');
   if(route==='/api/upload/resume'){
    assert(data.name===item.name&&data.size===item.bytes&&data.fingerprint===item.fingerprint,'Choose the same file to continue');const state=await storage({recover:true});const resumeData={token:data.token,path:target(item.area,item.name),size:item.bytes,fingerprint:item.fingerprint,lease:state.lease};const result=await (typeof backend.requestRaw==='function'?backend.requestRaw('sd-upload-resume',resumeData,900000):backend.request('sd-upload-resume',resumeData));
    assert(Number.isSafeInteger(result.bytes)&&result.bytes>=item.offset&&result.bytes<=item.bytes,'The upload checkpoint changed');item.offset=result.bytes;item.chunkBytes=result.chunkBytes;item.lease=state.lease;item.session=state.session;item.closeAttempted=false;await saveUploads();return {...result,name:item.name,area:item.area};
   }
   assert(!item.closeAttempted,'The upload exit has not been confirmed. Do not repeat cleanup.');
   if(route==='/api/upload/pause'){await sameUpload(item);return backend.request('sd-upload-pause',{token:data.token});}
   if(route==='/api/upload/chunk'){
    assert(Number.isSafeInteger(data.offset)&&data.offset===item.offset,'Upload chunks arrived out of order');
    assert(typeof data.data==='string'&&data.data.length<=Math.ceil(item.chunkBytes/3)*4&&/^[A-Za-z0-9+/]*={0,2}$/.test(data.data));
    const bytes=Buffer.from(data.data,'base64');assert(bytes.length>0&&bytes.length<=item.chunkBytes&&bytes.toString('base64')===data.data);
    assert(item.offset+bytes.length<=item.bytes,'The upload chunk exceeds the image size');
    await sameUpload(item);
    const result=await backend.request('sd-upload-chunk',{token:data.token,offset:data.offset,data:data.data});
    assert(result.bytes===item.offset+bytes.length,'The upload chunk write has not been confirmed');item.offset=result.bytes;await saveUploads();return result;
   }
   if(route==='/api/upload/finish'){
    assert(item.offset===item.bytes,'The file has not finished uploading');await sameUpload(item);item.closeAttempted=true;
    const result=await backend.request('sd-upload-finish',{token:data.token});
    assert(result.ok===true&&result.bytes===item.bytes&&result.path===target(item.area,item.name),'Upload completion has not been confirmed');uploads.delete(data.token);await saveUploads();return result;
   }
   if(route==='/api/upload/cancel')return cancelUpload(data.token,item);
  }
  if(route==='/api/mkdir'){
   assert(!uploads.size,'Wait for the upload to finish');assert(data.area==='games','Create folders inside Games');const dest=target('games',data.name,{directory:true});const state=await storage();await admission(state.session);return backend.request('sd-mkdir',{path:dest,parents:true,lease});
  }
  if(route==='/api/delete'){
   assert(!uploads.size,'Wait for the upload to finish');assert(data.area==='games','Only game files can be removed here');assert(data.confirmed===data.name,'Confirm the selected file or folder first');
   const relative=data.relative||'',list=await files('games',relative),selected=list.entries.find(e=>e.name===data.name);
   assert(selected&&selected.type===data.type&&(selected.type==='dir'||selected.size===data.size),'The selected entry changed. Refresh before deleting it');
   const name=relative?relative+'/'+data.name:data.name,dest=target('games',name,{directory:true});const state=await backend.request('status'),now=await admission();matching(state,lease,now.session);await admission(now.session);
   const result=await backend.request('sd-delete',{path:dest,lease,confirmed:true});assert(result.ok===true,'Removal was not confirmed');return {ok:true,path:dest};
  }
  throw Error('Unknown action');
 }
 async function run(route,data){
  const item=uploads.get(data.token),transfer=route.startsWith('/api/upload/')&&item;
  if(transfer){activeTransfer={token:data.token,phase:route==='/api/upload/resume'?'verifying':'uploading'};item.error=null;}
  try{const result=await perform(route,data);if(item)item.lastActivity=route==='/api/upload/pause'||route==='/api/upload/cancel'?0:Date.now();return result;}
  catch(error){if(item){item.lastActivity=0;item.error=error.message;}throw error;}
  finally{if(transfer)activeTransfer=null;}
 }
 const server=http.createServer(async(req,res)=>{
  res.setHeader('cache-control','no-store');res.setHeader('x-content-type-options','nosniff');res.setHeader('referrer-policy','no-referrer');
  try{
   assert(req.headers.host===new URL(origin).host,'Open the local module address');const url=new URL(req.url,origin);
   const assets={'/':['index.html','text/html'],'/app.js':['app.js','text/javascript'],'/style.css':['style.css','text/css'],'/logo.svg':['logo.svg','image/svg+xml'],'/import-plan.js':['import-plan.js','text/javascript']};
   if(req.method==='GET'&&assets[url.pathname]){const [name,type]=assets[url.pathname];res.writeHead(200,{'content-type':type+'; charset=utf-8'});return res.end(await fs.readFile(path.join(uiHome,name)));}
   assert(req.headers['x-izpsx-key']===token,'Open this page from IZ2S Platform');
   if(req.method==='GET'&&url.pathname==='/api/upload/resume-progress'){
    const state=await (backend.requestRaw?backend.requestRaw('status',{},2000):backend.request('status')),progress=state.resume;
    return reply(res,200,progress&&uploads.has(progress.token)?{phase:progress.phase,bytes:progress.bytes,total:progress.total}:{phase:'waiting'});
   }
   if(req.method==='GET'&&url.pathname==='/api/status'){
    const state=await guard(),pending=[...uploads].map(([token,item])=>{
     const active=activeTransfer?.token===token||Date.now()-(item.lastActivity||0)<10000;
     return {token,name:item.name,area:item.area,bytes:item.bytes,offset:item.offset,active,phase:activeTransfer?.token===token?activeTransfer.phase:active?'uploading':'paused',
      cancelAllowed:!item.closeAttempted,resumeAllowed:!active&&!item.closeAttempted&&!!item.fingerprint,fingerprint:item.fingerprint,
      reason:active?'Upload in progress.':item.error?'Upload paused: '+item.error:item.closeAttempted?'The transfer exit has not been confirmed. Reconnect SD storage.':item.fingerprint?'Upload paused. Choose the same file to continue from the verified checkpoint.':'This transfer was created by an older version. Cancel it and upload the file again.'};
    });
    return reply(res,200,{...state,session:state.session||null,attached:state.ready&&!!lease&&leaseSession===state.session,uploading:uploads.size>0,pendingUpload:pending[0]||null,service:'Console SD storage',cached:leaseSession===state.session?cached:{}});
   }
   assert(req.method==='POST'&&req.headers.origin===origin,'Invalid request');
   let body='',size=0;for await(const part of req){size+=part.length;assert(size<=REQUEST_BYTES,'The upload chunk is too large');body+=part;}
   const data=JSON.parse(body||'{}');assert(!closing,'The module window is closing');
   if(url.pathname==='/api/upload/resume'){const item=uploads.get(data.token);assert(!item||!(activeTransfer?.token===data.token||Date.now()-(item.lastActivity||0)<10000),'This upload is already in progress. Its current progress is shown below.');}
   const result=serial.then(()=>{assert(!closing,'The module window is closing');return run(url.pathname,data);});serial=result.catch(()=>{});reply(res,200,await result);
  }catch(e){reply(res,400,{error:e.message});}
 });
 const listening=await services.listenHttp(server);origin=listening.origin;
 return {server,url:origin+'/#'+token,close(){
  if(closeTask)return closeTask;
  closing=true;
  // Closing this PC view is independent of console connectivity and transfers.
  // The shared broker owns file handles and durable upload checkpoints. Only
  // Disconnect SD may explicitly release its lease; closing a view must not.
  closeTask=(async()=>{
   const stopped=new Promise((resolve,reject)=>server.close(error=>error&&error.code!=='ERR_SERVER_NOT_RUNNING'?reject(error):resolve()));
   server.closeAllConnections?.();
   await stopped;
   await saveUploads();
  })();
  return closeTask;
 }};
}
async function open(context){
 const handle=await create(context);
 try{await context.services.openPanel(handle.url,{title:'IZPSX emulator · games and BIOS'});return handle;}
 catch(error){await handle.close();throw error;}
}
module.exports={open,create,target,directory,AREAS,guardFor};
