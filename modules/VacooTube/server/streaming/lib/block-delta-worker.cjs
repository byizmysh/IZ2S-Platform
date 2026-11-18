const {parentPort}=require('node:worker_threads'),D=require('./block-delta.cjs');
let slots=[],pending;
parentPort.on('message',m=>{
 try{
  if(m.command==='reset'){slots=[Buffer.alloc(m.bytes),Buffer.alloc(m.bytes)];pending=null;return;}
  if(m.command==='commit'){
   if(!pending||pending.slot!==m.slot)throw Error('Delta dictionary commit differs');
   slots[m.slot]=pending.frame;pending=null;return;
  }
  if(pending)throw Error('Previous delta frame has not been acknowledged');
  if(m.blockBytes!==D.BLOCK)throw Error('Restart the launcher session: compression protocol changed');
  if(m.slot!==0&&m.slot!==1)throw Error('Invalid dictionary slot');
  const frame=Buffer.from(m.frame.buffer,m.frame.byteOffset,m.frame.byteLength),before=slots[m.slot];
  if(!before||before.length!==frame.length)throw Error('Delta dictionary not initialized');
  const began=performance.now(),r=D.encode(frame,before,{base:m.base,metrics:Buffer.from(m.metrics),sequence:m.sequence,keyframe:m.keyframe,level:m.level});
  r.workerEncodingMs=performance.now()-began;
  pending={slot:m.slot,frame};
  // Large packets already own their ArrayBuffer. Transfer it directly rather
  // than copying a full HD frame twice across the worker boundary. Tiny pooled
  // Buffers still need an independent transferable allocation.
  const packet=r.packet.byteOffset===0&&r.packet.buffer.byteLength===r.packet.byteLength?r.packet:Uint8Array.from(r.packet);
  parentPort.postMessage({id:m.id,...r,packet},[packet.buffer]);
 }catch(e){parentPort.postMessage({id:m.id,error:e.message});}
});
