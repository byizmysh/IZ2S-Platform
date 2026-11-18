const fs=require('node:fs/promises'),path=require('node:path'),{spawn}=require('node:child_process'),{PcmQueue}=require('./pcm-queue.cjs'),audioSource=require('./audio.cjs');
const PROFILE=Object.freeze({mode:'vacootube',width:1280,height:720,fps:0});
function frameReader(video,report,{now=Date.now,monotonic=()=>performance.now(),accept=()=>true,onFrame=()=>{}}={}){
 let header=Buffer.alloc(32),headerBytes=16,head=0,body=null,at=0,size=0,width=0,height=0;
 return chunk=>{for(let offset=0;offset<chunk.length;){if(!body){
  const n=Math.min(headerBytes-head,chunk.length-offset);chunk.copy(header,head,offset,offset+n);head+=n;offset+=n;
  if(head>=4){const magic=header.toString('ascii',0,4);if(!['IYF1','IYF2'].includes(magic))throw Error('Invalid YouTube capture frame');headerBytes=magic==='IYF2'?32:16;}
  if(head<headerBytes)continue;
  width=header.readUInt32LE(8);height=header.readUInt32LE(12);const expected=require('./render-sizes.cjs').profile(height);size=width*height*4;
  if(width!==expected.width||header.readUInt32LE(4)!==size)throw Error('Invalid YouTube capture frame');body=Buffer.allocUnsafe(size);at=0;
 }
  const n=Math.min(size-at,chunk.length-offset);chunk.copy(body,at,offset,offset+n);at+=n;offset+=n;if(at===size){
   const timestamped=headerBytes===32,requestId=timestamped?header.readUInt32LE(16):0,paintRevision=timestamped?header.readUInt32LE(20):null,paintAt=timestamped?header.readDoubleLE(24):now();
   if(!Number.isFinite(paintAt)||paintAt<=0)throw Error('Invalid YouTube paint timestamp');
   const age=Math.max(0,now()-paintAt);report.receivedFrames=(report.receivedFrames||0)+1;
   if(accept({requestId,paintRevision,timestamped,w:width,h:height})){video.latest={w:width,h:height,rgba:body,bgra:true,sourceEpoch:report.sourceEpoch??0,sequence:++video.frames,requestId,paintRevision,paintAt:timestamped?paintAt:null,paintAgeMs:age,at:monotonic()-age};report.frames=video.frames;report.captureProtocol=timestamped?'IYF2 / compositor paint timestamp':'IYF1 / receive timestamp';report.lastPaintAgeMs=age;report.maxPaintAgeMs=Math.max(report.maxPaintAgeMs||0,age);onFrame(video.latest);}
   else report.discardedResponses=(report.discardedResponses||0)+1;
   body=null;head=0;headerBytes=16;
  }
 }};
}
async function create(directory,{launch=spawn,findAudio=audioSource.target,locate=require('./install.cjs').locate,createSink=audioSource.silentSink,selfTest=false,availabilityFactory,firstFrameTimeoutMs=20000,profile=PROFILE}={}){
 profile=require('./render-sizes.cjs').profile(profile.height);
 // Discovery and owned sink setup are independent. Await both even on failure:
 // an asynchronously created sink must never escape a failed startup.
 const initialized=await Promise.allSettled([Promise.resolve().then(locate),Promise.resolve().then(createSink)]);
 if(initialized[0].status==='rejected'){
  if(initialized[1].status==='fulfilled')await initialized[1].value.close();
  throw initialized[0].reason;
 }
 const runtime=initialized[0].value;
 const sink=initialized[1].status==='fulfilled'?initialized[1].value:null;
 let closed=false,closePromise,audioChild,audioStarting=false,framePending=null,requestSequence=0,frameDemand=false,waiting,availability,child,log,resizePending,resizeSequence=0;
 const freshnessListeners=new Set();
 const report={source:'vacootube',pcAudio:'silent',audioSink:sink?.name,audioModule:sink?.module,audioMuted:!sink,audioError:sink?undefined:initialized[1].reason?.message,profile,version:runtime.version,scope:'owned VacooTube window and application audio',frames:0,sourceEpoch:0},video={latest:null,frames:0},audio=new PcmQueue();
 const save=()=>fs.writeFile(path.join(directory,'youtube-capture.json'),JSON.stringify({...report,audio:{received:audio.received,nonzero:audio.nonzero,dropped:audio.dropped}},null,2));
 const notify=()=>availability?.notify();
 const send=m=>{if(!closed&&child&&!child.stdio[3].destroyed&&child.stdio[3].writableLength<8192){child.stdio[3].write(JSON.stringify(m)+'\n');return true;}return false;};
 function issueFrame(){if(frameDemand&&!closed&&!report.captureSuspended&&report.ready&&!framePending&&!report.exited){const request={id:++requestSequence,at:performance.now()};framePending=request;if(send({type:'frame',requestId:request.id})){video.latest=null;report.requests=(report.requests||0)+1;}else framePending=null;}}
 function requestFrame(){frameDemand=true;issueFrame();}
 function waitFrame(options={}){
  if(waiting)return Promise.reject(Error('A YouTube frame wait is already active'));
  if(options.signal?.aborted)return availability.waitFrame(options);
  const pending=availability.waitFrame(options);
  if(!closed&&(options.sourceEpoch===undefined||options.sourceEpoch===report.sourceEpoch)&&(!video.latest||video.latest.sequence<=(options.afterSequence??-1))){frameDemand=true;issueFrame();}
  const result=pending.finally(()=>{if(waiting===result){waiting=null;frameDemand=false;}});waiting=result;return result;
 }
 async function pauseCapture(reason='console-route-pending'){
  if(closed||report.captureSuspended)return false;report.captureSuspended=true;report.routePaused=true;report.sourceEpoch++;video.latest=null;framePending=null;notify();
  for(const listener of freshnessListeners)await listener({reason,sourceEpoch:report.sourceEpoch});return true;
 }
 async function resumeCapture(){if(closed)return false;report.captureSuspended=false;report.routePaused=false;notify();issueFrame();return true;}
 async function setProfile(next){
  next=require('./render-sizes.cjs').profile(next.height);if(next.width===report.profile.width&&next.height===report.profile.height)return;
  if(closed||resizePending)throw Error('VacooTube resize is unavailable');
  report.reconfiguring=true;report.captureSuspended=true;report.sourceEpoch++;video.latest=null;framePending=null;notify();
  try{
   for(const listener of freshnessListeners)await listener({reason:'render-resolution-changed',sourceEpoch:report.sourceEpoch});
   const id=++resizeSequence;await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{resizePending=null;reject(Error('VacooTube render resize timed out'));},8000);resizePending={id,resolve:()=>{clearTimeout(timer);resizePending=null;resolve();},reject:error=>{clearTimeout(timer);resizePending=null;reject(error);}};if(!send({type:'resize',resizeId:id,width:next.width,height:next.height}))resizePending.reject(Error('VacooTube resize channel is closed'));});
   report.profile=next;report.captureSuspended=!!report.routePaused;delete report.error;await save();
  }finally{report.reconfiguring=false;notify();issueFrame();}
 }
 async function startAudio(){if(!sink||closed||report.exited||audioChild||audioStarting||!child?.pid)return;audioStarting=true;try{const target={device:sink.name+'.monitor',scope:'owned YouTube sink'};if(closed)return;audioChild=launch('/usr/bin/parec',audioSource.sinkArgs(sink.name),{stdio:['ignore','pipe','pipe']});report.audioTarget=target;audioChild.stdout.on('data',b=>audio.push(b));audioChild.stderr.on('data',b=>report.audioError=b.toString().slice(-1000));audioChild.on('error',e=>report.audioError=e.message);audioChild.on('exit',()=>{audioChild=null;});}catch(e){report.audioError=e.message;}finally{audioStarting=false;}}
 async function stop(p,grace=0){if(!p||p.exitCode!==null||p.signalCode)return;if(grace)await new Promise(resolve=>{const t=setTimeout(resolve,grace);p.once('exit',()=>{clearTimeout(t);resolve();});});if(p.exitCode!==null||p.signalCode)return;p.kill('SIGTERM');await new Promise(resolve=>{const t=setTimeout(()=>{p.kill('SIGKILL');resolve();},1500);p.once('exit',()=>{clearTimeout(t);resolve();});});}
 let timer;
 function close(){if(closePromise)return closePromise;send({type:'close'});closed=true;report.closed=true;freshnessListeners.clear();availability?.close();resizePending?.reject(Error('VacooTube closed during resize'));clearInterval(timer);child?.stdio[3].end();closePromise=(async()=>{await Promise.all([stop(audioChild),stop(child,2000)]);audio.clear();try{await sink?.close();}catch(e){report.audioCleanupError=e.message;throw e;}finally{await save();}})();return closePromise;}
 try{
  availabilityFactory??=require('../streaming/lib/frame-availability.cjs').create;
  availability=availabilityFactory(()=>({frame:video.latest,sourceEpoch:report.sourceEpoch,closed:report.closed,hidden:false,suspended:report.captureSuspended,error:report.error}));
  log=await fs.open(path.join(directory,'youtube-host.log'),'a',0o600);
  try{child=launch(runtime.binary,[],{cwd:__dirname,env:{...process.env,...sink?.env,IZ_YOUTUBE_MUTED:sink?'0':'1',IZ_YOUTUBE_MODULE_ROOT:__dirname,IZ_VACOOTUBE_RENDER_HEIGHT:String(profile.height),IZ_YOUTUBE_APP_SOURCE:runtime.app,IZ_YOUTUBE_PROFILE_DIRECTORY:path.join(require('../platform.cjs').home,'data/profile'),...(selfTest?{IZ_YOUTUBE_SELF_TEST:'1'}:{})},stdio:['ignore','pipe',log.fd,'pipe','pipe']});}finally{await log.close();}
  report.pid=child.pid;
  const receive=frameReader(video,report,{accept:frame=>{if(report.captureSuspended||report.reconfiguring||frame.w!==report.profile.width||frame.h!==report.profile.height||frame.timestamped&&frame.requestId!==framePending?.id)return false;report.lastRequestMs=framePending?performance.now()-framePending.at:0;framePending=null;return true;},onFrame:notify});
  child.stdio[4].on('data',b=>{try{receive(b);}catch(e){report.error=e.message;notify();}});
  let lines='';child.stdout.on('data',b=>{lines+=b.toString();if(lines.length>65536)lines=lines.slice(-65536);let i;while((i=lines.indexOf('\n'))>=0){const line=lines.slice(0,i);lines=lines.slice(i+1);if(line.startsWith('IZYOUTUBE '))try{const m=JSON.parse(line.slice(10));if(m.type==='resized'&&m.resizeId===resizePending?.id){resizePending.resolve();}if(m.type==='ready'){report.ready=true;report.hidden=m.hidden;report.offscreen=m.offscreen;report.url=m.url;issueFrame();}if(m.type==='fixture')report.fixture=m.state;if(m.type==='frame-wait'&&(!m.requestId||m.requestId===framePending?.id)){framePending=null;report.frameWaits=(report.frameWaits||0)+1;issueFrame();}if(m.type==='capture-error'){framePending=null;report.error=m.error;notify();}if(m.type==='error')report.inputError=m.error;}catch{}}});
  child.on('error',e=>{report.error=e.message;notify();});child.on('exit',(code,signal)=>{report.exited={code,signal};if(!closed)report.error='YouTube window closed';notify();});child.stdio[3].on('error',e=>{if(!closed){report.error=e.message;notify();}});child.stdio[3].on('drain',issueFrame);
  // Capturing the private monitor does not depend on compositor readiness.
  await startAudio();
  timer=setInterval(()=>{startAudio().catch(()=>{});save().catch(()=>{});},1000);timer.unref();
  const abort=new AbortController(),deadline=setTimeout(()=>abort.abort(),firstFrameTimeoutMs);
  try{if(!await waitFrame({signal:abort.signal}))throw Error('YouTube capture stopped before its first frame');}catch(error){if(error.name==='AbortError')throw Error('VacooTube did not produce a video frame; see youtube-host.log');throw error;}finally{clearTimeout(deadline);}
  await save();return {report,video,audio,send,close,requestFrame,waitFrame,pauseCapture,resumeCapture,onFreshness(listener){freshnessListeners.add(listener);return()=>freshnessListeners.delete(listener);},setProfile,focus:async()=>({focused:true,application:'YouTube'})};
 }catch(error){await close();throw error;}
}
module.exports={PROFILE,frameReader,create};
