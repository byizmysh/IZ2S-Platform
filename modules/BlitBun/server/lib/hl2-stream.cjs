// One upload plus one just-in-time encoded frame. No growing capture queue.
const {Worker}=require('node:worker_threads'),path=require('node:path'),pacing=require('./frame-pacing.js');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
function sameProfile(a,b){return ['mode','width','height','fps'].every(k=>a[k]===b[k]);}
function createEncoderWorker(){return new Worker(path.join(__dirname,'frame-worker.cjs'));}
function start({remote,stream,onError=()=>{},onProfile=()=>{},decorateMetrics=()=>{},profileFor=require('./render-profile.cjs').resolver(),workerFactory=createEncoderWorker,frameTimeoutMs=5000}){
 if(!Number.isFinite(frameTimeoutMs)||frameTimeoutMs<=0)throw Error('Invalid frame wait timeout');
 const worker=workerFactory();let stopped=false,pending,lastSequence=-1,workerError,generation=0,prepared,discarded=0,needsPrepare=!!remote.resumeCapture,sourceBarrier=Promise.resolve(),frameWait=null,routeWait=null;
 const stopListeners=new Set();
 // Cancel stale capture/route waits before joining a prefetched candidate.
 // A suspended native capture cannot produce the frame that join awaits.
 function nextGeneration(){generation++;needsPrepare=true;frameWait?.abort();routeWait?.abort();}
 const discard=value=>stream.discardPrepared?.(value?.compressionToken);
 async function clearPrepared(){if(!prepared)return;const result=await prepared;prepared=null;await discard(result.value);discarded++;}
 function interruptible(promise){return new Promise((resolve,reject)=>{const cancel=()=>{stopListeners.delete(cancel);resolve();};stopListeners.add(cancel);Promise.resolve(promise).then(value=>{stopListeners.delete(cancel);resolve(value);},error=>{stopListeners.delete(cancel);reject(error);});if(stopped)cancel();});}
 function failed(error){workerError??=error;frameWait?.abort();pending?.reject(error);pending=null;}
 worker.on('message',d=>{const p=pending;pending=null;if(!p)return;if(d.error)p.reject(Error(d.error));else {try{const {inputRgba,...encoded}=d;if(inputRgba&&p.frame)remote.recycleFrame?.(p.frame,Buffer.from(inputRgba.buffer,inputRgba.byteOffset,inputRgba.byteLength));p.resolve({...encoded,bytes:Buffer.from(d.bytes.buffer,d.bytes.byteOffset,d.bytes.byteLength)});}catch(error){p.reject(error);}}});
 worker.on('error',failed);worker.on('exit',code=>{if(!stopped)failed(Error('Half-Life 2 encoder exited: '+code));});
 const unsubscribe=remote.onFreshness?.(event=>{nextGeneration();discarded++;sourceBarrier=sourceBarrier.then(()=>stream.invalidate?.(event.reason||'source-interrupted'));return sourceBarrier;});
 async function delayUntil(at,epoch){while(!stopped&&epoch===generation){const remaining=at-performance.now();if(remaining<=0)return;await interruptible(wait(remaining));}}
 async function waitForFrame(epoch,timeoutMs=0){
  const own=new AbortController();frameWait=own;let expired=false;const timer=timeoutMs>0?setTimeout(()=>{expired=true;own.abort();},timeoutMs):null;
  try{return await remote.waitFrame({afterSequence:lastSequence,sourceEpoch:remote.report.sourceEpoch,signal:own.signal});}
  catch(error){if(error.name!=='AbortError')throw error;if(expired&&!stopped&&epoch===generation)throw Error('Half-Life 2 stopped producing frames');return null;}
  finally{if(timer)clearTimeout(timer);if(frameWait===own)frameWait=null;}
 }
 async function fresh(epoch){let until=Date.now()+frameTimeoutMs;if(!remote.waitFrame)remote.requestFrame?.();while(!stopped&&epoch===generation){if(workerError)throw workerError;if(remote.report.error||remote.report.exited||remote.report.closed)throw Error(remote.report.error||'Half-Life 2 capture ended');
  if(remote.waitFrame){const hidden=remote.report.sourceHidden||remote.report.captureSuspended;if(hidden)until=Date.now()+frameTimeoutMs;const frame=await waitForFrame(epoch,hidden?0:Math.max(1,until-Date.now()));if(frame&&frame.sequence!==lastSequence){lastSequence=frame.sequence;return frame;}continue;}
  if(remote.report.sourceHidden||remote.report.captureSuspended){until=Date.now()+frameTimeoutMs;await interruptible(wait(20));continue;}const f=remote.report.reconfiguring?null:remote.video.latest;if(f&&f.sequence!==lastSequence){lastSequence=f.sequence;return f;}if(Date.now()>until)throw Error('Half-Life 2 stopped producing frames');await interruptible(wait(2));}return null;}

 async function waitForRoute(epoch=generation){
  if(!stream.waitForRoute)return !stopped;const own=new AbortController();routeWait=own;
  try{await stream.waitForRoute({signal:own.signal});return !stopped;}catch(error){if(error.name==='AbortError'&&(stopped||epoch!==generation))return false;throw error;}finally{if(routeWait===own)routeWait=null;}
 }
 async function prepareSource(profile){
  while(!stopped){if(!await waitForRoute()){if(stopped)return false;continue;}if(remote.report.closed||remote.report.error)throw Error(remote.report.error||'Capture ended');if(remote.report.sourceHidden){if(remote.waitFrame)await waitForFrame(generation);else await interruptible(wait(20));continue;}const own=generation;await interruptible(sourceBarrier);if(stopped)return false;
   try{await interruptible(stream.prepare?.(profile.width,profile.height,1));if(stopped)return false;if(remote.resumeCapture&&!await interruptible(remote.resumeCapture()))continue;if(own!==generation)continue;needsPrepare=false;return true;}
   catch(error){if(!error.recoverable)throw error;await interruptible(wait(error.retryAfterMs||750));}
  }return false;
 }
 async function candidate(epoch,at,metrics){
  await delayUntil(at,epoch);if(stopped||epoch!==generation)return null;
  if(!await waitForRoute(epoch)||stopped||epoch!==generation)return null;
  const routeGeneration=stream.routeGeneration?.(),expectedProfile=profileFor(stream.consoleAddress?.()),f=await fresh(epoch);if(!f||stopped||epoch!==generation)return null;if(workerError)throw workerError;if(f.w!==expectedProfile.width||f.h!==expectedProfile.height)return null;
  const encoded=await new Promise((resolve,reject)=>{pending={resolve,reject,frame:f};try{worker.postMessage(f,[f.rgba.buffer]);}catch(error){pending=null;reject(error);}});
  if(stopped||epoch!==generation)return null;
  const began=performance.now(),compressionToken=await stream.prepareFrame?.(encoded.bytes,encoded.w,encoded.h,1),preparationMs=performance.now()-began;
  if(stopped||epoch!==generation||routeGeneration!==stream.routeGeneration?.()||remote.report.sourceHidden||remote.report.captureSuspended||f.sourceEpoch!==undefined&&f.sourceEpoch!==remote.report.sourceEpoch||!sameProfile(expectedProfile,profileFor(stream.consoleAddress?.()))){await stream.discardPrepared?.(compressionToken);return null;}
  if(compressionToken&&stream.commitPrepared&&(expectedProfile.fps<=0||expectedProfile.fps>=60)){const frameMetrics=Buffer.from(metrics||Buffer.alloc(32));frameMetrics.writeUInt32LE(0x495a4d35);frameMetrics.writeUInt32LE(Math.round(Math.max(0,Math.min(60000,encoded.encodeMs))*100),12);await stream.commitPrepared(compressionToken,frameMetrics);}
  return {f,encoded,epoch,routeGeneration,compressionToken,preparationMs};
 }
 // Prefetch errors are observed immediately even while the current ACK is pending.
 const prepare=(epoch,at,metrics)=>candidate(epoch,at,metrics).then(value=>({value}),error=>({error}));
 const done=(async()=>{let profile=remote.report.profile||profileFor(stream.consoleAddress?.());
  if(remote.resumeCapture){if(!await prepareSource(profile))return;}else{const first=await fresh(generation);if(!first||stopped)return;await interruptible(stream.prepare?.(first.w,first.h,1));}
  if(stopped)return;onProfile(profile);const ackForecast=pacing.createAckForecast();let nextAt=performance.now(),net=0,encodeMs=0,latency=0,recent=[],lastCompleted=0;
  while(!stopped){
   const selected=profileFor(stream.consoleAddress?.());if(!sameProfile(selected,profile)){
    await interruptible(stream.invalidate?.('source-profile-changed'));if(stopped)break;
    nextGeneration();if(prepared)await clearPrepared();
    await remote.setProfile(selected);if(stopped)break;profile=selected;needsPrepare=true;recent=[];net=0;ackForecast.reset();nextAt=performance.now();onProfile(profile);
   }
   if(needsPrepare){if(prepared)await clearPrepared();if(!await prepareSource(profile))break;net=0;ackForecast.reset();nextAt=performance.now();}
   const candidateBegan=performance.now();let ready=await(prepared||prepare(generation,performance.now()));prepared=null;const preparationWaitMs=performance.now()-candidateBegan;if(ready.error){if(ready.error.recoverable){needsPrepare=true;continue;}throw ready.error;}
   let value=ready.value;if(stopped)break;if(!value)continue;
   await delayUntil(nextAt,generation);if(stopped)break;
   if(value.epoch!==generation||value.routeGeneration!==stream.routeGeneration?.()||remote.report.sourceHidden||remote.report.captureSuspended||value.f.sourceEpoch!==undefined&&value.f.sourceEpoch!==remote.report.sourceEpoch||!sameProfile(profile,profileFor(stream.consoleAddress?.()))){await discard(value);discarded++;continue;}
   const period=profile.fps>0?1000/profile.fps:0;
   // Include the capture cadence: a fresh 30Hz source can already be 33ms old.
   if(!stream.isWireCommitted?.(value.compressionToken)&&performance.now()-value.f.at>Math.max(16,period)+value.encoded.encodeMs+value.preparationMs&&pacing.replacementWorthwhile(value.f.at,remote.video.latest?.at,value.encoded.encodeMs+value.preparationMs)){
    await discard(value);discarded++;ready=await prepare(generation,performance.now());if(ready.error){if(ready.error.recoverable){needsPrepare=true;continue;}throw ready.error;}value=ready.value;if(stopped)break;if(!value)continue;
    if(!sameProfile(profile,profileFor(stream.consoleAddress?.()))){await discard(value);discarded++;continue;}
   }
   const {f,encoded}=value;encodeMs=encoded.encodeMs;
   const begin=performance.now(),metrics=Buffer.alloc(32),fps=recent.length>1?(recent.length-1)*1000/(recent.at(-1)-recent[0]):0;nextAt=begin+period;
   metrics.writeUInt32LE(0x495a4d35);[fps,0,encodeMs,net,latency].forEach((v,i)=>metrics.writeUInt32LE(Math.round(Math.max(0,Math.min(60000,v))*100),4+i*4));
   decorateMetrics(metrics);
   const upload=stream.push(encoded.bytes,encoded.w,encoded.h,1,metrics,value.compressionToken);
   // At most this one candidate may run while the current frame is in flight.
   // A single delayed Wi-Fi ACK must not delay the following fast frame too.
   // Keep the same single-candidate queue and stale-frame replacement guard.
   const prepareDelay=(profile.fps<=0||profile.fps>=60)&&stream.wirePrefetchReady?.()?0:pacing.captureDelay(ackForecast.predict(net),0,encodeMs+Math.min(16,value.preparationMs||0),period);
   prepared=prepare(generation,begin+prepareDelay,metrics);
   const prefetchFailure=prepared.then(result=>{if(result.error&&!result.error.recoverable)throw result.error;return new Promise(()=>{});});
   try{await interruptible(Promise.race([upload,prefetchFailure]));if(stopped)break;}catch(error){
    nextGeneration();await clearPrepared();if(!error.recoverable)throw error;await interruptible(wait(error.retryAfterMs||750));continue;
   }
   const now=performance.now();net=now-begin;ackForecast.record(net);latency=now-f.at;recent.push(now);recent=recent.filter(t=>now-t<1000);
   Object.assign(stream.stats,{framePreparationWaitMs:+preparationWaitMs.toFixed(2),frameIntervalMs:lastCompleted?+(now-lastCompleted).toFixed(2):null,compressionPrefetch:!!value.compressionToken,source:remote.report.source||'half-life-2',connectionMode:profile.mode,targetFps:profile.fps,encoder:encoded.encoder||'SIMD / native window',encodeOverlap:true,discardedPreparedFrames:discarded,fps:+(recent.length>1?(recent.length-1)*1000/(recent.at(-1)-recent[0]):0).toFixed(2),encodeMs:+encodeMs.toFixed(2),captureMs:0,latencyMs:+latency.toFixed(2),sourceAgeMs:+(begin-f.at).toFixed(2),latencyMetric:Number.isFinite(f.paintAt)?'Electron paint event to GPU ACK; excludes work before paint event, controller and display scanout':f.assemblyMs!==undefined?'host first raw-frame byte to GPU ACK; excludes X11/FFmpeg work before pipe, controller and display scanout':'host raw-frame arrival to GPU ACK; excludes capture pipeline, controller and display scanout'});lastCompleted=now;
  }
 })().catch(error=>{if(!stopped)onError(error);});
 return {done,async close(){if(stopped)return;stopped=true;nextGeneration();unsubscribe?.();for(const cancel of [...stopListeners])cancel();pending?.reject(Error('Half-Life 2 stream stopped'));pending=null;await worker.terminate();await done;}};
}
module.exports={start,sameProfile,createEncoderWorker};
