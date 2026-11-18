// One active upload and one future frame. Dictionary state advances only on ACK.
function create({zip,base,level=()=>1,networkType=()=> 'unknown'}){
 let next=2,sequence=0,epoch=0,active=null,prepared=null,closed=false,draining=null;
 const slotDrains=new Map();
 const wireInvalidated=()=>Object.assign(Error('A wire-committed delta frame requires a receiver reset'),{code:'FRAME_PIPELINE_INVALIDATED',recoverable:true});
 const cancelled=()=>Object.assign(Error('Prepared delta frame was discarded'),{code:'DELTA_FRAME_DISCARDED',recoverable:true});
 function available(){if(closed)throw Error('Delta prefetch is closed');if(draining)throw Object.assign(Error('Delta prefetch is draining'),{code:'DELTA_PREFETCH_DRAINING',recoverable:true});}
 function shape(bytes,w,h,format){if(!Buffer.isBuffer(bytes)||!bytes.length||![w,h].every(n=>Number.isInteger(n)&&n>0)||!Number.isInteger(format))throw Error('Invalid delta prefetch frame');}
 function record(bytes,w,h,format,expected,seq){return {token:Object.freeze({expected,slot:expected&1,sequence:seq}),bytes,w,h,format,expected,seq,slot:expected&1,epoch,level:level(),networkType:networkType(),result:null,compressionMs:0};}
 function discard(record){
  if(!record)return Promise.resolve();if(record.wireCommitted)throw wireInvalidated();record.cancelled=true;
  // Fence a cancelled slot before a replacement job may reserve it.
  const before=slotDrains.get(record.slot)||Promise.resolve(),pending=before.catch(()=>{}).then(()=>zip.discard(record.slot));
  slotDrains.set(record.slot,pending);pending.then(()=>{if(slotDrains.get(record.slot)===pending)slotDrains.delete(record.slot);},()=>{if(slotDrains.get(record.slot)===pending)slotDrains.delete(record.slot);});return pending;
 }
 async function encode(record,metrics){
  await slotDrains.get(record.slot);
  if(closed||record.cancelled||record.epoch!==epoch)throw cancelled();
  const started=performance.now();
  const result=await zip.encode({frame:record.bytes,slot:record.slot,base,metrics,sequence:record.seq,keyframe:record.seq<=2,level:record.level,networkType:record.networkType});
  record.compressionMs=performance.now()-started;
  if(closed||record.cancelled||record.epoch!==epoch)throw cancelled();
  record.result=result;return result;
 }
 function drain(resetBytes){
  epoch++;prepared=null;active=null;
  const before=draining||Promise.resolve(),pending=before.catch(()=>{}).then(()=>zip.discardAll()).then(()=>{if(resetBytes!==undefined){if(closed)throw Error('Delta prefetch is closed');zip.reset(resetBytes);next=2;sequence=0;}});
  draining=pending;pending.then(()=>{if(draining===pending)draining=null;},()=>{if(draining===pending)draining=null;});return pending;
 }
 async function prepareFrame(bytes,w,h,format){
  available();shape(bytes,w,h,format);if(prepared)throw Error('A delta frame is already prepared');
  const expected=active?(active.expected===2?3:2):next,seq=active?active.seq+1:sequence+1,own=record(bytes,w,h,format,expected,seq);prepared=own;
  try{await encode(own,Buffer.alloc(32));if(prepared!==own||own.epoch!==epoch||closed)throw cancelled();return own.token;}
  catch(error){if(prepared===own&&own.epoch===epoch){prepared=null;await discard(own).catch(()=>{});}throw error;}
 }
 async function acquire(bytes,w,h,format,metrics=Buffer.alloc(32),token){
  available();shape(bytes,w,h,format);if(active)throw Error('A delta frame is already active');if(!Buffer.isBuffer(metrics)||metrics.length!==32)throw Error('Delta metrics must contain 32 bytes');
  // Reserve before the first await: prepareFrame can immediately target the
  // opposite publication slot while this frame is still being compressed.
  const own=record(bytes,w,h,format,next,sequence+1),ticket=own.token;active=own;
  const candidate=prepared;prepared=null;const started=performance.now();
  if(candidate?.wireCommitted){
   const matches=candidate.token===token&&candidate.result&&candidate.epoch===epoch&&candidate.bytes===bytes&&candidate.w===w&&candidate.h===h&&candidate.format===format&&candidate.expected===own.expected&&candidate.seq===own.seq&&candidate.level===own.level&&candidate.networkType===own.networkType;
   if(!matches){active=null;prepared=candidate;throw wireInvalidated();}
   active=candidate;return {ticket:candidate.token,encoded:candidate.result,compressionMs:candidate.compressionMs,compressionWaitMs:performance.now()-started,precompressed:true,wireCommitted:true};
  }
  const precompressed=!!candidate&&candidate.token===token&&candidate.result&&candidate.epoch===epoch&&candidate.bytes===bytes&&candidate.w===w&&candidate.h===h&&candidate.format===format&&candidate.expected===own.expected&&candidate.seq===own.seq&&candidate.level===own.level&&candidate.networkType===own.networkType;
  try{
   let encoded;
   if(precompressed){encoded=candidate.result;own.compressionMs=candidate.compressionMs;}
   else {if(candidate)await discard(candidate);encoded=await encode(own,metrics);}
   if(closed||active!==own||own.epoch!==epoch)throw cancelled();
   if(!Buffer.isBuffer(encoded.packet)||encoded.packet.length<64)throw Error('Invalid delta encoder packet');
   // Compression during prefetch does not own the final frame timing fields.
   metrics.copy(encoded.packet,32);own.result=encoded;
   return {ticket,encoded,compressionMs:own.compressionMs,compressionWaitMs:performance.now()-started,precompressed:!!precompressed};
  }catch(error){if(active===own&&own.epoch===epoch)await drain().catch(cleanup=>{error.cleanupError=cleanup.message;});throw error;}
 }
 function commit(ticket){if(!active||active.token!==ticket||!active.result||active.epoch!==epoch)throw Error('Delta commit does not match the active frame');zip.commit(active.slot);next=active.expected===2?3:2;sequence=active.seq;active=null;}
 function abort(ticket){if(!active||active.token!==ticket)throw Error('Delta abort does not match the active frame');return drain();}
 function discardPrepared(token){if(!prepared||prepared.token!==token||prepared.epoch!==epoch)return Promise.resolve(false);const own=prepared;if(own.wireCommitted)throw wireInvalidated();prepared=null;return discard(own).then(()=>true);}
 function reset(bytes){if(closed)throw Error('Delta prefetch is closed');if(!Number.isInteger(bytes)||bytes<=0)throw Error('Invalid delta frame byte size');return drain(bytes);}
 function close(){closed=true;return drain();}
 function commitPrepared(token,metrics=Buffer.alloc(32)){
  available();if(!active||!prepared||prepared.token!==token||!prepared.result||prepared.epoch!==epoch)return null;
  if(prepared.wireCommitted||prepared.level!==level()||prepared.networkType!==networkType())return null;if(!Buffer.isBuffer(metrics)||metrics.length!==32)throw Error('Delta metrics must contain 32 bytes');
  if(prepared.slot===active.slot||prepared.expected===active.expected||prepared.seq!==active.seq+1)throw Error('Future delta frame does not alternate');
  metrics.copy(prepared.result.packet,32);prepared.wireCommitted=true;return {ticket:prepared.token,encoded:prepared.result};
 }
 function isWireCommitted(token){return !!((prepared&&prepared.token===token&&prepared.wireCommitted)||(active&&active.token===token&&active.wireCommitted));}
 return {prepareFrame,acquire,commit,abort,reset,discardPrepared,close,commitPrepared,isWireCommitted,hasWireCommitted:()=>!!(prepared?.wireCommitted||active?.wireCommitted)};
}
module.exports={create};
