// One upload plus one just-in-time encoded frame. No growing capture queue.
const {Worker}=require('node:worker_threads'),path=require('node:path'),pacing=require('./frame-pacing.js');
const wait=ms=>new Promise(r=>setTimeout(r,ms));
function sameProfile(a,b){return ['mode','width','height','fps'].every(k=>a[k]===b[k]);}
function start({remote,stream,onError=()=>{},onProfile=()=>{},profileFor=()=>require('../../local-package/render-sizes.cjs').profile(),workerFactory=()=>new Worker(path.join(__dirname,'hl2-frame-worker.cjs'))}){
 const worker=workerFactory();let stopped=false,pending,lastSequence=-1,workerError,generation=0,prepared,discarded=0,needsPrepare=!!remote.resumeCapture,sourceBarrier=Promise.resolve();
 const stopListeners=new Set();
 function interruptible(promise){return new Promise((resolve,reject)=>{const cancel=()=>{stopListeners.delete(cancel);resolve();};stopListeners.add(cancel);Promise.resolve(promise).then(value=>{stopListeners.delete(cancel);resolve(value);},error=>{stopListeners.delete(cancel);reject(error);});if(stopped)cancel();});}
 function failed(error){workerError??=error;pending?.reject(error);pending=null;}
 worker.on('message',d=>{const p=pending;pending=null;if(!p)return;if(d.error)p.reject(Error(d.error));else p.resolve({...d,bytes:Buffer.from(d.bytes.buffer,d.bytes.byteOffset,d.bytes.byteLength)});});
 worker.on('error',failed);worker.on('exit',code=>{if(!stopped)failed(Error('Half-Life 2 encoder exited: '+code));});
 const unsubscribe=remote.onFreshness?.(event=>{generation++;needsPrepare=true;discarded++;sourceBarrier=sourceBarrier.then(()=>stream.invalidate?.(event.reason||'source-interrupted'));return sourceBarrier;});
 async function delayUntil(at,epoch){while(!stopped&&epoch===generation){const remaining=at-performance.now();if(remaining<=0)return;await interruptible(wait(remaining));}}
 async function fresh(epoch){let until=Date.now()+5000;remote.requestFrame?.();while(!stopped&&epoch===generation){if(workerError)throw workerError;if(remote.report.error||remote.report.exited)throw Error(remote.report.error||'Half-Life 2 capture ended');if(remote.report.sourceHidden||remote.report.captureSuspended){until=Date.now()+5000;await interruptible(wait(20));continue;}const f=remote.report.reconfiguring?null:remote.video.latest;if(f&&f.sequence!==lastSequence){lastSequence=f.sequence;return f;}if(Date.now()>until)throw Error('Half-Life 2 stopped producing frames');await interruptible(wait(2));}return null;}
 async function prepareSource(profile){
  while(!stopped){if(remote.report.sourceHidden){await interruptible(wait(20));continue;}const own=generation;await interruptible(sourceBarrier);if(stopped)return false;
   try{await interruptible(stream.prepare?.(profile.width,profile.height,1));if(stopped)return false;if(remote.resumeCapture&&!await interruptible(remote.resumeCapture()))continue;if(own!==generation)continue;needsPrepare=false;return true;}
   catch(error){if(!error.recoverable)throw error;await interruptible(wait(error.retryAfterMs||750));}
  }return false;
 }
 async function candidate(epoch,at){
  await delayUntil(at,epoch);if(stopped||epoch!==generation)return null;
  const f=await fresh(epoch);if(!f||stopped||epoch!==generation)return null;if(workerError)throw workerError;
  const encoded=await new Promise((resolve,reject)=>{pending={resolve,reject};try{worker.postMessage(f,[f.rgba.buffer]);}catch(error){pending=null;reject(error);}});
  return stopped||epoch!==generation?null:{f,encoded,epoch};
 }
 // Prefetch errors are observed immediately even while the current ACK is pending.
 const prepare=(epoch,at)=>candidate(epoch,at).then(value=>({value}),error=>({error}));
 const done=(async()=>{let profile=remote.report.profile||profileFor(stream.consoleAddress?.());
  if(remote.resumeCapture){if(!await prepareSource(profile))return;}else{const first=await fresh(generation);if(!first||stopped)return;await interruptible(stream.prepare?.(first.w,first.h,1));}
  if(stopped)return;onProfile(profile);let nextAt=performance.now(),net=0,encodeMs=0,latency=0,recent=[];
  while(!stopped){
   const selected=profileFor(stream.consoleAddress?.());if(!sameProfile(selected,profile)){
    await interruptible(stream.invalidate?.('source-profile-changed'));if(stopped)break;
    generation++;if(prepared){await prepared;prepared=null;discarded++;}
    await remote.setProfile(selected);if(stopped)break;profile=selected;needsPrepare=true;recent=[];net=0;nextAt=performance.now();onProfile(profile);
   }
   if(needsPrepare){if(prepared){await prepared;prepared=null;discarded++;}if(!await prepareSource(profile))break;nextAt=performance.now();}
   let ready=await(prepared||prepare(generation,performance.now()));prepared=null;if(ready.error)throw ready.error;
   let value=ready.value;if(stopped)break;if(!value)continue;
   await delayUntil(nextAt,generation);if(stopped)break;
   if(value.epoch!==generation||remote.report.sourceHidden||remote.report.captureSuspended||value.f.sourceEpoch!==undefined&&value.f.sourceEpoch!==remote.report.sourceEpoch||!sameProfile(profile,profileFor(stream.consoleAddress?.()))){discarded++;continue;}
   const period=profile.fps>0?1000/profile.fps:0;
   // Include the capture cadence: a fresh 30Hz source can already be 33ms old.
   if(performance.now()-value.f.at>Math.max(16,period)+value.encoded.encodeMs){
    discarded++;ready=await prepare(generation,performance.now());if(ready.error)throw ready.error;value=ready.value;if(stopped)break;if(!value)continue;
    if(!sameProfile(profile,profileFor(stream.consoleAddress?.()))){discarded++;continue;}
   }
   const {f,encoded}=value;encodeMs=encoded.encodeMs;
   const begin=performance.now(),metrics=Buffer.alloc(32),fps=recent.length>1?(recent.length-1)*1000/(recent.at(-1)-recent[0]):0;nextAt=begin+period;
   metrics.writeUInt32LE(0x495a4d35);[fps,0,encodeMs,net,latency].forEach((v,i)=>metrics.writeUInt32LE(Math.round(Math.max(0,Math.min(60000,v))*100),4+i*4));
   const upload=stream.push(encoded.bytes,encoded.w,encoded.h,1,metrics);
   // At most this one candidate may run while the current frame is in flight.
   prepared=prepare(generation,begin+pacing.captureDelay(net,0,encodeMs,period));
   const prefetchFailure=prepared.then(result=>{if(result.error)throw result.error;return new Promise(()=>{});});
   try{await interruptible(Promise.race([upload,prefetchFailure]));if(stopped)break;}catch(error){
    generation++;await prepared;prepared=null;if(!error.recoverable)throw error;await interruptible(wait(error.retryAfterMs||750));continue;
   }
   const now=performance.now();net=now-begin;latency=now-f.at;recent.push(now);recent=recent.filter(t=>now-t<1000);
   Object.assign(stream.stats,{source:remote.report.source||'half-life-2',connectionMode:profile.mode,targetFps:profile.fps,encoder:'SIMD / native window',encodeOverlap:true,discardedPreparedFrames:discarded,fps:+(recent.length>1?(recent.length-1)*1000/(recent.at(-1)-recent[0]):0).toFixed(2),encodeMs:+encodeMs.toFixed(2),captureMs:0,latencyMs:+latency.toFixed(2),sourceAgeMs:+(begin-f.at).toFixed(2),latencyMetric:Number.isFinite(f.paintAt)?'Electron paint event to GPU ACK; excludes work before paint event, controller and display scanout':f.assemblyMs!==undefined?'host first raw-frame byte to GPU ACK; excludes X11/FFmpeg work before pipe, controller and display scanout':'host raw-frame arrival to GPU ACK; excludes capture pipeline, controller and display scanout'});
  }
 })().catch(error=>{if(!stopped)onError(error);});
 return {done,async close(){if(stopped)return;stopped=true;generation++;unsubscribe?.();for(const cancel of [...stopListeners])cancel();pending?.reject(Error('Half-Life 2 stream stopped'));pending=null;await worker.terminate();await done;}};
}
module.exports={start,sameProfile};
