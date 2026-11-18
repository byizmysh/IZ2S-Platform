// Read input independently of video encoding, transfer and GPU frame ACKs.
// One bounded read at a time; no accumulating timer/request queue.
const {BYTES}=require('./platform.cjs').require('core/services/controller-packet.cjs');
function start({rpc,mapped,header,onInput,onError=()=>{},hz=60,now=performance.now.bind(performance)}){
 let stopped=false,paused=false,timer,wake,epoch=0,lastAck=-Infinity,latest;
 // Frame ACKs already carry fresh input. Tolerate normal frame pacing/jitter
 // before falling back to RPC; a 60 Hz timer must not compete with PCM refills.
 const interval=1000/hz,ackGrace=Math.max(interval+2,100),report={targetHz:hz,samples:0,ackSamples:0,suppressedReads:0,staleStates:0,lastReadMs:0,maxReadMs:0};
 const spans=[{address:mapped+128,length:56},{address:mapped+0x7f0280,length:320},{address:mapped+0x700004,length:4},{address:mapped+header+96,length:8}];
 const wait=delay=>new Promise(resolve=>{wake=resolve;timer=setTimeout(resolve,delay);});
 function deliver(packet){
  if(stopped||packet.length<BYTES)return;
  const input=Buffer.from(packet);
  // ACK and RPC snapshots can overtake each other. Each Npad and touch state
  // carries its own monotonic HID sampling number; never replay an older hold
  // after a newer release. Menu/pause metadata remains immediately effective.
  if(latest)for(const [at,length]of [[8,64],[72,64],[136,64],[200,64],[264,64],[340,56]]){
   if(input.readBigUInt64LE(at)<latest.readBigUInt64LE(at)){latest.copy(input,at,at,at+length);report.staleStates++;}
  }
  latest=input;onInput(input);
 }
 const done=(async()=>{while(!stopped){const began=now(),own=epoch;
  if(!paused&&began-lastAck<ackGrace){report.suppressedReads++;await wait(Math.max(1,ackGrace-(began-lastAck)));wake=null;continue;}
  try{if(!paused){const parts=rpc.readMany?await rpc.readMany(spans):await Promise.all(spans.map(s=>rpc.read(s.address,s.length)));
   if(stopped||paused||own!==epoch)continue;const packet=Buffer.alloc(BYTES);parts[0].copy(packet,340);parts[1].copy(packet,8);parts[2].copy(packet,328);parts[3].copy(packet,332);
   report.samples++;report.lastReadMs=+(now()-began).toFixed(2);report.maxReadMs=Math.max(report.maxReadMs,report.lastReadMs);deliver(packet);
  }}catch(error){if(!stopped&&own===epoch){report.error=error.message;onError(error);}}
  if(stopped)break;const delay=paused?50:Math.max(1,interval-(now()-began));await wait(delay);wake=null;
 }})();
 return {report,noteAck(packet){if(stopped)return;lastAck=now();report.ackSamples++;deliver(packet);clearTimeout(timer);wake?.();},pause(value){if(paused===!!value)return;paused=!!value;epoch++;if(paused)latest=null;else lastAck=-Infinity;clearTimeout(timer);wake?.();},close(){stopped=true;epoch++;clearTimeout(timer);wake?.();},done};
}
module.exports={start};
