'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const root=path.resolve(__dirname,'../../..'),sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function rescanWhenIdle(directory,state,supervisor){
 if(state.active||state.rescan||state.catalogStable===false)return;
 return require('./rescan.cjs').submit(directory,supervisor);
}
async function main({directory,menu,shell}){
 const rpc=require(path.join(directory,'shared/rpc.cjs'));let stopping=false,supervisor,retained=false,packages,picker,accepted=[],lastLaunchFailure=0,lastSdReturn=0,linkLoss=null;
 const stop=()=>{stopping=true;};process.on('SIGINT',stop);process.on('SIGTERM',stop);
 const wanted=async()=>stopping||await fs.access(path.join(directory,'launcher-loop.stop')).then(()=>true,()=>false);
 const policy=require('../../session/offline-policy.cjs');
 const ledger=require('../../session/ledger.cjs');
 const connectionFile=path.join(directory,'offline-state.json');
 async function observeLink(action){
  try{return await action();}
  catch(error){
   if(policy.requiresRetention(error)||!policy.isTransportLoss(error))throw error;
   if(!linkLoss){
    linkLoss={at:new Date().toISOString(),reason:ledger.redact(error.message)};
    await ledger.saveJson(connectionFile,{...linkLoss,state:'offline',owner:'sd-observation',automaticReconnect:true,resourcesRetained:true,menuReturnRequiresHost:false,executionUnverified:true});
    console.log('[SD] Waiting for the PC link. The native menu stays active.');
   }
   return null;
  }
 }
 async function synchronized(){
  if(!linkLoss)return;
  await ledger.saveJson(connectionFile,{state:'reconnected',at:new Date().toISOString(),owner:'sd-observation',automaticReconnect:true,resourcesRetained:true,menuReturnRequiresHost:false});
  linkLoss=null;
 }
 const manifest=JSON.parse(await fs.readFile(path.join(__dirname,'native/build/manifest.json')));if(!manifest.sha256)throw Error('Build the native SD launcher first');
 const fsState=JSON.parse(await fs.readFile(path.join(directory,'fs-rpc-state.json')));if(fsState.state!=='ready'||!fsState.verified)throw Error('Enable FS endpoint (--rpc) before launching SD applications');
 const appRuntime=await require('../../services/app-runtime.cjs').create({rpc,menu,directory,prepareMenu:()=>require(path.join(root,'shells',shell,'server/scene-state.cjs')).initialize({rpc,mapped:menu.controls.mapped})});
 async function publish(state){const updated=new Date().toISOString();if(state.catalogStable)accepted=state.applications;appRuntime.report.sd={...state,path:'SD:'+require('../../module-storage.cjs').BASE+'/',autonomous:true,presentOnCard:packages?.installed||[],pcLibrary:picker?.report||null};appRuntime.report.active=state.active?{id:state.last_id,source:'sd'}:null;await ledger.saveJson(path.join(directory,'app-runtime.json'),appRuntime.report);await ledger.saveJson(path.join(directory,'run11-runtime.json'),{session:rpc.a.session,stage:'SD applications',menuReady:state.state===2,updated,available:picker?.report.available||[],installed:accepted,sd:appRuntime.report.sd,active:appRuntime.report.active});}
 try{
  // bootstrap.deploy always prepares native recovery and parks BASIC before
  // starting the SD menu, including when attached through a hardware harness.
  const app=await require(path.join(root,'shells',shell,'server/composition.cjs')).build([],require('../../session/preload.cjs').cacheRoot(root));
  const gpu=await require('../../services/gpu-app.cjs').prepare(rpc,menu,app,directory);if(!gpu.ready||!gpu.scratchRestored)throw Error('SD launcher graphics preparation failed');
  const {metadata,bytes}=await require(path.join(root,'shells',shell,'server/assets.cjs')).loadTypeAtlas();await rpc.write(menu.controls.mapped+1024+metadata.offset,bytes);
  const nativeMenu={...menu,commands:gpu.commands,layout:gpu.layout,controls:{...menu.controls,descriptor:gpu.descriptor}};
  supervisor=await require('./bootstrap.cjs').deploy({directory,menu:nativeMenu});
  let deadline=Date.now()+20000,s;
  while(!await wanted()){
   const recovering=!!linkLoss;
   s=await observeLink(()=>supervisor.status());
   if(s){
    if(recovering&&s.state===2&&(!s.catalogStable||!s.requestStable)){await sleep(1500);continue;}
    if(s.state===4)throw Error('Native SD startup failed: 0x'+s.error.toString(16));
    await synchronized();
    if(s.state===2)break;
    if(recovering)deadline=Date.now()+20000;
    if(Date.now()>=deadline)throw Object.assign(Error('Native SD startup has not completed; resources retained'),{resourcesRetained:true});
   }
   await sleep(s?100:1500);
  }
  if(s?.state!==2)return;
  packages=await require('./server.cjs').create({root,directory,shell,stateRoot:process.env.IZ11_STATE_DIR||path.join(root,'data/state/current'),supervisor,initialState:s,observe:async()=>publish(await supervisor.status())});
  picker=await require('./library.cjs').create({root,directory,shell,supervisor});await picker.step(s);
  await publish(s);console.log('IZ launcher game event source ready.');console.log(shell+' SD runtime ready: '+s.count+' applications; filesystem and transitions run on the console.');
  while(!await wanted()){
   await sleep(1500);if(await wanted())break;
   const sampled=performance.now(),state=await observeLink(()=>supervisor.status());
   if(!state||performance.now()-sampled>500)continue;
   if(state.state!==2)throw Error('Native SD runtime stopped: 0x'+state.error.toString(16));
   if(linkLoss&&(!state.catalogStable||!state.requestStable))continue;
   supervisor.report.observed=state;await supervisor.save();
   const failure=state.launchFailure;
   if(failure&&failure.sequence!==lastLaunchFailure){lastLaunchFailure=failure.sequence;console.log('[SD launch] '+failure.id+' failed at stage '+failure.stage+' (0x'+failure.error.toString(16)+').');}
   const returns=state.sdReturns||(state.lastSdReturn?[state.lastSdReturn]:[]);
   for(const returned of returns){
    const distance=(returned.sequence-lastSdReturn)>>>0;
    if(lastSdReturn&&(!distance||distance>=0x80000000))continue;
    lastSdReturn=returned.sequence;
    const outcome=returned.error?' returned with error 0x'+returned.error.toString(16):' returned to the menu';
    console.log('[SD] '+returned.id+outcome+'.');
   }
   if(await observeLink(()=>rescanWhenIdle(directory,state,supervisor))===null)continue;
   await synchronized();
   // Publication and package jobs can hold native ownership; failures from
   // those operations still use the retention policy instead of replay.
   await picker.step(state);const latest=await packages.step(state);await publish(latest||state);
  }
 }catch(e){
  if(supervisor&&(policy.isTransportLoss(e)||policy.requiresRetention(e))){retained=true;supervisor.report.offline={reason:e.message,executionUnverified:true};await supervisor.save();await policy.retain(directory,e.message,{owner:'sd-supervisor',workerWasRunning:true,menuReturnRequiresHost:false});console.log('SD supervisor retained after loss of the PC link.');await policy.waitForStop(wanted);}
  else throw e;
 }finally{
  process.off('SIGINT',stop);process.off('SIGTERM',stop);
  if(linkLoss)retained=true;
  if(supervisor&&!retained)await supervisor.close();
  if(!retained)await appRuntime.close();
 }
}
module.exports={main,rescanWhenIdle};
