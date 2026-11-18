const fs=require('node:fs/promises'),path=require('node:path');
const catalogTools=require('./app-catalog.cjs');
const {prepare}=require('./gpu-app.cjs');
const {write:atomic}=require('./json-write.cjs');
const applicationChannels=require('../console/sd-launcher/application-channel.cjs');
const check=(v,m)=>{if(!v)throw Error(m);};
async function create({rpc,menu,directory,prepareApp=prepare,prepareMenu=async()=>{},resident=false,deferMenuUpload=false,rasterMenu=catalogTools.raster,scanMenu=catalogTools.scan,isolateControls=false,controlProtectedRanges=[],openApplicationChannel}){
 const mapped=menu.controls.mapped,cache=new Map(),closings=new WeakMap(),nativeReturns=new WeakMap(),retainedControls=new Map(),localClosures=new Set();let generation=0,refreshSeen=0,menuState=await rpc.read(mapped,1024),menuObserver=rpc,returningMenu;
 const builtinHosts=require('../builtin-hosts.cjs').create({context:{rpc,mapped,directory}});
 let catalog,menuData,baseMenuData,baseMenuKey,boundaryFailure=null,external=null,execution=0;const runtime={active:null,preparing:null,report:{session:rpc.a.session,started:new Date().toISOString(),transitions:[],generation:0,resourcesRetained:true}};
 const currentCommands=()=>runtime.active?.gpu?.commands?.[0]||menu.commands[0];
 runtime.currentControls=()=>runtime.active?.controls||menu.controls;
 runtime.catalogEntries=()=>catalog?.apps||[];
 runtime.observer=()=>menuObserver;
 runtime.applicationChannelsEnabled=()=>typeof openApplicationChannel==='function';
 runtime.readState=()=>((runtime.active?.channel?runtime.active.rpcScope.rpc:menuObserver).read(runtime.currentControls().mapped,1024));
 runtime.setApplicationChannelFactory=factory=>{check(!runtime.active&&!runtime.preparing&&typeof factory==='function','Cannot replace the application channel while an app is running');openApplicationChannel=factory;};
 runtime.menuRasterizer=rasterMenu;
 const save=()=>atomic(path.join(directory,'app-runtime.json'),runtime.report);
 // The frame loop pauses rendering before servicing a module's GPU mailbox.
 // No app names or mailbox offsets belong to the common scheduler.
 function unsafe(error){const policy=require('../session/offline-policy.cjs');return policy.requiresRetention(error)||policy.isTransportLoss(error)||/restore scratch|scratch restoration|did not join|join failed|worker stop timed out|allocations retained|buffers retained/i.test(error?.message||'');}
 function closeService(active){if(!active)return Promise.resolve();let closing=closings.get(active);if(!closing){const stop=async()=>{const service=active.serviceStarting?await active.serviceStarting:active.service;return service?.close();};closing=active.rpcScope?active.rpcScope.close(stop):Promise.resolve().then(stop);closings.set(active,closing);}return closing;}
 function lateOutcome(owner,error){if(error){const rows=runtime.report.lateAppOutcomes??=[];rows.push({id:owner.app?.id,epoch:owner.channel?.epoch,execution:owner.controls?.execution,error:error.message,resourcesRetained:!!error.resourcesRetained});if(rows.length>64)rows.shift();}return currentCommands();}
 function nativeReturn(ended,proof,error,{backgroundCleanup=false}={}){
  check(applicationChannels.isReturned(ended.channel,proof),'Native application return is not attested');
  if(nativeReturns.has(ended))return nativeReturns.get(ended);
  const returning=(async()=>{
  check(!runtime.active||runtime.active===ended,'Another application owns the launcher');
  ended.rpcScope.revoke();if(runtime.active===ended)runtime.active=null;if(runtime.preparing===ended)runtime.preparing=null;delete runtime.report.preparing;boundaryFailure=null;runtime.report.active=null;
  runtime.report.lastNativeReturn={epoch:ended.channel.epoch,graphicsIdle:proof.graphicsIdle,nativeCallsDrained:proof.nativeCallsDrained,resourcesReleased:false};
  if(backgroundCleanup){
   const row={epoch:ended.channel.epoch,id:ended.app?.id,state:'pending',nativeOwnershipReleased:false};(runtime.report.localCleanup??=[]).push(row);
   const closing=closeService(ended).then(receipt=>{row.state='closed';row.rpc=receipt;},failure=>{row.state='retained';row.error=failure.message;}).then(()=>save());
   localClosures.add(closing);closing.finally(()=>localClosures.delete(closing)).catch(()=>{});
  }
  await audio(null);if(error)runtime.report.lastLaunchError={message:error.message,code:error.code,at:new Date().toISOString()};
  runtime.report.transitions.push({event:'return',epoch:ended.channel.epoch,error:error?.message,native:true,time:new Date().toISOString()});await save();return menu.commands[0];
  })();nativeReturns.set(ended,returning);returningMenu=returning;returning.finally(()=>{if(returningMenu===returning)returningMenu=null;}).catch(()=>{});return returning;
 }
 // Public status is only a cheap trigger. The channel then verifies the actual
 // private native owner before releasing the PC panel's application lock.
 runtime.observeNativeReturn=async state=>{
  const active=runtime.active||runtime.preparing;if(!active?.channel||state?.host_rpc_returned!==active.channel.epoch)return false;
  const proof=await active.channel.synchronize();if(!proof.returned)return false;
  await nativeReturn(active,proof,null,{backgroundCleanup:true});return true;
 };
 // Local revocation only. The native supervisor/transport keeps its original
 // owner until independently proven idle; this method never releases graphics.
 runtime.stopRequests=reason=>{const state=(runtime.active||runtime.preparing)?.rpcScope?.revoke(reason);if(state)runtime.report.lastAppRPCRevocation=state;return state;};
 runtime.pending=async state=>{const active=runtime.active;if(!active)return false;if(boundaryFailure?.owner===active)return true;
  try{active.rpcScope?.assertUsable();const pending=await active.service?.pending?.(state);return runtime.active===active&&!!pending;}catch(error){if(runtime.active!==active){lateOutcome(active,error);return false;}if(unsafe(error)){active.rpcScope?.revoke(error);throw error;}boundaryFailure={owner:active,error};return true;}
 };
 async function audio(app,controlMapped=mapped){
  let effects={};
  if(app){
   const folder=path.join(directory,'loaded-apps',app.id+'-'+app.fingerprint.slice(0,16));await fs.mkdir(folder,{recursive:true});
   for(const [id,bytes]of Object.entries(app.effectData)){const out=path.join(folder,id+'.pcm');await fs.writeFile(out,bytes);effects[id]=out;}
  }
  await atomic(path.join(directory,'game-audio-source.json'),{session:rpc.a.session,mapped:controlMapped,package:app?{id:app.id,fingerprint:app.fingerprint,effects}:null});
 }
 async function rescan(state,upload=true,forceRaster=false,onlyIfChanged=false){
  const selected=catalog?.apps[state.readUInt32LE(80)]?.id;
  const next=await scanMenu(menu.catalog.root);
  if(onlyIfChanged&&catalog&&next.apps.length===catalog.apps.length&&next.apps.every((app,index)=>app.root===catalog.apps[index].root&&app.fingerprint===catalog.apps[index].fingerprint)){
   if(JSON.stringify(next.rejected)!==JSON.stringify(catalog.rejected)){catalog.rejected=next.rejected;runtime.report.catalog=catalogTools.summary(catalog);await save();}
   return;
  }
  await prepareMenu();if(!resident)await builtinHosts.sync(next);
  generation++;
  const language=await require('../../language/node.cjs').forConsole();
  const key=JSON.stringify({apps:next.apps.map(a=>[a.root,a.fingerprint]),language:language.id,messages:language.messages});
  if(forceRaster||key!==baseMenuKey){
   const bytes=await rasterMenu(next,directory,'catalog-'+generation),font=await require('./font-data.cjs').load();check(font.length===catalogTools.ATLAS_BYTES,'Menu type atlas changed size');
   baseMenuData=Buffer.concat([font,bytes]);baseMenuKey=key;
  }
  check(baseMenuData.length+1024<=Math.min(menu.controls.bytes,0x7e0000),'Catalog exceeds GPU storage or RUN18 checkpoint region');catalog=next;
  let index=selected?catalog.apps.findIndex(a=>a.id===selected):-1;if(index<0)index=Math.min(state.readUInt32LE(80),Math.max(0,catalog.apps.length-1));
  state.writeUInt32LE(index,80);state.writeFloatLE(index,68);state.writeUInt32LE(catalog.apps.length,112);state.writeUInt32LE(catalog.apps.length,1020);state.writeUInt32LE(0,116);
  state.writeFloatLE(0,96);state.writeFloatLE(0,100);state.writeFloatLE(state.readFloatLE(108)+1,108);state.writeUInt32LE(0,1012);state.writeUInt32LE(0,1016);
  refreshSeen=state.readFloatLE(104);menuState=state;
  runtime.report.catalog=catalogTools.summary(catalog);runtime.report.generation=generation;runtime.menuDataPending=true;
  if(upload)await runtime.publishMenuData();
  await save();
 }
 runtime.refreshMenu=async()=>{
  check(!runtime.active&&!runtime.preparing,'Return to the menu before refreshing it');
  await rescan(await runtime.readState(),!deferMenuUpload,false,true);
 };
 runtime.publishMenuData=async()=>{
  menuData=Buffer.from(baseMenuData);if(runtime.decorateMenuData)await runtime.decorateMenuData(menuData,{catalog,generation});
  runtime.report.menuUpload=await require('./owned-upload.cjs').upload(rpc,mapped+1024,menuData,runtime.onUploadProgress);await rpc.write(mapped,menuState);runtime.menuDataPending=false;
 };
 async function returnToMenu(error,staged){
  if(unsafe(error)){runtime.stopRequests(error);throw error;}
  const ended=staged||runtime.active||runtime.preparing;
  if(runtime.preparing===ended){runtime.preparing=null;delete runtime.report.preparing;}
  if(ended?.channel&&!runtime.active){runtime.active=ended;runtime.report.active={id:ended.app?.id,epoch:ended.channel.epoch,state:'closing'};}
  const receipt=await closeService(ended);if(ended?.rpcScope)runtime.report.lastAppRPCClosure=receipt;boundaryFailure=null;
  if(ended?.channel){const proof=await ended.channel.close({errorCode:error?0xe980:0});return nativeReturn(ended,proof,error);}
  await audio(null);runtime.active=null;runtime.report.active=null;
  menuState.writeFloatLE(1,960);menuState.writeFloatLE(0,964);menuState.writeUInt32LE(0,784);menuState.writeUInt32LE(error?1:0,92);
  await rescan(menuState);if(error)runtime.report.lastLaunchError={message:error.message,at:new Date().toISOString()};runtime.report.transitions.push({event:'return',error:error?.message,time:new Date().toISOString()});await save();
  return menu.commands[0];
 }
 runtime.queueExternal=app=>{check(!runtime.active&&!runtime.preparing&&!external&&app?.source==='sd'&&typeof app.root==='string','Another application owns the launcher');external=app;};
 runtime.rejectLaunch=async error=>{if(unsafe(error)){runtime.stopRequests(error);throw error;}external=null;return returnToMenu(error);};
 runtime.afterBatch=async()=>{
  if(returningMenu)await returningMenu;
  check(!runtime.preparing,'Application preparation is already pending');
  const owner=runtime.active;let state;try{state=await runtime.readState();}catch(error){if(owner&&runtime.active!==owner)return lateOutcome(owner,error);throw error;}
  if(runtime.active!==owner)return currentCommands();
  if(owner){
   if(state.readUInt32LE(1016)!==0)return returnToMenu();
   if(boundaryFailure?.owner===runtime.active)return returnToMenu(boundaryFailure.error);
   let outcome;try{owner.rpcScope?.assertUsable();outcome=await owner.service?.afterBatch(state);}catch(error){if(runtime.active!==owner)return lateOutcome(owner,error);if(unsafe(error)){owner.rpcScope?.revoke(error);throw error;}return returnToMenu(error);}
   if(runtime.active!==owner)return lateOutcome(owner,outcome?.error);
   if(outcome?.returnToMenu===true)return returnToMenu(outcome.error);return owner.gpu.commands[0];
  }
  const request=state.readUInt32LE(1012);
  if(request!==0||external){
   menuState=state;const candidate=external;external=null;const selected=candidate||catalog.apps[request-1];
   if(selected?.previewOnly){
    state.fill(0,960,968);state.writeUInt32LE(0,1012);
    await rpc.write(mapped+960,state.subarray(960,968));
    await rpc.write(mapped+1012,state.subarray(1012,1016));
    return menu.commands[0];
   }
   let launchScope,staged,channelRequested=false;try{
    check(selected,'The selected app is no longer in the catalog');
    if(openApplicationChannel){
     channelRequested=true;
     const channel=await openApplicationChannel({id:selected.id,execution:execution+1});check(applicationChannels.isChannel(channel),'The framework requires a verified application channel');
     menuObserver=channel.observer;launchScope=require('./app-rpc-scope.cjs').create(channel.rpc,{id:selected.id,execution:++execution});staged={channel,rpcScope:launchScope};runtime.preparing=staged;runtime.report.preparing={id:selected.id,epoch:channel.epoch};delete runtime.report.channelPreparationFailed;await save();
    }
    const appRPC=launchScope?.rpc||rpc,channel=staged?.channel;
    const app=await catalogTools.readApp(selected.root);check(app.fingerprint===selected.fingerprint,'App changed; tap Refresh before opening it');
    launchScope?.assertUsable();
    if(staged)staged.app=app;
    check(!candidate||!app.manifest.builtin&&!app.manifest.displayOnly&&!app.manifest.shellContribution,'This SD package is a menu extension and has no standalone application entry');
    check(!candidate||app.manifest.hostRuntime||!app.manifest.native&&!app.manifest.localRuntime,'This SD package has no compatible console or PC executor');
    if(app.manifest.displayOnly||app.manifest.builtin){if(channel)return returnToMenu(null,staged);state.writeUInt32LE(0,1012);state.writeFloatLE(0,960);state.writeFloatLE(0,964);await rpc.write(mapped,state);return menu.commands[0];}
    await require('./app-requirements.cjs').preflight({manifest:app.manifest,directory,filesystem:rpc.a.filesystem});
    if(appRPC.validateApp)await appRPC.validateApp(app);
    launchScope?.assertUsable();
    const cacheKey=channel?'epoch:'+channel.epoch:app.fingerprint;let gpu=cache.get(cacheKey);
    if(!gpu){check(cache.size<64,'This session has retained 64 app allocations; start a fresh session to load another');gpu=await prepareApp(appRPC,menu,app,directory,{isolateControls:isolateControls||!!channel,protectedRanges:channel?[...controlProtectedRanges,...channel.protectedRanges,...retainedControls.values()]:controlProtectedRanges});launchScope?.assertUsable();check(gpu.scratchRestored,'App preparation failed to restore scratch');check(gpu.ready,'App preparation incomplete');cache.set(cacheKey,gpu);}
    if(isolateControls||channel)check(gpu.controlStorage?.ready&&gpu.controlStorage.format==='iz2s.isolated-controls/1'&&gpu.controlStorage.mapped!==mapped,'App control storage is not isolated');
    const controls={...(gpu.controlStorage||menu.controls),execution:channel?execution:++execution};
    check(Number.isSafeInteger(controls.mapped)&&controls.mapped>=4096&&controls.bytes===menu.controls.bytes,'Invalid app control storage');
    if(channel){check(![...retainedControls.values()].some(r=>controls.mapped<r.base+r.bytes&&r.base<controls.mapped+controls.bytes),'A native application generation cannot reuse retained controls');retainedControls.set(controls.mapped,{base:controls.mapped,bytes:controls.bytes});Object.assign(staged,{gpu,controls});}
    // Native runtimes can restore this launcher after disconnecting the host.
    // Capture while the common scheduler has joined its graphics worker and
    // before the app replaces the shared menu storage.
    let launcherReturn;
    if(app.manifest.nativeLauncherReturn===true){
     const bytes=await appRPC.read(mapped,0x7f0000);check(bytes.readUInt32LE(0x700004)===0,'Native return snapshot is not the launcher');
     launcherReturn={session:rpc.a.session,bytes,slot:request-1,controls:menu.controls,commands:[...menu.commands[0]],catalog:catalog.apps.map(a=>({id:a.id,fingerprint:a.fingerprint}))};
    }
    const bytes=Buffer.concat([app.files.state.bytes,app.files.data.bytes]);
    bytes.writeUInt32LE(0,1012);bytes.writeUInt32LE(0,1016);
    if(app.manifest.transition==='none')bytes.writeFloatLE(0,960);
    if(app.files.memory){check(gpu.privateStorage?.bytes===app.files.memory.allocationBytes,'App private storage differs');await require('./owned-buffer.cjs').initialize(appRPC,gpu.privateStorage.mapped,gpu.privateStorage.bytes,app.files.memory.bytes,app.manifest.memory);}
    await require('./owned-buffer.cjs').initialize(appRPC,controls.mapped,controls.bytes,bytes);launchScope?.assertUsable();await audio(app,controls.mapped);launchScope?.assertUsable();
    let service;
    if(app.manifest.hostRuntime){
      const match=/^module:([a-z0-9-]+)\/([A-Za-z0-9-]+)$/.exec(app.manifest.hostRuntime);
      check(match,'Invalid application host runtime');
      const component=await (candidate?.getComponent||require('../components.cjs').get)(match[1],match[2]);
      launchScope??=require('./app-rpc-scope.cjs').create(rpc,{id:app.id,execution:controls.execution});
      launchScope.assertUsable();const starting=Promise.resolve().then(()=>component.create({rpc:launchScope.rpc,mapped:controls.mapped,directory,app,videoStorage:gpu.privateStorage||null,sdStorage:require('../module-storage.cjs').forModule(match[1]),...(launcherReturn?{launcherReturn}:{})}));
      if(staged)staged.serviceStarting=starting.then(service=>service,()=>null);service=await starting;launchScope.assertUsable();
    }
    runtime.active=staged?Object.assign(staged,{app,gpu,service,controls}):{app,gpu,service,controls,rpcScope:launchScope};
    if(runtime.preparing===staged){runtime.preparing=null;delete runtime.report.preparing;}
    if(channel)await channel.delegate({id:app.id,controls,gpu});
    runtime.report.active={id:app.id,fingerprint:app.fingerprint,...(controls.mapped!==mapped?{controlMapped:controls.mapped,controlExecution:controls.execution}:{}),...(candidate?{source:'sd',execution:'pc'}:{})};runtime.report.transitions.push({event:'launch',id:app.id,fingerprint:app.fingerprint,...(candidate?{source:'sd',execution:'pc'}:{}),time:new Date().toISOString()});await save();
    return gpu.commands[0];
   }catch(error){
    if(staged&&nativeReturns.has(staged)){await nativeReturns.get(staged);lateOutcome(staged,error);await save();return currentCommands();}
    // A failed scratch restoration means the native session must not be reused.
    if(unsafe(error)){launchScope?.revoke(error);if(staged&&!runtime.active){if(runtime.preparing===staged){runtime.preparing=null;delete runtime.report.preparing;}runtime.active=staged;runtime.report.active={id:selected?.id,epoch:staged.channel.epoch,state:'retained'};await save();}throw error;}
    if(staged)return returnToMenu(error,staged);
    if(channelRequested){runtime.report.lastLaunchError={message:error.message,code:error.code,at:new Date().toISOString()};runtime.report.channelPreparationFailed=true;await save();throw error;}
    if(launchScope&&runtime.active?.rpcScope!==launchScope)await launchScope.close();
    if(error.preflight){
     state.writeUInt32LE(0,1012);state.writeUInt32LE(1,92);await rpc.write(mapped+1012,state.subarray(1012,1016));await rpc.write(mapped+92,state.subarray(92,96));
     runtime.report.lastLaunchError={message:error.message,code:error.code,at:new Date().toISOString()};runtime.report.transitions.push({event:'rejected',id:selected?.id,error:error.message,time:new Date().toISOString()});await save();return menu.commands[0];
    }
    return returnToMenu(error);
   }
  }
  if(state.readFloatLE(104)!==refreshSeen)await rescan(state,true,true);
  return menu.commands[0];
 };
 let closing;runtime.close=()=>closing||(closing=(async()=>{try{const active=runtime.active||runtime.preparing;await closeService(active);if(active?.channel)await active.channel.close();await Promise.all([...localClosures]);}finally{await builtinHosts.close();}})());
 await rescan(menuState,!deferMenuUpload);await audio(null);return runtime;
}
module.exports={create};
