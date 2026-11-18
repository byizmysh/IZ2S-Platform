// Bounded low-latency PCM output. Reopen the device when docking or output changes.
const fs=require('node:fs/promises'),path=require('node:path');
const symbols=require('./platform.cjs').require('core/console/abi/audio-symbols.json');
const RATE=48000,CHUNK=19200,SLOTS=4,SLOT=65536,HEADER=65536;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const check=(v,m)=>{if(!v)throw Error(m);};
// Bridge actual queue drops without adding samples or buffering. Normal PCM
// stays byte-identical; only the first 2 ms after a discontinuity are blended.
function createDropRamp(frames=96){
 check(Number.isInteger(frames)&&frames>0&&frames<=480,'Invalid audio edge ramp');let last=null,seen=0;const stats={ramps:0};
 return {stats,reset(dropped=0){last=null;seen=dropped;},process(pcm,dropped=0){check(Buffer.isBuffer(pcm)&&pcm.length%4===0,'Invalid stereo PCM');if(!pcm.length)return pcm;
  if(dropped>seen&&last){const count=Math.min(frames,pcm.length/4);for(let i=0;i<count;i++)for(let channel=0;channel<2;channel++){const at=i*4+channel*2,weight=(i+1)/count;pcm.writeInt16LE(Math.round(last[channel]*(1-weight)+pcm.readInt16LE(at)*weight),at);}stats.ramps++;}
  seen=dropped;last=[pcm.readInt16LE(pcm.length-4),pcm.readInt16LE(pcm.length-2)];return pcm;
 }};
}
async function create({rpc,directory,source,enabled=()=>true,reference,label='Doom',audioOutput=()=> 'auto',routeIntervalMs=2000,routeSignal=()=>0}){
 check(source,'No PCM source');check(Number.isFinite(routeIntervalMs)&&routeIntervalMs>0,'Invalid audio route interval');const dropRamp=createDropRamp();
 // A refill includes a released-buffer RPC and a guarded write/append RPC.
 // Size blocks for that round trip, not for capture's smaller packet cadence.
 const batched=typeof rpc.writeCall==='function',sourceLimit=source.maxBytes;
 let chunkMs=batched?64:100,chunk=192*chunkMs,refillCost=0,pressure=0;
 if(batched&&Number.isInteger(sourceLimit))source.maxBytes=Math.min(sourceLimit,chunk*2);
 const report={session:rpc.a.session,source:label,started:new Date().toISOString(),format:'S16LE',sampleRate:RATE,channels:2,bytesPerSecond:192000,initialChunkMs:chunkMs,chunkMs,targetQueueMs:chunkMs*2,batchedRefill:batched,route:'console default output',functions:{},submitted:0,underruns:0,deadlineMisses:0,resourcesRetained:true};
 const file=path.join(directory,'stream-audio.json');
 const save=()=>fs.writeFile(file,JSON.stringify({...report,captureBytes:source.received,captureNonzeroChunks:source.nonzero,hostQueuedBytes:source.length,hostQueuedMs:source.length/192,scheduledDeviceQueueMs:Math.max(0,...[...expectedEnds.values()].map(t=>t-performance.now())),latencyEstimateOnly:true,bluetoothLatencyMeasured:false,droppedBytes:source.dropped},null,2));
 reference??=require('./platform.cjs').require('core/console/abi/sdk-checks.cjs');
 async function qualified(name){
  if(!report.functions[name]){const matches=symbols.filter(s=>name.startsWith('_Z')?s.name===name:s.name.includes(name));check(matches.length===1,'Ambiguous audio function '+name);const s=matches[0],address=rpc.a.sdk+s.offset;await rpc.request({command:'verify-function',address,hex:reference.bytes(s.offset,Math.min(32,s.size)).toString('hex')});report.functions[name]=address;}
  return report.functions[name];
 }
 async function call(name,...args){return (await rpc.callAddress(await qualified(name),args,{priority:'audio'})).result;}
 let base,opened=false,started=false,closed=false,done,lastSave=0;
 const free=new Set(),pending=new Set(),expectedEnds=new Map();let descriptors=new Map(),channels=2,routeStamp=null,lastRouteCheck=0,routeSignalSeen=null;
 async function release(drainAll=false){
  // Once every owned descriptor is released there is nothing left to reclaim.
  // Never probe the SDK's empty release list during drain/pause/cleanup.
  if(!pending.size){report.emptyReleasePollsAvoided=(report.emptyReleasePollsAvoided||0)+1;return;}
  for(let i=0;i<SLOTS&&pending.size;i++){const began=performance.now(),p=Number(await call('GetReleasedAudioOutBuffer',base));report.releasePolls=(report.releasePolls||0)+1;report.lastReleasePollMs=+(performance.now()-began).toFixed(2);report.maxReleasePollMs=Math.max(report.maxReleasePollMs||0,report.lastReleasePollMs);if(!p)break;check(descriptors.has(p)&&pending.has(p),'Unknown or duplicate audio buffer release');pending.delete(p);expectedEnds.delete(p);free.add(p);if(!drainAll&&pending.size<2)break;}
 }
 async function pause(){dropRamp.reset(source.dropped);if(started){check(await call('StopAudioOut',base)===0n,'Audio stop failed');started=false;}await call('FlushAudioOutBuffers',base);await release(true);check(Number(await call('GetAudioOutBufferCount',base))===0,'Audio queue did not drain');pending.clear();expectedEnds.clear();free.clear();for(const d of descriptors.keys())free.add(d);}
 async function cleanup(){if(!opened)return;await pause();await call('CloseAudioOut',base);opened=false;}
 async function adjustBuffer(){
  // Never grow on a single scheduling spike. At most 320 ms is queued on the
  // device, and the live capture queue stays bounded independently (200 ms).
  const limit=Math.min(160,Math.floor((batched?32768:SLOT)/(96*channels)/8)*8);
  const next=[64,96,128,160].find(ms=>ms>chunkMs&&ms>=refillCost*1.2&&ms<=limit)
   ||Math.min(limit,chunkMs<96?96:chunkMs+32);
  if(next<=chunkMs){pressure=0;report.bufferLimitReached=true;return;}
  await pause();chunkMs=next;chunk=192*chunkMs;source.clear();
  if(Number.isInteger(sourceLimit))source.maxBytes=Math.min(sourceLimit,chunk*2);
  for(const [d,address]of descriptors)await call('SetAudioOutBufferInfo',d,address,SLOT,chunk*channels/2);
  report.chunkMs=chunkMs;report.targetQueueMs=chunkMs*2;report.bufferAdjustments=(report.bufferAdjustments||0)+1;pressure=0;
 }
 function expand(pcm){
  if(channels===2)return pcm;
  // HDMI 5.1 PCM: front L/R carry stereo; center, LFE and surrounds are silent.
  const out=Buffer.alloc(pcm.length*3);for(let i=0,j=0;i<pcm.length;i+=4,j+=12)pcm.copy(out,j,i,i+4);return out;
 }
 async function route(){
  const mode=audioOutput();check(['auto','hdmi','bluetooth'].includes(mode),'Invalid audio output');
  const at=base+0x1000;await rpc.write(at,Buffer.alloc(256));
  await call('_ZN2nn5audio30GetActiveAudioOutputDeviceNameEPNS0_15AudioDeviceNameE',at);
  const active=(await rpc.read(at,256)).toString('utf8').split('\0')[0];
  // The source is stereo. The system routes/mixes it to HDMI itself; sending
  // four silent channels triples RPC traffic and cuts the maximum block size.
  return {mode,active,channels:2};
 }
 async function openRoute(param=base+0x300){
  const desired=await route();
  await call('InitializeAudioOutParameter',param);const params=await rpc.read(param,8);params.writeUInt32LE(RATE);params.writeUInt16LE(desired.channels,4);await rpc.write(param,params);
  const names=base+0x2000;await rpc.write(names,Buffer.alloc(8*256));const count=Number(await call('ListAudioOutsEPNS0_',names,8));check(count>=0&&count<=8,'Invalid audio output list');
  const raw=await rpc.read(names,count*256),outputs=Array.from({length:count},(_,i)=>raw.subarray(i*256,(i+1)*256).toString('utf8').split('\0')[0]);
  report.availableOutputs=outputs;report.activeDevice=desired.active;report.requestedOutput=desired.mode;
  let selected=desired.mode==='bluetooth'?outputs.find(n=>/bluetooth/i.test(n)):desired.mode==='hdmi'?outputs.find(n=>/hdmi/i.test(n)):undefined;
  selected??=outputs.find(n=>n==='DeviceOut');
  let result;if(selected){await rpc.write(names,Buffer.from(selected+'\0'));result=await call('OpenAudioOutEPNS0_8AudioOutEPKc',base,names,param);}else{check(desired.mode==='auto','Requested audio output is unavailable');result=await call('OpenDefaultAudioOutEPNS0_8AudioOutER',base,param);}
  check(result===0n,'Audio output open failed ('+desired.mode+')');opened=true;
  channels=Number(await call('GetAudioOutChannelCount',base));if(batched&&channels===6&&chunkMs>48){chunkMs=48;chunk=192*chunkMs;if(Number.isInteger(sourceLimit))source.maxBytes=Math.min(sourceLimit,chunk*2);}report.chunkMs=chunkMs;report.targetQueueMs=chunkMs*2;check(await call('GetAudioOutSampleRate',base)===48000n&&[2,6].includes(channels)&&await call('GetAudioOutSampleFormat',base)===2n,'Unexpected audio format');
  const namePtr=Number(await call('GetAudioOutName',base));report.outputName=namePtr?(await rpc.read(namePtr,256)).toString('utf8').split('\0')[0]:selected||'default';
  report.channels=channels;report.route=/hdmi|tv/i.test(desired.active)?'TV / HDMI':/bluetooth/i.test(desired.active)?'Bluetooth':/headset|headphone/i.test(desired.active)?'Headphones':/speaker/i.test(desired.active)?'Console speakers':'System output';report.warning=desired.mode==='hdmi'&&/bluetooth/i.test(desired.active)?'Bluetooth is still active. Select TV / HDMI on the console.':null;report.outputBytesPerSecond=RATE*channels*2;
  descriptors=new Map();free.clear();pending.clear();expectedEnds.clear();for(let i=0;i<SLOTS;i++){const d=base+0x400+i*0x40,address=base+HEADER+i*SLOT;descriptors.set(d,address);free.add(d);await call('SetAudioOutBufferInfo',d,address,SLOT,chunk*channels/2);}
  routeStamp=JSON.stringify(desired);routeSignalSeen=JSON.stringify([audioOutput(),routeSignal()]);lastRouteCheck=Date.now();
 }
 try{
  const temp=rpc.a.scratch+0x10000,saved=await rpc.read(temp,8);
  try{check((await rpc.call(5732112,[temp,65536,HEADER+SLOTS*SLOT])).result===0n,'Audio allocation failed');base=Number((await rpc.read(temp,8)).readBigUInt64LE());}finally{await rpc.write(temp,saved);}
  check(Number.isSafeInteger(base)&&base>0&&base%65536===0,'Invalid audio allocation');report.base=base;
  await rpc.write(base,Buffer.alloc(HEADER));const param=base+0x300;
  await openRoute(param);
  if(batched)await qualified('AppendAudioOutBuffer');
  report.ready=true;await save();
 }catch(error){report.error=error.message;try{await cleanup();}catch(e){report.cleanupError=e.message;}if(Number.isInteger(sourceLimit))source.maxBytes=sourceLimit;await save();throw error;}
 done=(async()=>{
  let active=false,lastCycle=performance.now(),releasedThisCycle=0;
  async function checkRoute(playing){
   const signal=JSON.stringify([audioOutput(),routeSignal()]);if(signal===routeSignalSeen)return;
   const cushion=started?Math.max(0,...[...expectedEnds.values()].map(end=>end-performance.now())):Infinity;
   const budget=Math.min(chunkMs*1.5,Math.max(12,(report.lastRefillMs||0)*3+8));
   if(playing&&started&&cushion<budget){report.routePollDeferrals=(report.routePollDeferrals||0)+1;return;}
   const began=performance.now(),desired=await route();report.lastRoutePollMs=+(performance.now()-began).toFixed(2);lastRouteCheck=Date.now();routeSignalSeen=signal;if(JSON.stringify(desired)!==routeStamp){await cleanup();source.clear();await openRoute();active=false;report.routeChanges=(report.routeChanges||0)+1;await save();}
  }

  while(!closed){
   const now=performance.now();report.maxLoopGapMs=Math.max(report.maxLoopGapMs||0,+(now-lastCycle).toFixed(2));if(active&&now-lastCycle>500){await pause();source.clear();report.resumes=(report.resumes||0)+1;report.playing=false;report.queuedBuffers=0;}lastCycle=performance.now();
   const next=!!enabled();if(!next){source.clear();if(active){await pause();report.playing=false;report.queuedBuffers=0;report.lastAt=new Date().toISOString();await save();}await checkRoute(false);active=false;await sleep(40);continue;}active=true;
   releasedThisCycle=0;
   // Poll the device only when a fresh chunk can actually refill it. Stop after
   // one released descriptor, avoiding the former extra RPC that returned zero.
   if(pending.size>=2&&source.length>=chunk){
    const times=[...expectedEnds.values()],at=performance.now();
    // The prediction only schedules polling. A native release still grants
    // ownership before any PCM buffer can be written again.
    if(!times.length||at>=Math.min(...times)){const began=performance.now();await release(times.length>0&&at>Math.max(...times)+5);releasedThisCycle=performance.now()-began;if(started&&!pending.size){report.underruns++;pressure=Math.max(pressure,2);}}
   }
   if(pending.size<2&&source.length>=chunk){
    const d=free.values().next().value;check(d!==undefined,'No free audio buffer');const stereo=dropRamp.process(source.take(chunk),source.dropped);report.discontinuityRamps=dropRamp.stats.ramps;const pcm=expand(stereo);
    const previousEnd=started?Math.max(0,...expectedEnds.values()):0;
    const refillBegan=performance.now();let result;if(batched)result=(await rpc.writeCall(descriptors.get(d),pcm,report.functions.AppendAudioOutBuffer,[base,d],{priority:'audio'})).result;
    else{await rpc.write(descriptors.get(d),pcm);result=await call('AppendAudioOutBuffer',base,d);}
    report.lastRefillMs=+(performance.now()-refillBegan).toFixed(2);report.maxRefillMs=Math.max(report.maxRefillMs||0,report.lastRefillMs);check(result===1n,'Audio buffer rejected');free.delete(d);pending.add(d);report.submitted++;
    if(started){if(previousEnd&&performance.now()>previousEnd){report.deadlineMisses=(report.deadlineMisses||0)+1;report.lastGapMs=+(performance.now()-previousEnd).toFixed(2);}expectedEnds.set(d,Math.max(performance.now(),...expectedEnds.values())+chunkMs);}
    const cost=report.lastRefillMs+releasedThisCycle;refillCost=refillCost?refillCost*.75+cost*.25:cost;report.refillCycleMs=+refillCost.toFixed(2);
    pressure=refillCost>chunkMs*.85?pressure+1:0;
    if(!started&&pending.size>=2){check(await call('StartAudioOut',base)===0n,'Audio playback failed');started=true;let end=performance.now();for(const descriptor of pending){end+=chunkMs;expectedEnds.set(descriptor,end);}}
   }else{if(source.ended&&source.length<chunk)throw Error('PCM source ended');await sleep(5);}
   if(pressure>=3)await adjustBuffer();
   await checkRoute(active);
   if(Date.now()-lastSave>1000){report.playing=started;report.queuedBuffers=pending.size;report.lastAt=new Date().toISOString();await save();lastSave=Date.now();}
  }
 })().catch(error=>{report.error=error.message;console.error(label+' audio: '+error.message);}).finally(async()=>{try{await cleanup();}catch(error){report.cleanupError=error.message;}report.playing=false;report.queuedBuffers=pending.size;report.completed=new Date().toISOString();if(Number.isInteger(sourceLimit))source.maxBytes=sourceLimit;await save();});
 return {report,done,async close(){if(closed)return done;closed=true;await done;}};
}
module.exports={create,createDropRamp,RATE,CHUNK,SLOTS,SLOT,HEADER};
