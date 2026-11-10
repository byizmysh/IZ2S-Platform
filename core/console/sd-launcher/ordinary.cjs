'use strict';
// The native supervisor renders the mixed menu and SD apps continuously.
// The existing host app loader borrows its display only at an acknowledged
// native boundary. Both console shells use this same scheduler.
const assert=require('node:assert/strict'),path=require('node:path');
const policy=require('../../session/offline-policy.cjs');
async function create({rpc,menu,directory,runtime,manager,sdk,supervisor,screen,startHost,stopHost,prepareHost,hostRunning,observeHost,report,save,diagnostics,loadManaged=require('./managed-runtime.cjs').load,clock=()=>performance.now()}){
 const mapped=menu.controls.mapped,frames=require('../../services/frame-rate.cjs').create();let display=null,borrowed=false,lastRefresh=0,lastConnected,lastOwner;
 const appTelemetry=require('../../packages/telemetry.cjs').create({write:()=>require('../../session/ledger.cjs').saveJson(path.join(directory,'app-runtime.json'),runtime.report)});
 let waiting=null,nextObservation=0,readFailures=0,connectionRecord=null,connectionRevision=0,savedConnectionRevision=0,lastCheckpoint;
 const connectionTelemetry=require('../../packages/telemetry.cjs').create({label:'Native menu connection telemetry',write:async()=>{
  const revision=connectionRevision,record=connectionRecord;
  await require('../../session/ledger.cjs').saveJson(path.join(directory,'offline-state.json'),record);await save();savedConnectionRevision=revision;
 }});
 async function saveConnection(record){connectionRecord=record;connectionRevision++;await connectionTelemetry.sample({force:true});await appTelemetry.sample({force:true});}
 // These retries observe memory only. Once any PC owner or uncertain native
 // mutation exists, the original retention path keeps its command boundary.
 function nativeOwnsDisplay(){return !display&&!borrowed&&!runtime.active&&!hostRunning()&&!supervisor.mixed.active&&!supervisor.mixed.retained;}
 async function observationFailed(failure){
  if(!nativeOwnsDisplay()||failure.resourcesRetained||failure.nativeRequestsRetained||failure.restartRequired||!policy.isTransportLoss(failure))throw failure;
  readFailures++;nextObservation=clock()+Math.min(5000,250*2**Math.min(readFailures-1,5));
  if(waiting)return;
  waiting={since:clock(),at:new Date().toISOString(),reason:require('../../session/ledger.cjs').redact(failure.message),observationOnly:true,executionUnverified:true};
  report.offline=runtime.report.offline=waiting;report.nativeMenuConnection={state:'waiting-for-network',automaticReconnect:true};
  await saveConnection({...waiting,state:'offline',owner:'mixed-observation',resourcesRetained:true,automaticReconnect:true,menuReturnRequiresHost:false});
 }
 async function readObservation(read){try{return await read();}catch(failure){await observationFailed(failure);return null;}}
 async function synchronized(){
  if(!waiting)return;
  const durationMs=clock()-waiting.since,at=new Date().toISOString();waiting=null;readFailures=0;nextObservation=0;
  delete report.offline;delete runtime.report.offline;report.nativeMenuConnection={state:'connected',automaticReconnect:true,resynchronizedAt:at,outageMs:durationMs};
  await saveConnection({state:'reconnected',at,owner:'mixed-observation',resourcesRetained:true,automaticReconnect:true,menuReturnRequiresHost:false,observationOnly:true,outageMs:durationMs});
 }
 const currentControls=()=>runtime.currentControls?.()||runtime.active?.controls||menu.controls;
 async function menuState(){const at=currentControls().mapped,spans=[{address:at,length:1024},{address:at+0x7f0800,length:2052},{address:at+0x700d7c,length:4}],parts=[];if(rpc.readMany)parts.push(...await rpc.readMany(spans));else for(const span of spans)parts.push(await rpc.read(span.address,span.length));return {state:parts[0],mailbox:parts[1],menuMarker:parts[2].readUInt32LE()};}
 function managedRequest(native){const app=native.applications?.[native.host_launch-1];return app?.source==='sd'&&app.execution==='pc'?app:null;}
 function sdRequest(state,native){const count=runtime.report.catalog?.apps.length||0;return state.readUInt32LE(1012)>count&&!managedRequest(native)||state.readUInt32LE(944)>count;}
 async function observe(native){
  assert(native.state===2&&!native.poisoned,'The native mixed menu stopped');manager.observeNative?.(native);
  manager.report.sd={...native,autonomous:true,mixed:true};runtime.report.sd=manager.report.sd;
  const checkpointKey=[native.last_id,native.checkpoint_state,native.checkpoint_generation,native.checkpoint_error].join(':');
  if(checkpointKey!==lastCheckpoint){
   lastCheckpoint=checkpointKey;
   if(native.checkpoint_state===2)console.log('[SD state] Restored '+native.last_id+' from checkpoint '+native.checkpoint_generation+'.');
   else if(native.checkpoint_state===4)console.log('[SD state] Saved '+native.last_id+' to SD ('+native.checkpoint_stored+' bytes).');
   else if(native.checkpoint_error)console.log('[SD state] Checkpoint unavailable for '+native.last_id+' (0x'+native.checkpoint_error.toString(16)+'); continuing without replacing a checkpoint.');
  }
  if(!runtime.active)runtime.report.active=native.active?{id:native.last_id,source:'sd'}:null;
  const owner=runtime.report.active?.id||'menu',changed=owner!==lastOwner;if(changed){frames.reset();lastOwner=owner;screen.setAvailable(!runtime.report.active);}
  if(!hostRunning()){
   const sample=frames.sample(native.frames>>>0,performance.now()),batch={status:'native-menu',frames:sample?.frames||0,interval:sample?.interval||null,at:new Date().toISOString()};
   report.nativeState={state:native.state,active:native.active,frames:native.frames,count:native.count,scan_generation:native.scan_generation};await manager.sample(batch,{force:changed});
  }
  runtime.report.updated=new Date().toISOString();await appTelemetry.sample({force:changed});return native;
 }
 async function step(){
  if(savedConnectionRevision!==connectionRevision)await connectionTelemetry.sample();
  if(clock()<nextObservation)return;
  let sampled=clock();const native=await readObservation(()=>supervisor.status());if(!native||clock()-sampled>500)return;
  if(waiting&&(native.catalogStable===false||native.requestStable===false))return;
  await observe(native);if(native.active){await synchronized();return;}
  if(hostRunning())await sdk.hardware?.refreshControllers?.();
  sampled=clock();const initial=hostRunning()?await observeHost():await readObservation(menuState);if(!initial||clock()-sampled>500||!runtime.active&&initial.menuMarker!==2)return;
  screen.onState(initial.state);
  const refresh=initial.state.readFloatLE(104),connected=!!(initial.state.readUInt32LE(32)&1),connectionChanged=lastConnected!==undefined&&connected!==lastConnected;lastConnected=connected;
  if(!runtime.active&&sdRequest(initial.state,native))return; // Native consumption wins.
  const nativeAction=!runtime.active&&native.host_display_request!==native.host_display_release&&(native.host_launch>0||native.host_remove>0);
  let appRequest=runtime.active?initial.state.readUInt32LE(1016)!==0:initial.state.readUInt32LE(1012)!==0||refresh!==lastRefresh||nativeAction;
  const serviceRequest=initial.mailbox.readUInt32LE()!==0&&initial.mailbox.readUInt32LE()!==initial.mailbox.readUInt32LE(2048);
  const packages=await readObservation(()=>manager.pending(initial.state));if(packages===null)return;
  const packageRequest=packages||nativeAction&&native.host_remove>0||!runtime.active&&native.requestStable&&native.host_install_sequence>0,hostRequest=!appRequest&&await runtime.pending(initial.state);
  if(!appRequest&&!serviceRequest&&!connectionChanged&&!packageRequest&&!hostRequest){await synchronized();return;}
  try{if(!display)display=await supervisor.mixed.acquire();}catch(error){if(error.menuBusy)return;if(error.displayNotClaimed){await observationFailed(error);return;}throw error;}
  await synchronized();
  await stopHost();const state=await rpc.read(currentControls().mapped,1024);
  if(!runtime.active&&sdRequest(state,native)){await display.release();display=null;return;}
  const managed=!runtime.active&&display.request.launch&&display.request.application?.source==='sd'&&display.request.application.execution==='pc'?display.request.application:null;
  let managedFailed=false;
  if(managed){
   try{runtime.queueExternal(await loadManaged({directory,id:managed.id,lease:display}));}
   catch(error){if(error.resourcesRetained||error.restartRequired||policy.isTransportLoss(error))throw error;await runtime.rejectLaunch(error);managedFailed=true;}
   state.writeUInt32LE(0,1012);await rpc.write(mapped+1012,Buffer.alloc(4));
  }
  const pendingInstall=display.pendingInstall;
  if(pendingInstall&&!runtime.active&&!state.readUInt32LE(1012)){state.writeUInt32LE(pendingInstall.sequence,808);state.writeUInt32LE(pendingInstall.index,812);state.writeUInt32LE(pendingInstall.target,792);await rpc.write(mapped+784,state.subarray(784,848));}
  appRequest=runtime.active?state.readUInt32LE(1016)!==0:state.readUInt32LE(1012)!==0||state.readFloatLE(104)!==lastRefresh;
  if(managed||appRequest||packageRequest){screen.setAvailable(false);await screen.suspend();}
  const previous=runtime.active,generation=runtime.report.generation,boundary=await require('../host/transition.cjs').begin(sdk,{app:runtime.active?.app,controls:currentControls(),appRequest:!!managed||appRequest,serviceRequest,connectionChanged});
  if(!managed&&(appRequest||packageRequest))await manager.perform(state);
  if(pendingInstall&&manager.acceptedInstallSequence===pendingInstall.sequence)await display.acknowledgeInstall(pendingInstall.sequence);
  await runtime.afterBatch();if(previous&&runtime.active!==previous)sdk.retireControls?.(previous.controls);const changed=!runtime.active&&(!!managed||managedFailed||appRequest||packageRequest||generation!==runtime.report.generation);
  if(changed){await manager.atBoundary();await screen.refresh();screen.setVideoStorage(menu.streamStorage);}
  await boundary.finish(runtime.active?.app,currentControls());
  if(runtime.active){await prepareHost(runtime.active.gpu.commands[0],currentControls());await startHost();}
  else{
   if(changed){
    // Native SD entries follow the stable PC prefix. The parent needs only
    // that prefix, while the native worker re-appends SD tiles after release.
    const header=await rpc.read(mapped,1024),count=runtime.report.catalog.apps.length;header.writeUInt32LE(count,112);header.writeUInt32LE(count,1020);await rpc.write(mapped,header);
    await display.publish({applications:runtime.report.catalog.apps,menu});
   }
   lastRefresh=(await rpc.read(mapped+104,4)).readFloatLE();await display.release();display=null;screen.setAvailable(true);await appTelemetry.sample({force:true});
  }
 }
 return {step,supervisor,get held(){return !!display?.active;},get recovering(){return !!waiting;},
  async stopForBorrow(){if(runtime.active||hostRunning())throw Object.assign(Error('Return the PC application to the menu before borrowing its display'),{menuBusy:true});display=await supervisor.mixed.acquire();if(display.request.launch||display.request.remove){await display.release();display=null;throw Object.assign(Error('A console menu action already owns the display'),{menuBusy:true});}borrowed=true;report.threadRunning=false;report.nativeMenuParked=display.token;await save();},
  async resumeBorrow(){assert(borrowed&&display?.active&&!runtime.active);await display.release();display=null;borrowed=false;delete report.nativeMenuParked;await save();},
  async close(){await stopHost();const active=!!runtime.active;if(active)await runtime.close();await supervisor.close();report.nativeMenuRetained=false;await screen.close();if(!active)await runtime.close();await manager.close();await save();},
 };
}
async function main(options){
 const {rpc,menu,directory,runtime,manager,report,save,stopping}=options;let controller,supervisor,screen,retained=false,error;
 report.nativeMenuStarting=true;report.nativeMenuStartingPid=process.pid;await save();
 try{
  supervisor=await require('./bootstrap.cjs').deploy({directory,menu,hostApplications:runtime.report.catalog.apps});
  report.nativeMenuRetained=true;delete report.nativeMenuStarting;delete report.nativeMenuStartingPid;await save();
  const until=Date.now()+20000;for(;;){const s=await supervisor.status();assert(s.state!==4,'Native mixed-menu startup failed');if(s.state===2)break;assert(Date.now()<until,'Native mixed-menu startup did not finish');await new Promise(r=>setTimeout(r,25));}
  screen=await require('../../session-hooks.cjs').create({context:{rpc,mapped:menu.controls.mapped,directory,controllerInventory:()=>options.sdk.hardware.controllers}});screen.setVideoStorage(menu.streamStorage);
  controller=await create({...options,supervisor,screen});
  options.registerBoundary?.(controller);
  const borrow=require('../native/display-handoff.cjs').create({directory,session:rpc.a.session,stopWorker:()=>controller.stopForBorrow(),startWorker:()=>controller.resumeBorrow(),report,save});
  await controller.step();await options.onReady?.();console.log('IZ launcher game event source ready.');
  console.log('IZ launcher mixed native menu running; SD discovery and launch do not require PC callbacks.');
  while(!await stopping()){if(await borrow())continue;await controller.step();const interval=await screen.pollInterval();await new Promise(r=>setTimeout(r,interval));}
 }catch(failure){
  error=failure;
  if(failure.resourcesRetained||failure.restartRequired||policy.isTransportLoss(failure)){
   retained=true;report.nativeMenuRetained=true;report.offline={reason:failure.message,executionUnverified:true};await save();await policy.retain(directory,failure.message,{owner:'mixed-supervisor',workerWasRunning:!!supervisor,menuReturnRequiresHost:false});await policy.waitForStop(stopping);
  }else throw failure;
 }finally{
  if(!retained)try{if(controller)await controller.close();else if(supervisor){await supervisor.close();report.nativeMenuRetained=false;await screen?.close();await save();}else{report.nativeMenuRetained=false;delete report.nativeMenuStarting;delete report.nativeMenuStartingPid;await save();}}
  catch(cleanup){report.nativeMenuRetained=true;report.cleanupError=cleanup.message;await save();throw Object.assign(error||cleanup,{resourcesRetained:true});}
 }
}
module.exports={create,main};
