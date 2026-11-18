// RGB565 double buffer in launcher-owned mapped storage. No native allocation,
// app switch, decoder, or host desktop permission is needed for the color bars.
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),crypto=require('node:crypto');
const SLOT=0x610000,STRIDE=0x78000,HEADER=0x700c80,MAX=1920*1080/2;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function createGamepad(...args){
 const provider=await require(require('node:path').join(require('./lib/platform.cjs').root,'core/components.cjs')).provider('controller-input',{optional:true});
 if(!provider)throw Error('Install the Controllers module before enabling gamepad input. Video streaming remains available with Controller off.');
 return provider.create(...args);
}
function dimensions(w,h,format=0){
 const valid=format===0?w>=2&&w<=640&&h>=2&&h<=360&&w%2===0:format===2?w>=4&&w<=1280&&h>=4&&h<=720&&w%4===0&&h%4===0&&1024+w*h<=STRIDE:format===1&&w>=4&&w<=1920&&h>=4&&h<=1080&&w%4===0&&h%4===0;
 if(!Number.isInteger(w)||!Number.isInteger(h)||!valid)throw Error(`Invalid frame dimensions: ${w} × ${h}, format ${format}`);
}
function frameBytes(w,h,format=1){dimensions(w,h,format);return format===2?1024+w*h:format===1?w*h/2:w*h*2;}
function formatName(format){return ['RGB565-LE','BC1-opaque','Indexed8-lossless'][format];}
function changed(before,after){
 if(!before||before.length!==after.length)return [0,after.length];
 let first=0;while(first<after.length&&before.readUInt32LE(first)===after.readUInt32LE(first))first+=4;
 if(first===after.length)return null;
 let end=after.length;while(end>first&&before.readUInt32LE(end-4)===after.readUInt32LE(end-4))end-=4;
 return [Math.floor(first/4096)*4096,Math.min(after.length,Math.ceil(end/4096)*4096)];
}
function bars(w=480,h=270){
 dimensions(w,h);const b=Buffer.alloc(w*h*2),colors=[0xffff,0xffe0,0x07ff,0x07e0,0xf81f,0xf800,0x001f,0];
 for(let y=0;y<h;y++)for(let x=0;x<w;x++)b.writeUInt16LE(colors[Math.min(7,Math.floor(x*8/w))],(y*w+x)*2);return b;
}
function uploader({rpc,mapped,video={base:mapped+SLOT,stride:STRIDE,capacity:STRIDE},save=async()=>{},onFreshness=async()=>{}}){
 let visibilityEpoch=0;const slots=[null,null];let pub=0,active=0,busy=false,failure=null,initialized=false,last=null,currentFormat=0,lastSave=0;
 const stats={format:'RGB565-LE',frames:0,unchanged:0,uploadedBytes:0,acknowledged:0,started:new Date().toISOString()};
 async function push(bytes,w,h,format=0,metrics){
  dimensions(w,h,format);if(frameBytes(w,h,format)>video.capacity)throw Error('Frame exceeds video storage');if(!Buffer.isBuffer(bytes)||bytes.length!==frameBytes(w,h,format))throw Error('Invalid frame length');
  if(busy)throw Error('Frame upload already in progress');if(failure)throw Error(failure);busy=true;
  try{
   const ownEpoch=visibilityEpoch;
   if(!initialized){const header=await rpc.read(mapped+HEADER,32);pub=header.readUInt32LE(0);active=header.readUInt32LE(12)&1;currentFormat=header.readUInt32LE(24);initialized=true;}
   stats.requests=(stats.requests||0)+1;stats.lastInputAt=new Date().toISOString();
   if(metrics)await rpc.write(mapped+HEADER+104,metrics);
   if(last&&last.w===w&&last.h===h&&last.format===format&&last.bytes.equals(bytes)){
    stats.unchanged++;if(Date.now()-lastSave>1000){await save(stats);lastSave=Date.now();}return {skipped:true,...stats};
   }
   const slot=active^1,range=changed(slots[slot],bytes),start=performance.now();
   if(range)await rpc.write(video.base+slot*video.stride+range[0],bytes.subarray(...range));
   if(!last||last.w!==w||last.h!==h){
    const dims=Buffer.alloc(8);dims.writeUInt32LE(w);dims.writeUInt32LE(h,4);await rpc.write(mapped+HEADER+4,dims);
   }
   if(currentFormat!==format){const f=Buffer.alloc(4);f.writeUInt32LE(format);await rpc.write(mapped+HEADER+24,f);}
   pub=((((pub>>>1)+1)&0x7fffffff)<<1|slot)>>>0;if(pub===0)pub=2;
   const commit=Buffer.alloc(4);commit.writeUInt32LE(pub);await rpc.write(mapped+HEADER,commit);
   const deadline=Date.now()+2500;let ack;
   do{ack=(await rpc.read(mapped+HEADER+12,4)).readUInt32LE();if(ack===pub)break;await sleep(8);}while(Date.now()<deadline);
   if(ack!==pub)throw Error('Frame acknowledgement timed out; uploads stopped to protect the active buffer');
   slots[slot]=Buffer.from(bytes);last={w,h,format,bytes:slots[slot]};active=slot;currentFormat=format;
   if(ownEpoch===visibilityEpoch)await onFreshness(true,'fresh-frame-acknowledged');
   stats.frames++;stats.acknowledged=pub;stats.uploadedBytes+=range?range[1]-range[0]:0;
   stats.width=w;stats.height=h;stats.format=formatName(format);stats.lastMs=+(performance.now()-start).toFixed(2);stats.lastAt=new Date().toISOString();
   stats.lastBytes=range?range[1]-range[0]:0;await save(stats);lastSave=Date.now();return {...stats};
  }catch(error){failure=error.message;stats.error=failure;await save(stats);throw error;}
  finally{busy=false;}
 }
 return {push,stats,invalidate:async reason=>{visibilityEpoch++;last=null;await onFreshness(false,reason);},close:async()=>{visibilityEpoch++;await onFreshness(false,'stream-stopped');}};
}
// A fresh ACK authorizes an echo, never a timer. Source/route changes cancel
// pending reveals before either host-ready or generation echo can be published.
function decodeConsoleDisplay(flags){
 if(!(flags&0x10000000))return null;
 const state={visible:!!(flags&0x01000000),sensorReady:!!(flags&0x02000000),routeReady:!!(flags&0x04000000),hostReady:!!(flags&0x08000000),generation:(flags>>>8)&255};
 state.reason=state.visible?'visible':!state.sensorReady?'waiting-for-console-state':!state.routeReady?'waiting-for-network':!state.hostReady?'waiting-for-fresh-frame':'waiting-for-display-confirmation';return state;
}
function createDisplayFreshness({rpc,mapped,header=HEADER,enabled=true,allowed=()=>true,context=()=>'',freshSource=()=>true,observedGeneration=()=>null,onGenerationChange=()=>{},onVisible=()=>{},now=Date.now}){
 let epoch=0,visible=null,barrier=Promise.resolve(),echo=null,lastAck=-Infinity,lastHeartbeat=-Infinity,heartbeat=0;
 const word=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n>>>0);return b;},own=(revision,state)=>revision===epoch&&allowed()&&context()===state;
 function queue(task){const next=barrier.then(task);barrier=next.catch(()=>{});return next;}
 function gate(requested,reason){
  const revision=requested?epoch:++epoch,state=context(),at=now(),gap=at-lastAck;if(requested)lastAck=at;
  return queue(async()=>{
   if(!requested){if(visible!==false)await rpc.write(mapped+header+164,word(0));visible=false;onVisible(false,reason);return false;}
   if(!own(revision,state)||!freshSource())return false;
   if(enabled){const observed=observedGeneration(),changed=Number.isInteger(observed)&&echo!==null&&(echo&255)!==observed;
    if(visible!==true||echo===null||gap>400||changed){const generation=(await rpc.read(mapped+header+280,4)).readUInt32LE();if(!own(revision,state)||!freshSource())return false;if(!generation)return false;
     if(Number.isInteger(observed)&&(generation&255)!==observed){onGenerationChange(generation&255);return false;}
     if(generation!==echo){await rpc.write(mapped+header+292,word(generation));if(!own(revision,state)||!freshSource())return false;echo=generation;}
    }
   }
   if(!own(revision,state)||!freshSource())return false;if(visible!==true)await rpc.write(mapped+header+164,word(1));if(!own(revision,state)||!freshSource())return false;visible=true;onVisible(true,reason);return true;
  });
 }
 function beat(){const revision=epoch,state=context(),at=now();return queue(async()=>{if(!enabled||visible!==true||!own(revision,state)||!freshSource()||at-lastHeartbeat<200)return false;heartbeat=(heartbeat+1)>>>0||1;await rpc.write(mapped+header+296,word(heartbeat));lastHeartbeat=at;return true;});}
 return {gate,heartbeat:beat,drain:()=>barrier,get visible(){return visible;}};
}
async function create({rpc,mapped,directory,persistRenderSettings=false,renderSettings=null,panelLabel=null,port=0,transport=process.env.HB_STREAM_TRANSPORT||'tcp',preview=false,audioFactory=require('./lib/stream-audio.cjs').create,streamFactory,cropWindow=require('./lib/window-crop.cjs').crop,listNativeWindows=require('./lib/native-window-capture.cjs').list,selectNativeWindow=require('./lib/native-window-capture.cjs').select,captureNativeWindow=require('./lib/native-window-capture.cjs').create,focusWindow=require('./lib/window-focus.cjs').focus,gamepadFactory=createGamepad,launchHl2,captureHl2,launchGtav,captureGtav,createRangersController,launchRangers,captureRangers,startJellymoped,createGtavOverlay,startHl2,createYoutube,playerIntegration=null,startYoutubeInput=require('./lib/youtube-input-sampler.cjs').start,controllerInventory=null,createControllerHub=async options=>(await require(require('node:path').join(require('./lib/platform.cjs').root,'core/components.cjs')).provider('controller-hub',{optional:true}))?.create(options),openWindow=url=>require(require('node:path').join(require('./lib/platform.cjs').root,"core/services/open-panel.cjs")).openPanel(url,{env:{...process.env,HB_OPEN_PANEL:'1'}})}){
 if(!Number.isInteger(port)||port!==0&&port<1024||port>65535)throw Error('Invalid capture port');
 const moduleServers=new Map();
 async function component(id,name){
  let server=moduleServers.get(id);
  if(!server){server=require(require('node:path').join(require('./lib/platform.cjs').root,"core/modules.cjs")).loadServer(path.resolve(require('./lib/platform.cjs').root,"."),id);moduleServers.set(id,server);}
  return (await server).load(name);
 }
 launchHl2??=async(...args)=>(await component('half-life-2','launch')).launch(...args);
 captureHl2??=async(...args)=>(await component('half-life-2','capture')).create(...args);
 launchGtav??=async(...args)=>(await component('grand-theft-auto-5','launch')).launch(...args);
 captureGtav??=async(...args)=>(await component('grand-theft-auto-5','capture')).create(...args);
 createGtavOverlay??=async()=>(await component('grand-theft-auto-5','overlay')).create();
 launchRangers??=async(...args)=>(await component('space-rangers','launch')).launch(...args);
 captureRangers??=async(...args)=>(await component('space-rangers','capture')).create(...args);
 createRangersController??=async(...args)=>(await component('space-rangers','controller')).create(...args);
 createYoutube??=async(...args)=>{if(!playerIntegration)throw Error('No embedded player integration is configured');return playerIntegration.create(...args);};
 const key=crypto.randomBytes(24).toString('hex');let closing=false,closePromise,optionQueue=Promise.resolve();
 // JellyMoped handoff integration. One BlitBun host retains the console stream.
 startJellymoped??=async(...args)=>(await component('jellymoped','launch')).start(...args);
 let jellymoped=null,jellyActive=false,jellyStarting=null,jellyRevision=0;
 async function stopJellymoped(){jellyActive=false;jellyRevision++;const owned=jellymoped;jellymoped=null;await owned?.close();}
 async function launchJellymoped(){
  await stopJellymoped();if(closing)return;
  jellyActive=true;const revision=++jellyRevision;
  const current=()=>!closing&&jellyActive&&revision===jellyRevision;
  try{
   const owned=await startJellymoped(directory,{
    isActive:async()=>current()&&(!rpc.read||(await rpc.read(mapped+0x700004,4)).readUInt32LE()===2),
    onWindow:async(window,selection)=>{
     if(!current()||selection.signal?.aborted)return;
     await changeOptions({...options,source:'native-window',windowId:window.id,controller:'gamepad',stats:true},window);
     if(current()){stream.stats.jellymoped={state:selection.kind,appId:selection.appId,title:window.title};gameFocus.arm();emit();}
    },
    onWaiting:reason=>{
     const task=optionQueue.catch(()=>{}).then(async()=>{if(!current())return;await stream.invalidate?.(reason);await stopPump();stream.stats.jellymoped={state:reason};emit();});optionQueue=task;return task;
    }
   });
   if(current())jellymoped=owned;else await owned.close();
  }catch(error){jellyActive=false;throw error;}
 }

 if(!['rpc','tcp'].includes(transport))throw Error('HB_STREAM_TRANSPORT must be rpc or tcp');
 const factory=streamFactory||(transport==='tcp'?require(path.join(__dirname,'lib',process.env.HB_VIDEO_CODEC==='legacy'?'pc-stream-tcp.cjs':'pc-stream-delta.cjs')).uploader:uploader);
 const video={base:mapped+SLOT,stride:STRIDE,capacity:STRIDE};
 let options={stretch:false,controller:'off',stats:false,audioOutput:'auto',resolution:'auto',dockResolution:'1920',handheldResolution:'1280',fps:60,shareSound:true,compressionPolicy:'auto',renderMode:'auto',allowDockWifi:false},remote,pad,padProfile=null,source='browser',pump,audioSink,audioEnabled=false,browserPending,lastCommand=0,captureTarget=null,gameProfile=null,nativeGame=null,gtavOverlay=null,youtubeInput=null,youtubeStopping=false;const panels=new Set();await rpc.write(mapped+HEADER+96,Buffer.from([0,0,0,0]));
 const profileStore=renderSettings||(persistRenderSettings?await require('./lib/render-profile.cjs').settings(require('./lib/platform.cjs').root):null);if(profileStore)Object.assign(options,profileStore.get());const initialDockPolicy=Buffer.alloc(4);initialDockPolicy.writeUInt32LE(options.allowDockWifi?1:0);await rpc.write(mapped+HEADER+336,initialDockPolicy);
 const initialProfiles=Buffer.alloc(12);[options.resolution==='auto'?0:Number(options.resolution),Number(options.dockResolution),Number(options.handheldResolution)].forEach((value,i)=>initialProfiles.writeUInt32LE(value,i*4));await rpc.write(mapped+HEADER+168,initialProfiles);
 let controllerHub=null,displayVisible=null,notifyProfile=()=>{},routeGate=null,stream,gpuGeneration=null;
 const displayFreshness=createDisplayFreshness({rpc,mapped,observedGeneration:()=>gpuGeneration,onGenerationChange:noteGpuGeneration,enabled:false,allowed:()=>!closing&&(!routeGate||routeGate.allowNow()),context:()=>JSON.stringify([source,nativeGame?.report?.sourceEpoch??null,routeGate?.snapshot().generation??0]),freshSource:()=>!nativeGame?.report?.closed&&!nativeGame?.report?.sourceHidden&&!nativeGame?.report?.captureSuspended&&!nativeGame?.report?.routePaused&&!nativeGame?.report?.reconfiguring,onVisible:visible=>{displayVisible=visible;if(visible&&source==='grand-theft-auto-5'&&options.stats)gtavOverlay?.show();}});
 async function displayGate(visible,reason){return displayFreshness.gate(!!visible,reason);}

 await displayGate(false,'initializing');
 const browserPcm=new(require(require('node:path').join(require('./lib/platform.cjs').root,"core/services/pcm-queue.cjs")).PcmQueue)();let browserAudioId=null;
 const gameFocus=require(require('node:path').join(require('./lib/platform.cjs').root,"core/services/controller-focus.cjs")).create({target:()=>['space-rangers','half-life-2','grand-theft-auto-5','native-window'].includes(source)&&options.controller!=='off'?nativeGame:null,onResult:result=>{stream.stats.controllerFocus={...result,at:new Date().toISOString()};emit();}});
 async function newGamepad(){const profile=source==='space-rangers'?'space-rangers':source==='grand-theft-auto-5'?'pro-controller':undefined;const created=source==='space-rangers'?await createRangersController(directory,{target:()=>nativeGame?.report?.window}):await gamepadFactory(directory,{profile});padProfile=profile??null;return created;}
 function noteGpuGeneration(generation){
  if(!Number.isInteger(generation)||generation<0||generation>255)return;
  if(gpuGeneration===null){gpuGeneration=generation;return;}
  const delta=(generation-gpuGeneration)&255;if(!delta||delta>=128)return;gpuGeneration=generation;
  const tasks=[displayGate(false,'console-gpu-freshness-generation-changed'),nativeGame?.pauseCapture?.('console-gpu-freshness-generation-changed'),stream?.invalidate?.('console-gpu-freshness-generation-changed')];
  Promise.all(tasks).catch(error=>{if(stream)stream.stats.freshnessError=error.message;});
 }
 function dispatchInput(packet){
  if(packet.length>=336){const flags=packet.readUInt32LE(332),display=decodeConsoleDisplay(flags);if(display&&stream){stream.stats.consoleDisplay={...display,at:new Date().toISOString()};stream.stats.consoleVideoVisible=display.visible;}if(flags&0x80000000)noteGpuGeneration((flags>>>8)&255);}
  if(controllerHub)packet=controllerHub.filter(packet);
  audioEnabled=packet.readUInt32LE(328)===2;
  if(routeGate&&!routeGate.allowNow()){const neutral=Buffer.alloc(396);remote?.input(neutral);pad?.input(neutral);nativeGame?.release?.();return;}
  if(source==='doom')remote?.input(packet);
  if(source==='player')nativeGame?.input(packet);else pad?.input(packet);
  if(pad?.report?.last?.sourceChanged)gameFocus.arm();gameFocus.input(pad?.report?.last);
 }
 function ensureInput(){
  const needed=!preview&&(source==='player'||options.controller!=='off');
  if(!needed){youtubeInput?.close();youtubeInput=null;return;}
  if(youtubeInput||!rpc.readMany&&!rpc.read&&startYoutubeInput===require('./lib/youtube-input-sampler.cjs').start)return;
  youtubeInput=startYoutubeInput({rpc,mapped,header:HEADER,onInput:dispatchInput,onError:error=>{
   const neutral=Buffer.alloc(396);remote?.input(neutral);pad?.input(neutral);nativeGame?.release?.();stream.stats.inputError=error.message;
  }});
 }
 // Readiness is observed once, not polled on the native RPC/audio queue.
 const consoleMonitor=await require('./lib/stream-monitor.cjs').create({rpc,directory,preview});
 routeGate=require('./lib/route-gate.cjs').create({enabled:false});
 const initialRoute=Buffer.alloc(4);await rpc.write(mapped+HEADER+252,initialRoute);
 const hl2ProfileFor=require('./lib/render-profile.cjs').resolver({consoleState:()=>consoleMonitor?.state,selection:()=>options});
 const makeStream=()=>{const created=factory({rpc,mapped,video,directory,consoleState:()=>consoleMonitor?.state,onFreshness:displayGate,onHeartbeat:async()=>true,compressionPolicy:()=>options.compressionPolicy,renderMode:()=>options.renderMode,onInput:packet=>{consoleMonitor.observe(packet,stream?.consoleAddress?.());if(youtubeInput?.noteAck)youtubeInput.noteAck(packet);else if(!youtubeInput)dispatchInput(packet);if(source==='player'&&packet.readUInt32LE(328)!==2&&!youtubeStopping&&!closing){youtubeStopping=true;changeOptions({...options,source:'browser',controller:'off'}).catch(error=>{stream.stats.sourceError=error.message;stream.stats.sourceErrorStack=error.stack;}).finally(()=>youtubeStopping=false);}},onPause:paused=>{youtubeInput?.pause(paused);if(paused){const neutral=Buffer.alloc(396);remote?.input(neutral);pad?.input(neutral);if(source==='player')nativeGame?.release();}},controlsEnabled:()=>source==='player'||options.controller!=='off',keepAlive:()=>source==='grand-theft-auto-5'&&options.stats,save:stats=>fs.writeFile(path.join(directory,'pc-screen-stats.json'),JSON.stringify(stats,null,2))});
  for(const method of ['prepare','push','prepareFrame'])if(typeof created[method]==='function'){const original=created[method].bind(created);created[method]=async(...args)=>{await routeGate.ready();return original(...args);};}
  if(created.commitPrepared){const original=created.commitPrepared.bind(created);created.commitPrepared=(...args)=>routeGate.allowNow()?original(...args):false;}
  if(created.wirePrefetchReady){const original=created.wirePrefetchReady.bind(created);created.wirePrefetchReady=()=>routeGate.allowNow()&&original();}
  created.waitForRoute=options=>routeGate.wait(options);created.routeGeneration=()=>routeGate.snapshot().generation;created.stats.routeGate=routeGate.snapshot();return created;
 };
 stream=makeStream();stream.stats.options=options;
 if(!preview&&typeof controllerInventory==='function')controllerHub=await createControllerHub({rpc,mapped,directory,inventory:controllerInventory,onChange:async(next,old)=>{
  const neutral=Buffer.alloc(396);remote?.input(neutral);pad?.input(neutral);nativeGame?.release?.();
  const changed=next.profile!==old.profile&&source!=='grand-theft-auto-5';if(changed&&pad){await pad.close();pad=await newGamepad();gameFocus.arm();}
  return {needsRestart:changed&&['space-rangers','half-life-2','grand-theft-auto-5'].includes(source)&&!!nativeGame};
 }});
 let available=true;
 if(preview)stream.stats.preview=true;
 const assets=await Promise.all(['pc-screen.html','pc-codec.js','pc-gpu-codec.js','frame-codec.wasm','pcm-capture-worklet.js','capture-audio.js','frame-pacing.js'].map(name=>fs.readFile(path.join(__dirname,'lib',name))));
 let [page,codec,gpuCodec,wasm,worklet,captureAudio,pacing]=assets;
 if(panelLabel){if(!/^[A-Za-z0-9 ?.-]{1,48}$/.test(panelLabel))throw Error('Invalid panel label');page=Buffer.from(page.toString().replaceAll('BlitBun',panelLabel));}
 const captureHistory=[];let captureSave=Promise.resolve();
 if(preview)page=Buffer.from(page.toString().replace('<h1>BlitBun</h1>','<h1>BlitBun</h1><small>LOCAL TEST / NO CONSOLE</small>'));
 const snapshot=()=>({...stream.stats,routeGate:routeGate.snapshot(),options,source,nativeWindow:source==='native-window'?{title:nativeGame?.report?.window?.title||gameProfile?.name,audioWarning:nativeGame?.report?.audioWarning,frames:nativeGame?.report?.frames}:null,controllerHub:controllerHub?.snapshot(),preview,captureProfile:hl2ProfileFor(stream.consoleAddress?.()),consoleState:consoleMonitor?.state||null,inputSampler:youtubeInput?.report,youtube:source==='player'?{inputError:nativeGame?.report?.inputError||stream.stats.inputError,inputSampler:youtubeInput?.report,audioError:nativeGame?.report?.audioError,ready:nativeGame?.report?.ready,frames:nativeGame?.report?.frames}:null,gameProfile:gameProfile?{...gameProfile,gameResolutionPending:!!nativeGame?.report?.gameResolutionPending,profileWarning:nativeGame?.report?.profileWarning}:null,controller:{source:pad?.report?.source,profile:pad?.report?.profile,buttonLayout:pad?.report?.buttonLayout,enabled:options.controller!=='off',ready:!!pad&&!pad.report?.exited,packets:pad?.report?.packets||0,lastAt:pad?.report?.last?.at,inputActive:!!pad?.report?.last?.enabled,announcement:pad?.report?.announcement??null,error:pad?.report?.error|| (pad?.report?.exited?'Virtual controller disconnected':null)},audio:audioSink?{route:audioSink.report?.route,channels:audioSink.report?.channels,error:audioSink.report?.error,warning:audioSink.report?.warning,playing:audioSink.report?.playing}:null});
 const emit=()=>{const message='data: '+JSON.stringify(snapshot())+'\n\n';for(const res of panels){if(res.destroyed||res.writableEnded){panels.delete(res);continue;}if(res.writableLength>1048576){res.destroy();continue;}res.write(message);}};
 notifyProfile=emit;
 const events=setInterval(emit,1000);events.unref();
 async function stopPump(){pad?.release?.();youtubeInput?.close();youtubeInput=null;browserAudioId=null;browserPcm.clear();await audioSink?.close();audioSink=null;if(pump){const old=pump;pump=null;await old.close();}await nativeGame?.close();nativeGame=null;gtavOverlay?.close();gtavOverlay=null;}
 function browserAudio(start,id){
  const change=optionQueue.catch(()=>{}).then(async()=>{
   if(closing)throw Error('Session is stopping');
   if(!start&&id!==browserAudioId)return {stopped:true};
   if(source!=='browser'){if(start)throw Error('Browser capture is inactive');return {stopped:true};}
   await stopPump();
   if(start){audioSink=await audioFactory({rpc,directory,source:browserPcm,enabled:()=>(preview||audioEnabled)&&routeGate.allowNow(),label:'Capture',routeSignal:()=>consoleMonitor.revision,audioOutput:()=>options.audioOutput});browserAudioId=crypto.randomBytes(8).toString('hex');}
   ensureInput();return {audioId:browserAudioId,stopped:!start};
  });optionQueue=change;return change;
 }
 function changeOptions(next,expectedWindow=null){
  next={...next};if(['space-rangers','half-life-2','grand-theft-auto-5'].includes(next.launch))next.resolution='auto';next.audioOutput??=options.audioOutput;next.resolution??=options.resolution;next.fps??=options.fps;next.dockResolution??=options.dockResolution;next.handheldResolution??=options.handheldResolution;next.shareSound??=options.shareSound;next.compressionPolicy??=options.compressionPolicy;next.renderMode??=options.renderMode;next.allowDockWifi??=options.allowDockWifi;
  if(!['auto','640','832','960','1280','1600','1920'].includes(next.resolution)||!['640','832','960','1280','1600','1920'].includes(next.dockResolution)||!['640','832','960','1280','1600','1920'].includes(next.handheldResolution)||![30,40,60].includes(next.fps)||typeof next.shareSound!=='boolean')throw Error('Invalid render profile');
  if(typeof next.allowDockWifi!=='boolean')throw Error('Invalid dock network policy');
  if(!['auto','on','off'].includes(next.compressionPolicy)||!['auto','direct','ram'].includes(next.renderMode))throw Error('Invalid stream processing settings');
  if(!['auto','hdmi','bluetooth'].includes(next.audioOutput))throw Error('Invalid audio output');
  if(typeof next.stretch!=='boolean'||typeof next.stats!=='boolean'||!['off','gamepad'].includes(next.controller)||next.launch!==undefined&&!['doom','half-life-2','player','grand-theft-auto-5','space-rangers'].includes(next.launch)||next.source!==undefined&&!['browser','native-window','doom','half-life-2','player','grand-theft-auto-5','space-rangers'].includes(next.source))throw Error('Invalid options');
  if(next.windowId!==undefined&&(!Number.isSafeInteger(next.windowId)||next.windowId<=0||next.source!=='native-window'))throw Error('Invalid selected window');
  if(next.launch==='player'){next.controller='gamepad';next.stretch=true;next.stats=false;next.source='player';}
  if(next.launch==='half-life-2'){next.controller='gamepad';next.stretch=true;next.source='half-life-2';}
  if(next.launch==='grand-theft-auto-5'){next.controller='gamepad';next.stretch=true;next.source='grand-theft-auto-5';}
  if(next.launch==='space-rangers'){next.controller='gamepad';next.stretch=true;next.source='space-rangers';}
  if(next.launch==='doom'){next.controller='gamepad';next.stretch=true;next.source='doom';}
  const change=optionQueue.catch(()=>{}).then(async()=>{
   if(closing)throw Error('Session is stopping');
   const selectedWindow=next.windowId!==undefined?await selectNativeWindow(next.windowId):null;if(expectedWindow&&(selectedWindow?.pid!==expectedWindow.pid||selectedWindow.identity?.start!==expectedWindow.identity?.start||selectedWindow.identity?.uid!==expectedWindow.identity?.uid))throw Error('Steam window owner changed during handoff');
   const routePolicyChanged=next.allowDockWifi!==options.allowDockWifi;
   const codecChanged=next.compressionPolicy!==options.compressionPolicy||next.renderMode!==options.renderMode;
   const profileChanged=next.resolution!==options.resolution||next.fps!==options.fps||next.dockResolution!==options.dockResolution||next.handheldResolution!==options.handheldResolution;
   if(profileChanged||codecChanged){await browserPending?.catch(()=>{});if(codecChanged)await nativeGame?.pauseCapture?.('stream-settings-changed');await stream.invalidate?.(codecChanged?'stream-settings-changed':'render-profile-changed');}
   options={...options,allowDockWifi:next.allowDockWifi,compressionPolicy:next.compressionPolicy,renderMode:next.renderMode,resolution:next.resolution,dockResolution:next.dockResolution,handheldResolution:next.handheldResolution,fps:next.fps};
   if(routePolicyChanged){const policy=Buffer.alloc(4);policy.writeUInt32LE(next.allowDockWifi?1:0);await rpc.write(mapped+HEADER+336,policy);await routeGate.refresh();await nativeGame?.pauseCapture?.('dock-network-policy-changed');await stream.invalidate?.('dock-network-policy-changed');}
   if(codecChanged)await stream.refreshSettings?.();
   if(profileChanged){const selected=Buffer.alloc(12);selected.writeUInt32LE(next.resolution==='auto'?0:Number(next.resolution));selected.writeUInt32LE(Number(next.dockResolution),4);selected.writeUInt32LE(Number(next.handheldResolution),8);await rpc.write(mapped+HEADER+168,selected);}
   if(next.source&&next.source!==source){await stream.invalidate?.('source-changed');source=next.source;captureTarget=null;if(source==='browser')gameProfile=null;emit();await browserPending?.catch(()=>{});await stopPump();}
   if(next.controller!==options.controller||(pad&&source!=='player'&&padProfile!==(source==='space-rangers'?'space-rangers':source==='grand-theft-auto-5'?'pro-controller':null))||(next.source==='player'&&pad)||(next.controller!=='off'&&!pad&&source!=='player'&&!preview)){await pad?.close();pad=null;options={...options,controller:'off'};if(next.controller!=='off'&&!preview&&source!=='player')pad=await newGamepad();}
   if(selectedWindow){await stream.invalidate?.('window-selected');await stopPump();captureTarget=selectedWindow.id;gameProfile={name:selectedWindow.title,source:'native-window',...hl2ProfileFor(stream.consoleAddress?.()),sequence:(gameProfile?.sequence||0)+1};if(!preview)nativeGame=await captureNativeWindow(directory,{window:selectedWindow,profile:hl2ProfileFor(stream.consoleAddress?.()),shareSound:next.shareSound,offscreen:jellyActive,drawMouse:jellyActive});}
   if(next.launch==='half-life-2'){const launched=preview?{...(await component('half-life-2','launch')).PROFILE,status:'preview'}:await launchHl2(directory,{profile:hl2ProfileFor(stream.consoleAddress?.())});gameProfile={...launched,sequence:(gameProfile?.sequence||0)+1};if(!preview){await stopPump();nativeGame=await captureHl2(directory,{profile:hl2ProfileFor(stream.consoleAddress?.())});}}
   else if(next.launch==='grand-theft-auto-5'){await stream.invalidate?.('game-launch-started');await displayGate(false,'game-launch-started');await stopPump();const launched=preview?{...(await component('grand-theft-auto-5','launch')).PROFILE,status:'preview'}:await launchGtav(directory,{controllerDevice:pad?.report?.device});gameProfile={...launched,sequence:(gameProfile?.sequence||0)+1};if(!preview){nativeGame=await captureGtav(directory,{profile:hl2ProfileFor(stream.consoleAddress?.())});gtavOverlay=await createGtavOverlay();}}
   else if(next.launch==='space-rangers'){await stream.invalidate?.('game-launch-started');await displayGate(false,'game-launch-started');await stopPump();const launched=preview?{...(await component('space-rangers','launch')).PROFILE,status:'preview'}:await launchRangers(directory);gameProfile={...launched,sequence:(gameProfile?.sequence||0)+1};if(!preview)nativeGame=await captureRangers(directory,{profile:hl2ProfileFor(stream.consoleAddress?.())});}
   else if(next.launch==='player'){await stopPump();if(!preview)nativeGame=await createYoutube(directory);gameProfile={...playerIntegration.package.PROFILE,source:'player',sequence:(gameProfile?.sequence||0)+1};}
   else if(next.launch==='doom')gameProfile=null;
   if(next.launch==='doom'&&!preview&&(!remote||remote.report.exited)){
    await stopPump();await remote?.close();remote=null;
    try{remote=await (await component('doom','create')).create(directory,{capture:true,headless:true,audio:true});}
    catch(error){await pad?.close();pad=null;options={...options,controller:'off'};throw error;}
   }
   if(source==='player')next.stats=false;
   const flags=Buffer.alloc(4);flags.writeUInt32LE((next.stretch?1:0)|(next.controller!=='off'?2:0)|(next.stats?(source==='grand-theft-auto-5'?8:4):0));await rpc.write(mapped+HEADER+96,flags);
   options={stretch:next.stretch,controller:next.controller,stats:next.stats,audioOutput:next.audioOutput,resolution:next.resolution,dockResolution:next.dockResolution,handheldResolution:next.handheldResolution,fps:next.fps,shareSound:next.shareSound,compressionPolicy:next.compressionPolicy,renderMode:next.renderMode,allowDockWifi:next.allowDockWifi};if(profileChanged||codecChanged||routePolicyChanged)await profileStore?.set(options);stream.stats.options=options;stream.stats.source=source;if(source==='native-window')await nativeGame?.setAudioEnabled?.(options.shareSound);gtavOverlay?.setEnabled(options.stats);if(displayVisible&&options.stats)gtavOverlay?.show();
   if(next.launch==='grand-theft-auto-5'&&!preview&&pad?.announce)pad.announce();
   if(['space-rangers','half-life-2','grand-theft-auto-5','native-window'].includes(source)&&options.controller!=='off')gameFocus.arm();
   if(!routeGate.snapshot().ready)await nativeGame?.pauseCapture?.('console-route-pending');
   if(['space-rangers','half-life-2','grand-theft-auto-5','native-window'].includes(source)&&!pump&&!preview&&nativeGame){
    try{audioSink=await audioFactory({rpc,directory,source:nativeGame.audio,enabled:()=>audioEnabled&&options.shareSound&&routeGate.allowNow(),routeSignal:()=>consoleMonitor.revision,label:source==='space-rangers'?'Space Rangers':source==='grand-theft-auto-5'?'GTA V':source==='native-window'?'Window':'Half-Life 2',audioOutput:()=>options.audioOutput});}catch(error){stream.stats.audioError=error.message;}
    delete stream.stats.sourceError;pump=(startHl2||(source==='native-window'?require('./lib/hl2-stream.cjs'):(await component(source,'stream'))).start)({remote:nativeGame,stream,profileFor:hl2ProfileFor,decorateMetrics:metrics=>gtavOverlay?.decorate(metrics),onProfile:profile=>{gameProfile={...gameProfile,...profile,sequence:(gameProfile?.sequence||0)+1};emit();},onError:error=>{stream.stats.sourceError=error.message;stream.stats.sourceErrorStack=error.stack;fs.writeFile(path.join(directory,'pc-screen-stats.json'),JSON.stringify(stream.stats,null,2)).catch(()=>{});emit();}});
   }
   if(source==='player'&&!pump&&!preview&&nativeGame){
    ensureInput();
    try{audioSink=await audioFactory({rpc,directory,source:nativeGame.audio,enabled:()=>audioEnabled&&routeGate.allowNow(),label:'YouTube',routeSignal:()=>consoleMonitor.revision,audioOutput:()=>options.audioOutput});}catch(error){stream.stats.audioError=error.message;}
    const youtubeProfile=playerIntegration.package.PROFILE;
    delete stream.stats.sourceError;pump=(startHl2||playerIntegration.stream.start)({remote:nativeGame,stream,profileFor:()=>youtubeProfile,onError:error=>{stream.stats.sourceError=error.message;stream.stats.sourceErrorStack=error.stack;emit();}});
   }
   if(source==='doom'&&!pump&&!preview&&remote){
    try{audioSink=await audioFactory({rpc,directory,source:remote.audio,enabled:()=>audioEnabled&&routeGate.allowNow(),routeSignal:()=>consoleMonitor.revision,audioOutput:()=>options.audioOutput});}catch(error){console.error('Doom audio: '+error.message);await fs.writeFile(path.join(directory,'stream-audio-error.json'),JSON.stringify({error:error.message,at:new Date().toISOString()}));}
    delete stream.stats.sourceError;pump=(await component('doom','stream')).start({remote,stream,onError:error=>{stream.stats.sourceError=error.message;stream.stats.sourceErrorStack=error.stack;console.error('Doom stream: '+error.message);fs.writeFile(path.join(directory,'pc-screen-stats.json'),JSON.stringify(stream.stats,null,2)).catch(()=>{});emit();}});}
   ensureInput();emit();return {...options,source,preview,gameProfile};
  });optionQueue=change;return change;
 }
 const server=http.createServer(async(req,res)=>{
  const reply=(status,data)=>{res.writeHead(status,{'content-type':'application/json','cache-control':'no-store'});res.end(JSON.stringify(data));};
  try{
   if(req.headers.host!==`127.0.0.1:${port}`)return reply(403,{error:'Use the local capture address'});
   if(req.method==='GET'&&req.url==='/'){res.writeHead(200,{'content-type':'text/html; charset=utf-8','cache-control':'no-store','content-security-policy':"default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; worker-src blob:; style-src 'unsafe-inline'; media-src blob:; connect-src 'self'; frame-ancestors 'none'"});return res.end(page);}
   if(req.method==='GET'&&req.url==='/pc-gpu-codec.js'){res.writeHead(200,{'content-type':'text/javascript; charset=utf-8','cache-control':'no-store'});return res.end(gpuCodec);}
   if(req.method==='GET'&&req.url==='/pc-codec.js'){res.writeHead(200,{'content-type':'text/javascript; charset=utf-8','cache-control':'no-store'});return res.end(codec);}
   if(req.method==='GET'&&req.url==='/pcm-capture-worklet.js'){res.writeHead(200,{'content-type':'text/javascript; charset=utf-8','cache-control':'no-store'});return res.end(worklet);}
   if(req.method==='GET'&&req.url==='/capture-audio.js'){res.writeHead(200,{'content-type':'text/javascript; charset=utf-8','cache-control':'no-store'});return res.end(captureAudio);}
   if(req.method==='GET'&&req.url==='/frame-pacing.js'){res.writeHead(200,{'content-type':'text/javascript; charset=utf-8','cache-control':'no-store'});return res.end(pacing);}
   if(req.method==='GET'&&req.url==='/frame-codec.wasm'){res.writeHead(200,{'content-type':'application/wasm','cache-control':'no-store'});return res.end(wasm);}
   if(req.method==='GET'&&req.url==='/native-windows'&&req.headers['x-stream-key']===key){return reply(200,await listNativeWindows());}
   if(req.method==='GET'&&req.url==='/status')return reply(200,snapshot());
   if(req.method==='GET'&&req.url==='/events?key='+key){res.writeHead(200,{'content-type':'text/event-stream','cache-control':'no-store','connection':'keep-alive'});panels.add(res);res.on('close',()=>panels.delete(res));res.on('error',()=>panels.delete(res));res.write('data: '+JSON.stringify(snapshot())+'\n\n');return;}
   if(req.method==='GET'&&req.url==='/stream-preview'&&req.headers['x-stream-key']===key){const frame=source==='doom'?remote?.video.latest:nativeGame?.video?.latest,bytes=require('./lib/stream-preview.cjs').thumbnail(frame);if(!bytes)return reply(404,{error:'No preview frame'});res.writeHead(200,{'content-type':'application/octet-stream','cache-control':'no-store'});return res.end(bytes);}
   if(req.method==='GET'&&req.url==='/doom-preview'&&req.headers['x-stream-key']===key){const f=remote?.video.latest;if(!f)return reply(404,{error:'No game frame'});res.writeHead(200,{'content-type':'application/octet-stream','cache-control':'no-store','x-width':f.w,'x-height':f.h});return res.end(f.rgba);}
   if(req.method==='GET'&&req.url.startsWith('/crop?')&&req.headers['x-stream-key']===key){const u=new URL(req.url,'http://127.0.0.1');const result=await cropWindow(Number(u.searchParams.get('w')),Number(u.searchParams.get('h')),u.searchParams.get('label')||'');captureTarget=result.windowId||null;return reply(200,result);}
   if(req.method!=='POST'||req.headers['x-stream-key']!==key||req.headers.origin!==`http://127.0.0.1:${port}`)return reply(403,{error:'Open the capture link from this run'});
   if(req.url==='/capture-state'){
    const parts=[];let n=0;for await(const part of req){n+=part.length;if(n>2048)throw Error('Capture report too large');parts.push(part);}
    const row=JSON.parse(Buffer.concat(parts));if(!['options','source-picker','source-selected','video-ready','preparing','encoder-ready','first-frame-acknowledged','audio-error','failed','stopped'].includes(row.phase))throw Error('Invalid capture phase');
    const report={at:new Date().toISOString(),phase:row.phase,...(Number.isInteger(row.width)&&row.width>=0&&row.width<=16384&&Number.isInteger(row.height)&&row.height>=0&&row.height<=16384?{width:row.width,height:row.height}:{}),...(typeof row.error==='string'?{error:row.error.slice(0,300).replace(/https?:\/\/\S+/g,'[address]')}:{})};captureHistory.push(report);if(captureHistory.length>32)captureHistory.shift();
    const serialized=JSON.stringify({latest:report,history:captureHistory},null,2);captureSave=captureSave.catch(()=>{}).then(()=>fs.writeFile(path.join(directory,'pc-screen-capture.json'),serialized));await captureSave;return reply(200,{recorded:true});
   }
   if(closing)return reply(503,{error:'Session is stopping'});
   if(!available)return reply(409,{error:'Return to the menu before starting PC Screen'});
   if(req.url==='/focus'){if(options.controller==='off')throw Error('Enable Controller first');if(source==='native-window')return reply(200,preview?{focused:true,preview:true}:await nativeGame.focus());if(['space-rangers','half-life-2','grand-theft-auto-5'].includes(source))return reply(200,preview?{focused:true,preview:true}:await gameFocus.focus());if(source!=='browser'||!captureTarget)throw Error('Share a game window first');return reply(200,preview?{focused:true,preview:true}:await focusWindow(captureTarget));}
   if(req.url==='/audio/start')return reply(200,await browserAudio(true));
   if(req.url==='/audio/stop')return reply(200,await browserAudio(false,req.headers['x-audio-id']));
   if(req.url==='/audio'){
    const id=req.headers['x-audio-id'];if(source!=='browser'||!browserAudioId||id!==browserAudioId)return reply(409,{error:'Capture audio is inactive'});
    if(audioSink?.report?.error)throw Error('Capture audio: '+audioSink.report.error);
    const parts=[];let n=0;for await(const part of req){n+=part.length;if(n>19200)throw Error('PCM packet too large');parts.push(part);}
    if(!n||n%4)throw Error('Invalid stereo PCM packet');
    if(source!=='browser'||id!==browserAudioId)return reply(409,{error:'Capture audio changed'});
    browserPcm.push(Buffer.concat(parts));return reply(200,{received:n});
   }
   if(req.url==='/native-window'){const parts=[];let n=0;for await(const part of req){n+=part.length;if(n>128)throw Error('Selection too large');parts.push(part);}const selected=JSON.parse(Buffer.concat(parts));return reply(200,await changeOptions({...options,source:'native-window',windowId:selected.id,shareSound:selected.shareSound??options.shareSound}));}
   if(req.url==='/options'){
    const parts=[];let n=0;for await(const part of req){n+=part.length;if(n>512)throw Error('Options too large');parts.push(part);}
    return reply(200,await changeOptions(JSON.parse(Buffer.concat(parts))));
   }
   if(req.url==='/test'){
    if(transport==='tcp')throw Error('Use the compressed test picture button');
    return reply(200,await stream.push(bars(),480,270));
   }
   const url=new URL(req.url,'http://127.0.0.1');
   if(['/prepare','/frame'].includes(url.pathname)&&req.headers['x-stream-profile']){const p=hl2ProfileFor(stream.consoleAddress?.()),expected=`${p.width}x${p.height}@${p.fps}:${options.compressionPolicy}:${options.renderMode}`;if(req.headers['x-stream-profile']!==expected)return reply(503,{error:'Render profile changed; recapture the current source',recoverable:true,retryAfterMs:50});}
   if(url.pathname==='/prepare'){
    if(source!=='browser')return reply(409,{error:'Doom is streaming directly'});
    const w=Number(url.searchParams.get('w')),h=Number(url.searchParams.get('h')),format=Number(url.searchParams.get('format')||1);dimensions(w,h,format);
    if(browserPending)return reply(409,{error:'Video preparation or frame already in progress'});
    const pending=preview?Promise.resolve({prepared:true,preview:true}):stream.prepare?.(w,h,format)||Promise.resolve({prepared:false});browserPending=pending;
    try{return reply(200,await pending);}finally{if(browserPending===pending)browserPending=null;}
   }
   if(url.pathname!=='/frame')return reply(404,{error:'Not found'});
   if(source!=='browser')return reply(409,{error:'Doom is streaming directly'});
   const w=Number(url.searchParams.get('w')),h=Number(url.searchParams.get('h')),format=Number(url.searchParams.get('format')||0);if(![0,1,2].includes(format))throw Error('Invalid capture format');dimensions(w,h,format);
   const parts=[];let n=0;for await(const part of req){n+=part.length;if(n>MAX)throw Error('Frame too large');parts.push(part);}
   const metrics=Buffer.alloc(32);metrics.writeUInt32LE(0x495a4d35);let values=[];try{const value=JSON.parse(req.headers['x-stream-metrics']||'[]');if(Array.isArray(value))values=value;}catch{}
   for(let i=0;i<5;i++){const v=Number(values[i]);metrics.writeUInt32LE(Number.isFinite(v)?Math.round(Math.max(0,Math.min(v,60000))*100):0,4+i*4);}
   // Client timings share the browser clock. They describe capture -> GPU ACK,
   // not display scanout, speaker output or measured audiovisual synchrony.
   if(['PC GPU','SIMD'].includes(values[5]))stream.stats.encoder=values[5];
   for(const [i,name] of ['fps','captureMs','encodeMs','reportedTransportMs','latencyMs'].entries())stream.stats[name]=metrics.readUInt32LE(4+i*4)/100;
   stream.stats.latencyMetric='browser capture to GPU ACK; excludes display and Bluetooth';
   if(source!=='browser')return reply(409,{error:'Doom is streaming directly'});
   const pending=stream.push(Buffer.concat(parts),w,h,format,metrics);browserPending=pending;
   try{reply(200,await pending);}finally{if(browserPending===pending)browserPending=null;}
  }catch(error){reply(error.recoverable?503:400,{error:error.message,...(error.recoverable?{recoverable:true,retryAfterMs:error.retryAfterMs}:{})});}
 });
 ({port}=await require('./lib/platform.cjs').require('sdk/platform.cjs').listenHttp(server,{port}));
 const url=`http://127.0.0.1:${port}/#${key}`;
 await fs.writeFile(path.join(directory,'pc-screen-local.json'),JSON.stringify({url,port,transport,formats:transport==='tcp'?['BC1-opaque']:['BC1-opaque','RGB565-LE'],defaultSize:[960,540],maximumSize:[1920,1080],viewOnly:true},null,2),{mode:0o600});
 console.log('PC Screen capture: '+url);
 const commands=new Set();
 function onState(state){
  const action=state.readFloatLE(968),sequence=state.readFloatLE(972);if(closing||!available||!Number.isInteger(sequence)||sequence<1||sequence===lastCommand||![1,2,3,4,5,6,7,16,17,18,19,20,21,22,24,25,26,27,28,29,30,32,33,34,35,36,37,38].includes(action))return;lastCommand=sequence;
  const task=(async()=>{
   let commandError;const row={action:action===1?'window':action===2?'doom':action===3?'half-life-2':action===4?'player':action===7?'jellymoped':action===6?'space-rangers':'grand-theft-auto-5',sequence,started:new Date().toISOString()};
   try{if(action<=6)await stopJellymoped();if(action===7){if(!preview){jellyStarting=(jellyStarting||Promise.resolve()).catch(()=>{}).then(launchJellymoped);await jellyStarting;}}else if(action>=24){const mode=action>=32?'handheldResolution':'dockResolution',index=action-(action>=32?32:24),value=index===0?(action>=32?'1280':'1920'):['640','832','960','1280','1600','1920'][index-1];await changeOptions({...options,resolution:'auto',[mode]:value});row.action='resolution';row[mode]=value;}else if(action>=16){await changeOptions({...options,resolution:['auto','640','832','960','1280','1600','1920'][action-16]});row.action='resolution';row.resolution=options.resolution;}else if(action>=2)await changeOptions({...options,launch:action===2?'doom':action===3?'half-life-2':action===4?'player':action===6?'space-rangers':'grand-theft-auto-5'});if((action===1||action===3||action===5)&&!panels.size&&!preview)await openWindow(url);if(action===3||action===5||action===6)gameFocus.arm();row.status='ready';await rpc.write(mapped+HEADER+148,Buffer.from([2,0,0,0]));}
   catch(error){commandError=error;row.status='failed';row.error=error.message;stream.stats.sourceError=error.message;stream.stats.sourceErrorStack=error.stack;emit();await rpc.write(mapped+HEADER+148,Buffer.from([3,0,0,0])).catch(()=>{});}
   row.completed=new Date().toISOString();await fs.writeFile(path.join(directory,'pc-screen-command.json'),JSON.stringify(row,null,2));
   if(commandError)throw commandError;
  })();
  commands.add(task);task.then(()=>commands.delete(task),()=>commands.delete(task));
  // Older callers may dispatch without awaiting; current session hooks await
  // the same promise so failures reach the module's error report.
  task.catch(error=>console.error('PC Screen command: '+error.message));return task;
 }
 async function close(){
  if(closePromise)return closePromise;closing=true;routeGate?.close();consoleMonitor?.close();gameFocus.close();controllerHub?.close();
  closePromise=(async()=>{
   await Promise.allSettled([...commands]);
   await jellyStarting?.catch(()=>{});await stopJellymoped();await optionQueue.catch(()=>{});await routeGate.drain();
   await stream.invalidate?.('stream-stopped').catch(error=>{stream.stats.closeVisibilityError=error.message;});await stopPump();clearInterval(events);for(const res of panels)res.end();panels.clear();
   const outcomes=await Promise.allSettled([stream.close?.(),remote?.close(),pad?.close()]);remote=null;pad=null;
   if(!await fs.access(path.join(directory,'result.json')).then(()=>true,()=>false))await rpc.write(mapped+HEADER+96,Buffer.from([0,0,0,0])).catch(()=>{});
   server.closeIdleConnections();server.closeAllConnections();await new Promise(resolve=>server.close(resolve));
   const error=outcomes.find(r=>r.status==='rejected');if(error)throw error.reason;
  })();return closePromise;
 }
 return {get stream(){return stream;},consoleState:()=>consoleMonitor?.state||null,onState,setVideoStorage:storage=>{if(storage){if(!Number.isSafeInteger(storage.mapped)||storage.mapped<=0||storage.bytes<0x200000)throw Error('Invalid private video storage');Object.assign(video,{base:storage.mapped,stride:0x100000,capacity:0x100000});}else Object.assign(video,{base:mapped+SLOT,stride:STRIDE,capacity:STRIDE});},setAvailable:value=>{available=!!value;},suspend:async()=>{await jellyStarting?.catch(()=>{});await stopJellymoped();await routeGate.drain();await stream.invalidate?.('application-suspended');await stopPump();source='browser';await stream.close?.();stream=makeStream();stream.stats.options=options;emit();},close};
}
module.exports={create,createDisplayFreshness,decodeConsoleDisplay,uploader,changed,bars,dimensions,frameBytes,formatName,SLOT,STRIDE,HEADER,MAX};
