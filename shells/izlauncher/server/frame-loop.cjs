// Continuous console-side frame submission. Host pauses only for explicit transitions.
// Stop between batches with RUN_DIRECTORY/launcher-loop.stop.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const code=require('../../../core/console/abi/thread-code.json').functions;
const timing=require('../../../core/console/abi/timing-code.json').functions;
const streamClock=require('./stream-clock.cjs');
const offlinePolicy=require('../../../core/session/offline-policy.cjs');
const {verifyInput}=require('../../../core/services/input-resolution.cjs');
const check=(v,m)=>{if(!v)throw Error(m);};
const struct=(n,type,words={},ptrs={})=>{const b=Buffer.alloc(n);b.writeUInt32LE(type);for(const[k,v]of Object.entries(words))b.writeUInt32LE(v,+k);for(const[k,v]of Object.entries(ptrs))b.writeBigUInt64LE(BigInt(v),+k);return b;};
async function main(directory,attempt='',instance=''){
 check(!instance||/^[a-z0-9-]+$/.test(instance),'Invalid loop instance');
 check(!attempt||/^[a-z0-9-]+$/.test(attempt),'Invalid attempt label');const stem='launcher'+(attempt?'-'+attempt:'');const loopStem=stem+(instance?'-'+instance:'');
 directory=path.resolve(directory);const rpc=require(path.join(directory,'shared/rpc.cjs')),sdk=rpc.a.sdk;
 const prepared=JSON.parse(await fs.readFile(path.join(directory,stem+'.json'))),module=JSON.parse(await fs.readFile(path.join(directory,'compatibility-module.json')));
 check(prepared.ready&&prepared.scratchRestored&&prepared.session===rpc.a.session,'Shader preparation incomplete');
 const qualified=JSON.parse(await fs.readFile(path.join(directory,stem+'-check.json')));check(qualified.session===rpc.a.session&&qualified.passed&&qualified.scratchRestored,'Launcher input checks incomplete');
 const file=path.join(directory,loopStem+'-loop.json');await fs.writeFile(file,'{}',{flag:'wx'});
 const report={session:rpc.a.session,started:new Date().toISOString(),batches:[],resourcesRetained:true};
 const save=()=>require('../../../core/session/ledger.cjs').saveJson(file,report);
 const telemetry=require('../../../core/packages/telemetry.cjs').create({write:save,label:'IZlauncher render telemetry'});
 const residentAppTelemetry=require('../../../core/packages/telemetry.cjs').create({write:()=>require('../../../core/session/ledger.cjs').saveJson(path.join(directory,'app-runtime.json'),runtime.report),label:'IZlauncher resident app telemetry'});
 const diagnostics=require('../../../core/packages/runtime-log.cjs').create({directory,stateRoot:process.env.IZ11_STATE_DIR||path.join(require('../../../core/root.cjs'),'data/state/current')});
 const cleanup=require('../../../core/packages/runtime-log.cjs').cleanup;let primaryError;
 let stop=false;process.on('SIGINT',()=>stop=true);process.on('SIGTERM',()=>stop=true);
 const stopping=async()=>stop||await fs.access(path.join(directory,'launcher-loop.stop')).then(()=>true,()=>false);
 const sdkImage=require('../../../core/console/abi/sdk-checks.cjs');
 const residentMode=process.env.IZ18_RESIDENT_GRAPHICS==='1';let resident=null;let thread=null,lastHeartbeat=0n,lastRefresh=0,lastConnected;
 let runtime;
 let sdkRuntime;
 try{
 await diagnostics.stage('initialize-app-runtime',{},true);
 runtime=await require('../../../core/services/app-runtime.cjs').create({rpc,menu:prepared,directory,resident:residentMode,prepareMenu:()=>require('./scene-state.cjs').initialize({rpc,mapped:prepared.controls.mapped})});
 for(const c of [...Object.values(code),timing.tick]){
  const expected=Buffer.from(c.bytes,'hex').subarray(0,256);await rpc.request({command:'verify-function',address:sdk+c.offset,hex:expected.toString('hex')});
 }
 const lookup=sdk+0x45c288;await rpc.request({command:'verify-function',address:lookup,hex:sdkImage.bytes(0x45c288,16).toString('hex')});
 const freq=module.base+0x13d6f0;await rpc.request({command:'verify-function',address:freq,hex:'00ca9a52803ba072c0035fd6'});
 const frequency=Number((await rpc.callAddress(freq,[])).result);check(frequency===31250000,'Unexpected timer frequency');report.counterFrequency=frequency;
 await rpc.request({command:'verify-function',address:sdk+0x586fb0,hex:require('../../../core/console/abi/ipc-functions.json').copy.hex});
 const verified=new Set();for(const name of ['vkAcquireNextImage2KHR','vkQueueSubmit','vkQueuePresentKHR','vkQueueWaitIdle']){
  const symbol=module.symbols.find(s=>s.name===name&&s.section&&s.type===2),address=module.base+symbol.offset;check(address===prepared.functions[name],'Function address changed');
  await rpc.request({command:'verify-function',address,hex:(await rpc.read(address,32)).toString('hex')});verified.add(address);
 }
 const inputInspection=JSON.parse(await fs.readFile(path.join(directory,'input-inspection.json')));check(inputInspection.session===rpc.a.session,'Input inspection session differs');
 const input=inputInspection.imports['_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj'];
 await verifyInput(rpc,'_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj',input);verified.add(input.target);report.input={type:'handheld',id:32,target:input.target};
 const touch=inputInspection.imports['_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE'];
 await verifyInput(rpc,'_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE',touch);verified.add(touch.target);report.touch={target:touch.target,offset:128};
 const temp=rpc.a.scratch+0x10000,saved=await rpc.read(temp,8);let base;
 try{check((await rpc.call(5732112,[temp,4096,0x400000])).result===0n,'Allocation failed');base=Number((await rpc.read(temp,8)).readBigUInt64LE());}finally{await rpc.write(temp,saved);}
 check(base>0&&base%4096===0,'Bad allocation');report.base=base;await save();
 const control=base+0x1000;
 const r=prepared.resources,device=BigInt(r.device),queue=BigInt(r.queue),swapchain=BigInt(r.swapchain),acquired=BigInt(r.acquired),frameCount=6;
 const data=Buffer.alloc(0x9000),put=(o,b)=>b.copy(data,o),u64=(o,v)=>data.writeBigUInt64LE(BigInt(v),o);
 put(0x100,struct(56,1000060010,{48:1},{16:swapchain,24:0xffffffffffffffffn,32:acquired}));
 u64(0x200,acquired);data.writeUInt32LE(0x400,0x210);u64(0x220,swapchain);data.writeUInt32LE(32,0x230);
 const semObject=control+0x800;u64(0x850,control+0x880);r.semaphores.forEach((v,i)=>u64(0x880+i*8,v));
 put(0x600,struct(64,1000001001,{16:1,32:1},{24:semObject+0x28,40:control+0x220,48:control}));
 for(let phase=0;phase<prepared.commands.length;phase++){
  const o=0x1000+phase*0x100,obj=control+o;u64(o+0x50,obj+0x60);prepared.commands[phase].forEach((v,i)=>u64(o+0x60+i*8,v));
  put(o+0x80,struct(72,4,{16:1,40:1,56:1},{24:control+0x200,32:control+0x210,48:obj+0x28,64:semObject+0x28}));
 }
 await rpc.write(control,data);
 // Check dynamic selection with the final swapchain index before launching.
 const last=r.images.length-1;await rpc.callAddress(lookup,[semObject,last]);check((await rpc.read(semObject+0x28,8)).readBigUInt64LE()===BigInt(r.semaphores[last]),'Semaphore selector differs');
 await diagnostics.stage('initialize-input',{},true);
 sdkRuntime=await require('../../../core/console/host/runtime.cjs').create({rpc,mapped:prepared.controls.mapped,directory});
 report.sdk={joyMouse:!!sdkRuntime.hardware.joyMouse,joyMouseError:sdkRuntime.hardware.joyMouseError};await save();
 await sdkRuntime.atBoundary();report.sdk.controllers=sdkRuntime.hardware.controllers;await save();
 let packageManager;
 if(residentMode){
  const finiteAudio=process.env.IZ18_RESIDENT_FINITE_AUDIO==='1';
  console.log(finiteAudio?'Preparing resident transitions with the experimental finite console audio adapter (11 min 39 s per session).':'Preparing resident graphics session; audio output is unavailable in this experiment.');
  resident=await require('../resident/prepare.cjs').prepare({rpc,menu:prepared,directory,catalog:runtime.report.catalog,finiteAudio});
  prepared.commands=resident.commands;report.resident={ready:true,graphics:true,audio:false,audioMode:resident.audio.status().mode,slots:resident.report.slots.map(s=>({id:s.id,slot:s.slot}))};await save();
  const ramMenu={...prepared,controls:resident.menu.controls,commands:resident.menu.gpu.commands};
  await runtime.close();runtime=await require('../../../core/services/app-runtime.cjs').create({rpc,menu:ramMenu,directory,resident:true});
  packageManager=await require('./package-manager.cjs').create({rpc,menu:ramMenu,directory,runtime,
   prepareShell:async()=>resident.menu.gpu,
   packageEligible:pkg=>(finiteAudio?pkg.residentAudio:pkg.resident)?.compatible===true,
   validateCandidate:async(app,current)=>resident.plan([...current.filter(a=>a.id!==app.id),app]),
   validateRemoval:async()=>{throw Error('This application stays in RAM until the resident session ends. Start a new session to free its memory.');},
   redrawProgress:()=>require('./progress-frame.cjs').present({rpc,menu:ramMenu,idle:()=>thread===null})});
 }else packageManager=await require('./package-manager.cjs').create({rpc,menu:prepared,directory,runtime,redrawProgress:()=>require('./progress-frame.cjs').present({rpc,menu:prepared,idle:()=>thread===null&&!runtime.active})});
 // Switch to the post-menu shell only after the original qualification passes.
 for(let phase=0;phase<prepared.commands.length;phase++){
  const handles=Buffer.alloc(prepared.commands[phase].length*8);prepared.commands[phase].forEach((v,i)=>handles.writeBigUInt64LE(BigInt(v),i*8));await rpc.write(control+0x1060+phase*0x100,handles);
 }
 let frame,proof,checks,program,logicalCalls,planFingerprint;
 async function prepareFrame(){
 const calls=[];checks=[];const add=(fn,args,expectZero=false)=>{if(expectZero)checks.push(calls.length);calls.push({function:fn,args:[...args,...Array(4-args.length).fill(0)]});};
 add(sdk+timing.tick.offset,[]);
 for(let f=0;f<frameCount;f++){
  const obj=control+0x1000+(f%prepared.commands.length)*0x100;
  const frameInput=sdkRuntime.hardware.frameCalls(prepared.controls.mapped);
  if(!frameInput.some(call=>call.args[0]===prepared.controls.mapped))add(input.target,[prepared.controls.mapped,control+0x230]);
  for(const call of frameInput){verified.add(call.function);add(call.function,call.args);}
  add(touch.target,[prepared.controls.mapped+128]);
  streamClock.append(calls,{sdk,base,mapped:prepared.controls.mapped,tick:sdk+timing.tick.offset});
  add(prepared.functions.vkAcquireNextImage2KHR,[device.toString(),control+0x100,control],true);
  add(lookup,[obj,{u32:control}]);add(lookup,[semObject,{u32:control}]);
  add(prepared.functions.vkQueueSubmit,[queue.toString(),1,obj+0x80,0],true);
  add(prepared.functions.vkQueuePresentKHR,[queue.toString(),control+0x600],true);
  add(prepared.functions.vkQueueWaitIdle,[queue.toString()],true);
 }
 add(sdk+timing.tick.offset,[]);
 logicalCalls=calls;
 await rpc.write(prepared.controls.mapped+streamClock.FREQUENCY_OFFSET,streamClock.frequencyWord(frequency));
 const fingerprint=crypto.createHash('sha256').update(JSON.stringify(calls)).digest('hex');
 if(program&&fingerprint===planFingerprint){report.reusedFramePlans=(report.reusedFramePlans||0)+1;await save();return;}
 program=await require('../../../core/console/native/repeating.cjs').prepare(rpc,{base,code,calls,verified});frame=program.frame;proof=program.proof;
 planFingerprint=fingerprint;
 report.proof=proof;report.frameSha256=crypto.createHash('sha256').update(frame).digest('hex');report.framesPerCycle=frameCount;report.continuous=true;await save();
 await fs.writeFile(path.join(directory,loopStem+'-loop-frame.bin'),frame);

 }
 await prepareFrame();
 await fs.writeFile(path.join(directory,'game-audio-source.json'),JSON.stringify({session:rpc.a.session,mapped:prepared.controls.mapped}));
 console.log('IZ launcher game event source ready.');
 const frameRate=require('../../../core/services/frame-rate.cjs').create();
 async function startWorker(){
  check(thread===null,'Worker already running');
  await rpc.call(0x586fb0,[program.live,program.snapshot,frame.length]);
  const pointer=Buffer.alloc(8);pointer.writeBigUInt64LE(BigInt(program.live));await rpc.write(program.selector,pointer);
  check((await rpc.callAddress(sdk+code.create.offset,[base,0,sdk+code.longjmp.offset,program.live])).result===0n,'pthread_create failed');
  thread=(await rpc.read(base,8)).readBigUInt64LE();report.thread=String(thread);report.threadRunning=true;lastHeartbeat=0n;frameRate.reset();await save();
 }
 async function stopWorker(){
  if(thread===null)return;await program.stop();const until=Date.now()+10000;let done=false;
  while(Date.now()<until){const result=(await rpc.callAddress(sdk+code.tryWait.offset,[thread])).result;check(result===0n||result===1n,'Bad thread state');if(result===1n){done=true;break;}await new Promise(r=>setTimeout(r,10));}
  check(done,'Continuous worker stop timed out; resources retained');check((await rpc.callAddress(sdk+code.join.offset,[thread,base+8])).result===0n,'Join failed');thread=null;report.threadRunning=false;
 }
 const nativeDisplayHandoff=require('../../../core/console/native/display-handoff.cjs').create({directory,session:rpc.a.session,stopWorker,startWorker,report,save});
 report.displayHandoffVersion=1;
 let screen,offline=false,servicing=false;
 try{
  await startWorker();console.log('IZ launcher continuous native frame loop running.');
  screen=resident?{pollInterval:async()=>1000,setVideoStorage(){},onState(){},close:async()=>{}}:await require('../../../core/session-hooks.cjs').create({context:{rpc,mapped:prepared.controls.mapped,directory,controllerInventory:()=>sdkRuntime.hardware.controllers}});screen.setVideoStorage(prepared.streamStorage);
  while(!await stopping()){
   if(await nativeDisplayHandoff())continue;
   await sdkRuntime.hardware.refreshControllers?.();
   const pollInterval=await screen.pollInterval();await new Promise(r=>setTimeout(r,pollInterval));
   const spans=[{address:prepared.controls.mapped,length:1024},{address:prepared.controls.mapped+0x7f0800,length:2052},{address:program.addresses.at(-1),length:8},{address:program.results,length:logicalCalls.length*8}];
   await diagnostics.stage('read-console-state',{spans:spans.length,bytes:spans.reduce((n,s)=>n+s.length,0)});
   const readings=rpc.readMany?await rpc.readMany(spans):null;
   const state=readings?.[0]??await rpc.read(spans[0].address,1024),mailbox=readings?.[1]??await rpc.read(spans[1].address,2052);
   const residentState=resident?await resident.observe():null;
   if(!resident)await diagnostics.observe(state,packageManager.report.available);
   screen?.onState(state);
   const heartbeat=(readings?.[2]??await rpc.read(program.addresses.at(-1),8)).readBigUInt64LE();
   if(heartbeat&&heartbeat!==lastHeartbeat){
    const raw=readings?.[3]??await rpc.read(program.results,logicalCalls.length*8),failures=checks.filter(i=>raw.readBigUInt64LE(i*8)!==0n).map(i=>({call:i,result:String(raw.readBigUInt64LE(i*8))}));
    check(!failures.length,'Continuous graphics call failed: '+JSON.stringify(failures));
    const sample=frameRate.sample(residentState?.frames??state.readUInt32LE(88),performance.now()),interval=sample?.interval||null;
    report.batches.push({status:'continuous',counter:String(heartbeat),interval,frames:sample?.frames??0,frameMetric:sample?.metric||'warming up GPU counter',failures:[],at:new Date().toISOString()});if(report.batches.length>300)report.batches.shift();lastHeartbeat=heartbeat;if(!resident)await telemetry.sample();
    if(resident)packageManager.report.memory=resident.memory.status();
    await packageManager.sample(report.batches.at(-1));
    if(interval)console.log('IZ launcher continuous: '+sample.frames+' frames / '+interval.toFixed(3)+'s');
   }
   if(resident){
    if(await resident.audio.pending()){
     servicing=true;await diagnostics.stage('resident-audio-startup-or-close',{},true);
     await stopWorker();report.resident.audioState=await resident.audio.atBoundary({workerStopped:true});
     report.resident.audio=report.resident.audioState.ready;await save();
     await startWorker();servicing=false;
     console.log('Resident audio: '+(report.resident.audio?'finite queue started; no recurring host audio calls':'startup/close boundary completed'));
    }
    if(residentState.slot===0&&await packageManager.pending(await rpc.read(resident.menu.controls.mapped,1024))){
     servicing=true;await diagnostics.stage('resident-ram-admission',{},true);await stopWorker();
     if((await resident.observe()).slot===0){
      await packageManager.perform(await rpc.read(resident.menu.controls.mapped,1024));
      await runtime.afterBatch();await packageManager.preparingRAM();await resident.refresh(runtime.report.catalog,{workerStopped:true});
      await packageManager.atBoundary();prepared.commands=resident.commands;
      for(let phase=0;phase<prepared.commands.length;phase++){
       const handles=Buffer.alloc(prepared.commands[phase].length*8);prepared.commands[phase].forEach((v,i)=>handles.writeBigUInt64LE(BigInt(v),i*8));await rpc.write(control+0x1060+phase*0x100,handles);
      }
      report.resident.memory=resident.memory.status();await save();console.log('Resident RAM: '+report.resident.memory.apps.length+' apps retained, '+report.resident.memory.usedBytes+' / '+report.resident.memory.budgetBytes+' buffer bytes');
     }
     await startWorker();servicing=false;
    }
    report.residentState=residentState;await telemetry.sample();
    runtime.report.resident=residentState;
    runtime.report.active=residentState.slot===0?null:{id:residentState.id,resident:true};
    await residentAppTelemetry.sample();
    continue;
   }
   const refresh=state.readFloatLE(104),connected=!!(state.readUInt32LE(32)&1);
   const appRequest=runtime.active?state.readUInt32LE(1016)!==0:state.readUInt32LE(1012)!==0||refresh!==lastRefresh;
   const serviceRequest=mailbox.readUInt32LE()!==0&&mailbox.readUInt32LE()!==mailbox.readUInt32LE(2048);
   const connectionChanged=lastConnected!==undefined&&connected!==lastConnected;lastConnected=connected;
   await diagnostics.stage('check-package-queue');
   const packageRequest=await packageManager.pending(state);
   const hostRequest=!appRequest&&await runtime.pending(state);
   if(!appRequest&&!serviceRequest&&!connectionChanged&&!packageRequest&&!hostRequest)continue;
   if(appRequest||packageRequest){screen?.setAvailable(false);await screen?.suspend();}
   // Only explicit app/service transitions or controller reconnects pause rendering.
   servicing=true;
   await diagnostics.stage('stop-render-worker',{appRequest,serviceRequest,connectionChanged,packageRequest,hostRequest},true);
   await stopWorker();await diagnostics.stage('sdk-boundary',{},true);await sdkRuntime.atBoundary(runtime.active?.app);
   await diagnostics.stage('load-package',{},true);await packageManager.perform(state);
   await diagnostics.stage('publish-app-catalog',{},true);const previousApp=runtime.active?.app;await runtime.afterBatch();
   await diagnostics.stage('publish-menu',{},true);await packageManager.atBoundary();await screen.refresh();screen?.setVideoStorage(prepared.streamStorage);
   const commands=runtime.active?runtime.active.gpu.commands[0]:prepared.commands[0];
   if(runtime.active?.app!==previousApp)await sdkRuntime.atBoundary(runtime.active?.app);
   check(commands.length===r.images.length,'App swapchain command count differs');const handles=Buffer.alloc(commands.length*8);commands.forEach((v,i)=>handles.writeBigUInt64LE(BigInt(v),i*8));await rpc.write(control+0x1060,handles);
   await diagnostics.stage('restart-render-worker',{},true);lastRefresh=(await rpc.read(prepared.controls.mapped+104,4)).readFloatLE();await prepareFrame();await startWorker();
   screen?.setAvailable(!runtime.active);
   servicing=false;
  }
 }catch(error){
  if(await offlinePolicy.canRetainRender({error,workerRunning:thread!==null,servicing,stopping:stop,directory})){
   offline=true;report.offline={at:new Date().toISOString(),reason:error.message,workerWasRunning:true,executionUnverified:true};
   await save();await offlinePolicy.retain(directory,error.message,{owner:'graphics',workerWasRunning:true,app:runtime.active?.app?.id||null});
   await diagnostics.stage('offline-retained',{execution:'unverified',automaticReconnect:false},true);
   console.log('IZ launcher offline: render resources retained; waiting for STOP.');
   await offlinePolicy.waitForStop(stopping);
  }else{primaryError=error;await diagnostics.fail(error);throw error;}
 }finally{if(!offline)await cleanup(primaryError,[['screen',()=>screen?.close()],['worker',()=>stopWorker()],['resident-audio',()=>resident?.audio.close({workerStopped:thread===null})],['packages',()=>packageManager.close()]],diagnostics.cleanup);}
 }catch(error){primaryError||=error;await diagnostics.fail(primaryError);throw primaryError;}finally{if(!report.threadRunning)await cleanup(primaryError,[['sdk',()=>sdkRuntime?.close()],['data/app-links',()=>runtime?.close()]],diagnostics.cleanup);}
 await diagnostics.complete();report.completed=new Date().toISOString();await save();console.log('IZ launcher stopped; resources retained.');
}
if(require.main===module)main(process.argv[2],process.argv[3],process.argv[4]).catch(e=>{console.error(e);process.exitCode=1;});

module.exports = {main};
