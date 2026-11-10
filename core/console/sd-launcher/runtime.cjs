'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const root=path.resolve(__dirname,'../../..'),sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function main({directory,menu,shell}){
 const rpc=require(path.join(directory,'shared/rpc.cjs'));let stopping=false,supervisor,retained=false,packages,picker,accepted=[];
 const stop=()=>{stopping=true;};process.on('SIGINT',stop);process.on('SIGTERM',stop);
 const wanted=async()=>stopping||await fs.access(path.join(directory,'launcher-loop.stop')).then(()=>true,()=>false);
 const policy=require('../../session/offline-policy.cjs');
 const manifest=JSON.parse(await fs.readFile(path.join(__dirname,'native/build/manifest.json')));if(!manifest.sha256)throw Error('Build the native SD launcher first');
 const fsState=JSON.parse(await fs.readFile(path.join(directory,'fs-rpc-state.json')));if(fsState.state!=='ready'||!fsState.verified)throw Error('Enable FS endpoint (--rpc) before launching SD applications');
 const appRuntime=await require('../../services/app-runtime.cjs').create({rpc,menu,directory,prepareMenu:()=>require(path.join(root,'shells',shell,'server/scene-state.cjs')).initialize({rpc,mapped:menu.controls.mapped})});
 async function publish(state){const updated=new Date().toISOString();if(state.catalogStable)accepted=state.applications;appRuntime.report.sd={...state,path:'SD:'+require('../../module-storage.cjs').BASE+'/',autonomous:true,presentOnCard:packages?.installed||[],pcLibrary:picker?.report||null};appRuntime.report.active=state.active?{id:state.last_id,source:'sd'}:null;await require('../../session/ledger.cjs').saveJson(path.join(directory,'app-runtime.json'),appRuntime.report);await require('../../session/ledger.cjs').saveJson(path.join(directory,'run11-runtime.json'),{session:rpc.a.session,stage:'SD applications',menuReady:state.state===2,updated,available:picker?.report.available||[],installed:accepted,sd:appRuntime.report.sd,active:appRuntime.report.active});}
 try{
  // bootstrap.deploy always prepares native recovery and parks BASIC before
  // starting the SD menu, including when attached through a hardware harness.
  const app=await require(path.join(root,'shells',shell,'server/composition.cjs')).build([],require('../../session/preload.cjs').cacheRoot(root));
  const gpu=await require('../../services/gpu-app.cjs').prepare(rpc,menu,app,directory);if(!gpu.ready||!gpu.scratchRestored)throw Error('SD launcher graphics preparation failed');
  const {metadata,bytes}=await require(path.join(root,'shells',shell,'server/assets.cjs')).loadTypeAtlas();await rpc.write(menu.controls.mapped+1024+metadata.offset,bytes);
  const nativeMenu={...menu,commands:gpu.commands,layout:gpu.layout,controls:{...menu.controls,descriptor:gpu.descriptor}};
  supervisor=await require('./bootstrap.cjs').deploy({directory,menu:nativeMenu});
  const deadline=Date.now()+20000;let s;do{s=await supervisor.status();if(s.state===2)break;if(s.state===4)throw Error('Native SD startup failed: 0x'+s.error.toString(16));await sleep(100);}while(Date.now()<deadline);if(s?.state!==2)throw Object.assign(Error('Native SD startup has not completed; resources retained'),{resourcesRetained:true});
  packages=await require('./server.cjs').create({root,directory,shell,stateRoot:process.env.IZ11_STATE_DIR||path.join(root,'data/state/current'),supervisor,initialState:s,observe:async()=>publish(await supervisor.status())});
  picker=await require('./library.cjs').create({root,directory,shell,supervisor});await picker.step(s);
  await publish(s);console.log('IZ launcher game event source ready.');console.log(shell+' SD runtime ready: '+s.count+' applications; filesystem and transitions run on the console.');
  while(!await wanted()){
   await sleep(1500);const state=await supervisor.status();supervisor.report.observed=state;await supervisor.save();
   if(state.state!==2)throw Error('Native SD runtime stopped: 0x'+state.error.toString(16));
   const commands=path.join(directory,'sd-launcher-rescan.request');if(await fs.access(commands).then(()=>true,()=>false)){await fs.rm(commands);await supervisor.rescan();}
   // Telemetry only. Native discovery, file reads, shader creation, input,
   // presentation, exit and re-entry do not depend on these observations.
   await picker.step(state);const synchronized=await packages.step(state);await publish(synchronized||state);
  }
 }catch(e){
  if(supervisor&&(policy.isTransportLoss(e)||e.resourcesRetained||e.nativeRequestsRetained)){retained=true;supervisor.report.offline={reason:e.message,executionUnverified:true};await supervisor.save();await policy.retain(directory,e.message,{owner:'sd-supervisor',workerWasRunning:true,menuReturnRequiresHost:false});console.log('SD supervisor retained after loss of the PC link.');await policy.waitForStop(wanted);}
  else throw e;
 }finally{
  process.off('SIGINT',stop);process.off('SIGTERM',stop);
  if(supervisor&&!retained)await supervisor.close();
  if(!retained)await appRuntime.close();
 }
}
module.exports={main};
