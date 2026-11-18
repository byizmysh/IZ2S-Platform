const {parentPort,workerData}=require('node:worker_threads'),D=require('./block-delta.cjs');
let codec=null;try{codec=require('./fast-compression.cjs').load(workerData?.codecPath);}catch{}
let slots=[],pending=new Map();
parentPort.on('message',m=>{
 try{
  if(m.command==='reset'){D.layout(m.bytes);slots=[Buffer.alloc(m.bytes),Buffer.alloc(m.bytes)];pending.clear();return;}
  if(m.command==='commit'){
   const prepared=pending.get(m.slot);if(!prepared)throw Error('Delta dictionary commit differs');
   slots[m.slot]=prepared;pending.delete(m.slot);return;
  }
  if(m.command==='discard'){pending.delete(m.slot);return;}
  if(m.command==='discard-all'){pending.clear();return;}
  if(pending.has(m.slot))throw Error('Previous delta frame for this slot has not been acknowledged');
  if(m.blockBytes!==D.BLOCK)throw Error('Restart the launcher session: compression protocol changed');
  if(m.slot!==0&&m.slot!==1)throw Error('Invalid dictionary slot');
  const frame=Buffer.from(m.frame.buffer,m.frame.byteOffset,m.frame.byteLength),before=slots[m.slot];
  if(!before||before.length!==frame.length)throw Error('Delta dictionary not initialized');
  // Low-latency level 1 must not spend an entire frame budget searching 33
  // independent temporal dictionaries. Strong matches retain their savings;
  // moving blocks use the native compressor, then stored DEFLATE if needed.
  // The deadline bounds optional search, not OS scheduling or native calls.
  // 31 words traverses BC1 endpoint/index positions instead of sampling only
  // one position every 16 blocks, which aliases periodic scenery/UI patterns.
  const began=performance.now(),r=D.encode(frame,before,{base:m.base,metrics:Buffer.from(m.metrics),sequence:m.sequence,keyframe:m.keyframe,level:m.level,codec,...(m.level===1?D.latencyPolicy(m.networkType):{})});
  r.workerEncodingMs=performance.now()-began;
  pending.set(m.slot,frame);
  // Large packets already own their ArrayBuffer. Transfer it directly rather
  // than copying a full HD frame twice across the worker boundary. Tiny pooled
  // Buffers still need an independent transferable allocation.
  const packet=r.packet.byteOffset===0&&r.packet.buffer.byteLength===r.packet.byteLength?r.packet:Uint8Array.from(r.packet);
  parentPort.postMessage({id:m.id,...r,packet},[packet.buffer]);
 }catch(e){parentPort.postMessage({id:m.id,error:e.message});}
});
