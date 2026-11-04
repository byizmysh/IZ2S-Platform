const fs=require('node:fs/promises'),path=require('node:path');
const catalogTools=require('./app-catalog.cjs');
const {prepare}=require('./gpu-app.cjs');
const {write:atomic}=require('./json-write.cjs');
const check=(v,m)=>{if(!v)throw Error(m);};
async function create({rpc,menu,directory,prepareApp=prepare,prepareMenu=async()=>{},resident=false}){
 const mapped=menu.controls.mapped,cache=new Map(),closings=new WeakMap();let generation=0,refreshSeen=0,menuState=await rpc.read(mapped,1024);
 const builtinHosts=require('../builtin-hosts.cjs').create({context:{rpc,mapped,directory}});
 let catalog,menuData,boundaryFailure=null;const runtime={active:null,report:{session:rpc.a.session,started:new Date().toISOString(),transitions:[],generation:0,resourcesRetained:true}};
 const save=()=>atomic(path.join(directory,'app-runtime.json'),runtime.report);
 // The frame loop pauses rendering before servicing a module's GPU mailbox.
 // No app names or mailbox offsets belong to the common scheduler.
 function unsafe(error){return error?.restartRequired===true||error?.resourcesRetained===true||/restore scratch|scratch restoration|did not join|join failed|worker stop timed out|allocations retained|buffers retained/i.test(error?.message||'');}
 function closeService(active){if(!active)return Promise.resolve();let closing=closings.get(active);if(!closing){closing=Promise.resolve().then(()=>active.service?.close());closings.set(active,closing);}return closing;}
 runtime.pending=async state=>{const active=runtime.active;if(!active)return false;if(boundaryFailure?.owner===active)return true;
  try{return !!await active.service?.pending?.(state);}catch(error){if(unsafe(error))throw error;if(runtime.active!==active)return false;boundaryFailure={owner:active,error};return true;}
 };
 async function audio(app){
  let effects={};
  if(app){
   const folder=path.join(directory,'loaded-apps',app.id+'-'+app.fingerprint.slice(0,16));await fs.mkdir(folder,{recursive:true});
   for(const [id,bytes]of Object.entries(app.effectData)){const out=path.join(folder,id+'.pcm');await fs.writeFile(out,bytes);effects[id]=out;}
  }
  await atomic(path.join(directory,'game-audio-source.json'),{session:rpc.a.session,mapped,package:app?{id:app.id,fingerprint:app.fingerprint,effects}:null});
 }
 async function rescan(state,upload=true){
  const selected=catalog?.apps[state.readUInt32LE(80)]?.id;
  const next=await catalogTools.scan(menu.catalog.root);await prepareMenu();if(!resident)await builtinHosts.sync(next);
  const bytes=await catalogTools.raster(next,directory,'catalog-'+(++generation));
  const font=await require('./font-data.cjs').load();check(font.length===catalogTools.ATLAS_BYTES,'Menu type atlas changed size');
  menuData=Buffer.concat([font,bytes]);if(runtime.decorateMenuData)await runtime.decorateMenuData(menuData);check(menuData.length+1024<=Math.min(menu.controls.bytes,0x7e0000),'Catalog exceeds GPU storage or RUN18 checkpoint region');catalog=next;
  let index=selected?catalog.apps.findIndex(a=>a.id===selected):-1;if(index<0)index=Math.min(state.readUInt32LE(80),Math.max(0,catalog.apps.length-1));
  state.writeUInt32LE(index,80);state.writeFloatLE(index,68);state.writeUInt32LE(catalog.apps.length,112);state.writeUInt32LE(catalog.apps.length,1020);state.writeUInt32LE(0,116);
  state.writeFloatLE(0,96);state.writeFloatLE(0,100);state.writeFloatLE(state.readFloatLE(108)+1,108);state.writeUInt32LE(0,1012);state.writeUInt32LE(0,1016);
  refreshSeen=state.readFloatLE(104);menuState=state;
  if(upload){await require('../packages/progress.cjs').upload(rpc,mapped+1024,menuData,runtime.onUploadProgress);await rpc.write(mapped,menuState);}
  runtime.report.catalog=catalogTools.summary(catalog);runtime.report.generation=generation;await save();
 }
 async function returnToMenu(error){
  await closeService(runtime.active);boundaryFailure=null;
  await audio(null);runtime.active=null;runtime.report.active=null;
  menuState.writeFloatLE(1,960);menuState.writeFloatLE(0,964);menuState.writeUInt32LE(0,784);menuState.writeUInt32LE(error?1:0,92);
  await rescan(menuState);runtime.report.transitions.push({event:'return',error:error?.message,time:new Date().toISOString()});await save();
  return menu.commands[0];
 }
 runtime.afterBatch=async()=>{
  const state=await rpc.read(mapped,1024);
  if(runtime.active){
   if(state.readUInt32LE(1016)!==0)return returnToMenu();
   if(boundaryFailure?.owner===runtime.active)return returnToMenu(boundaryFailure.error);
   let outcome;try{outcome=await runtime.active.service?.afterBatch(state);}catch(error){if(unsafe(error))throw error;return returnToMenu(error);}
   if(outcome?.returnToMenu===true)return returnToMenu(outcome.error);return runtime.active.gpu.commands[0];
  }
  const request=state.readUInt32LE(1012);
  if(request!==0){
   menuState=state;const selected=catalog.apps[request-1];
   try{
    check(selected,'The selected app is no longer in the catalog');
    const app=await catalogTools.readApp(selected.root);check(app.fingerprint===selected.fingerprint,'App changed; tap Refresh before opening it');
    if(app.manifest.displayOnly||app.manifest.builtin){state.writeUInt32LE(0,1012);state.writeFloatLE(0,960);state.writeFloatLE(0,964);await rpc.write(mapped,state);return menu.commands[0];}
    let gpu=cache.get(app.fingerprint);
    if(!gpu){check(cache.size<64,'This session has retained 64 app versions; start a fresh session to load another');gpu=await prepareApp(rpc,menu,app,directory);check(gpu.scratchRestored,'App preparation failed to restore scratch');check(gpu.ready,'App preparation incomplete');cache.set(app.fingerprint,gpu);}
    // Native runtimes can restore this launcher after disconnecting the host.
    // Capture while the common scheduler has joined its graphics worker and
    // before the app replaces the shared menu storage.
    let launcherReturn;
    if(app.manifest.nativeLauncherReturn===true){
     const bytes=await rpc.read(mapped,0x7f0000);check(bytes.readUInt32LE(0x700004)===0,'Native return snapshot is not the launcher');
     launcherReturn={session:rpc.a.session,bytes,slot:request-1,controls:menu.controls,commands:[...menu.commands[0]],catalog:catalog.apps.map(a=>({id:a.id,fingerprint:a.fingerprint}))};
    }
    const bytes=Buffer.concat([app.files.state.bytes,app.files.data.bytes]);
    bytes.writeUInt32LE(0,1012);bytes.writeUInt32LE(0,1016);
    if(app.manifest.transition==='none')bytes.writeFloatLE(0,960);
    if(app.files.memory){check(gpu.privateStorage?.bytes===app.files.memory.allocationBytes,'App private storage differs');await require('./owned-buffer.cjs').initialize(rpc,gpu.privateStorage.mapped,gpu.privateStorage.bytes,app.files.memory.bytes,app.manifest.memory);}
    await require('./owned-buffer.cjs').initialize(rpc,mapped,menu.controls.bytes,bytes);await audio(app);
    let service;
    if(app.manifest.hostRuntime){
      const match=/^module:([a-z0-9-]+)\/([A-Za-z0-9-]+)$/.exec(app.manifest.hostRuntime);
      check(match,'Invalid application host runtime');
      const component=await require('../components.cjs').get(match[1],match[2]);
      service=await component.create({rpc,mapped,directory,app,sdStorage:require('../module-storage.cjs').forModule(match[1]),...(launcherReturn?{launcherReturn}:{})});
    }
    runtime.active={app,gpu,service};
    runtime.report.active={id:app.id,fingerprint:app.fingerprint};runtime.report.transitions.push({event:'launch',id:app.id,fingerprint:app.fingerprint,time:new Date().toISOString()});await save();
    return gpu.commands[0];
   }catch(error){
    // A failed scratch restoration means the native session must not be reused.
    if(unsafe(error))throw error;
    return returnToMenu(error);
   }
  }
  if(state.readFloatLE(104)!==refreshSeen)await rescan(state);
  return menu.commands[0];
 };
 runtime.close=async()=>{try{await closeService(runtime.active);}finally{await builtinHosts.close();}};
 await rescan(menuState);await audio(null);return runtime;
}
module.exports={create};
