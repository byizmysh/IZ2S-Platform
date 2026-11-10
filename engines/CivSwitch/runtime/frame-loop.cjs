'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const {create}=require('./client.cjs');
const stateNames=['_ZN2nn3hid15GetNpadStyleSetERKj','_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj',
  '_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj','_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj',
  '_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj','_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj',
  '_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE'];
const struct=(size,type,words={},pointers={})=>{const b=Buffer.alloc(size);b.writeUInt32LE(type);for(const [at,v]of Object.entries(words))b.writeUInt32LE(v,+at);for(const [at,v]of Object.entries(pointers))b.writeBigUInt64LE(BigInt(v),+at);return b;};
async function main(directory,{frames=Infinity,shell='izlauncher',store=true}={}){
  if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Choose IZlauncher or IZPSlauncher');
  const renderer=path.join(__dirname,'..',shell==='izpslauncher'?'pslauncher':'launcher');
  directory=path.resolve(directory);
  const options=JSON.parse(await fs.readFile(path.join(directory,'civswitch-connection.json'))),client=create({host:options.host});
  await client.connect();await client.quiet(true);
  await client.audioClockMode(process.env.IZ2S_CIV_AUDIO_CLOCK||'off');
  const menu=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto.json')));
  if(!menu.ready||!menu.scratchRestored||menu.session!==options.addresses.session)throw Error('CivSwitch menu preparation is incomplete');
  const rpc=require(path.join(directory,'shared/rpc.cjs'));
  for(const name of [...stateNames,'_ZN2nn2os13GetSystemTickEv','_ZN2nn2os22GetSystemTickFrequencyEv'])await client.lookup(name);
  const tickFrequency=Number(await client.call('_ZN2nn2os22GetSystemTickFrequencyEv'));
  if(!Number.isSafeInteger(tickFrequency)||tickFrequency<1)throw Error('Invalid Civ7 hardware clock');
  for(const name of ['vkAcquireNextImage2KHR','vkQueueSubmit','vkQueuePresentKHR','vkQueueWaitIdle'])await client.lookup(name,'graphics');
  const c=options.addresses.scratch+0x10000,r=menu.resources;
  const data=Buffer.alloc(4096),put=(at,b)=>b.copy(data,at),u64=(at,v)=>data.writeBigUInt64LE(BigInt(v),at);
  put(0x100,struct(56,1000060010,{48:1},{16:r.swapchain,24:1000000000n,32:r.acquired}));
  u64(0x410,r.acquired);u64(0x418,r.swapchain);data.writeUInt32LE(0x400,0x420);data.writeUInt32LE(32,0x504);
  put(0x200,struct(72,4,{16:1,40:1,56:1},{24:c+0x410,32:c+0x420,48:c+0x400,64:c+0x408}));
  put(0x300,struct(64,1000001001,{16:1,32:1},{24:c+0x408,40:c+0x418,48:c}));
  await rpc.write(c,data);
  const prepareApp=require('./gpu-app.cjs').prepare;
  const runtime=await require('../../../core/services/app-runtime.cjs').create({rpc,menu,directory,deferMenuUpload:true,prepareApp,
    rasterMenu:require(path.join(renderer,'raster.cjs')).raster,
    prepareMenu:()=>require(path.join(renderer,'scene-state.cjs')).initialize({rpc,mapped:menu.controls.mapped})});
  const report={session:menu.session,engine:'civswitch',filesystem:false,started:new Date().toISOString(),frames:0,resourcesRetained:true};
  const save=()=>fs.writeFile(path.join(directory,'civswitch-loop.json'),JSON.stringify(report,null,2),{mode:0o600});
  let retained=false,stopped=false;const stop=()=>{stopped=true;};process.once('SIGINT',stop);process.once('SIGTERM',stop);
  let commands=menu.commands[0],manager,lastStore=0,lastSavedFrames=0;
  const frame=async(count=2)=>client.frame({mapped:menu.controls.mapped,control:c,device:r.device,queue:r.queue,commands,semaphores:r.semaphores,tickFrequency,exitGesture:!!runtime.active},count);
  try{
    if(store)manager=await require(path.join(renderer,'package-manager.cjs')).create({rpc,menu,directory,runtime,prepareShell:prepareApp,
      validateCandidate:app=>rpc.validateApp(app),redrawProgress:()=>frame(1)});
    commands=menu.commands[0];
    console.log('CIV7_LAUNCHER_READY '+shell);
    while(!stopped&&report.frames<frames){
      if(await fs.access(path.join(directory,'civswitch-loop.stop')).then(()=>true,()=>false))break;
      const state=await frame(Math.min(runtime.active?6:2,frames-report.frames));report.frames+=state.frames;report.input=state;
      if(state.audioClock?.running)report.lastActiveAudioClock={...state.audioClock,at:new Date().toISOString()};
      if(state.audioQueue)report.lastActiveAudioQueue={...state.audioQueue,at:new Date().toISOString()};
      let storeWork=false;const generation=runtime.report.generation,active=runtime.active;
      if(manager&&Date.now()-lastStore>=300){lastStore=Date.now();const bytes=await rpc.read(menu.controls.mapped,1024);storeWork=await manager.pending(bytes);if(storeWork)await manager.perform(bytes);}
      commands=await runtime.afterBatch();
      if(manager&&(storeWork||generation!==runtime.report.generation||active!==runtime.active)){await manager.preparingRAM();await manager.atBoundary();commands=runtime.active?.gpu.commands[0]||menu.commands[0];}
      if(report.frames-lastSavedFrames>=60){lastSavedFrames=report.frames;await manager?.sample({frames:report.frames});await save();}
    }
  }catch(error){
    report.error=error.message;
    if(!require('./lifetime.cjs').retain(error,{client,rpc}))throw error;
    retained=true;report.offline={at:new Date().toISOString(),reason:error.message,autonomous:false,resourcesRetained:true,executionUnverified:true};await save();
    console.log('CIV7_LAUNCHER_OFFLINE '+error.message);
    await require('../../../core/session/offline-policy.cjs').waitForStop(async()=>stopped||await fs.access(path.join(directory,'civswitch-loop.stop')).then(()=>true,()=>false));
  }
  finally{
    process.removeListener('SIGINT',stop);process.removeListener('SIGTERM',stop);
    await require('./lifetime.cjs').finish({retained,report,save,cleanup:async()=>{await manager?.close();await runtime.close();}});
    // The independent layer remains visible. Do not invoke the SB4 recovery
    // worker or destroy an unjoined driver's presentation objects.
    if(!retained&&!client.uncertain&&!rpc.uncertain)try{await client.quiet(false);}catch(error){report.cleanupError=error.message;await save();}
  }
}
if(require.main===module)main(process.argv[2],{frames:process.argv[3]?Number(process.argv[3]):Infinity,shell:process.argv[4]||'izlauncher'}).catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={main};
