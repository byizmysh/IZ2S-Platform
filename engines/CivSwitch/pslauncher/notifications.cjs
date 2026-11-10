// A fixed, isolated mailbox in the menu font gap. No console filesystem access.
const MAGIC=0x495a4e31,HEADER=1024+0x1f0000,SLOT=1024+0x1c0000,SLOT_BYTES=131072,SECONDS=4;
function create({rpc,mapped,clock=Date.now}){
 let queue=[],current=null,sequence=0;
 function enqueue({id,name,bytes}){if(!Buffer.isBuffer(bytes)||bytes.length!==SLOT_BYTES)throw Error('Invalid notification raster');queue.push({id,name,bytes:Buffer.from(bytes)});}
 async function capture(){if(!current)return;const header=await rpc.read(mapped+HEADER,32);if(header.readUInt32LE()===MAGIC&&header.readUInt32LE(4)===current.sequence){current.header=header;current.captured=clock();}else if(clock()-current.started>SECONDS*1000)current=null;}
 async function pending(){if(!queue.length)return false;if(!current)return true;if(clock()-current.started<3800)return false;const h=await rpc.read(mapped+HEADER,32);return h.readUInt32LE()!==MAGIC||h.readFloatLE(16)<=0;}
 async function publish(){
  if(current&&current.header.readFloatLE(16)<=0)current=null;
  // A separate app may have owned the storage in between. Do not resurrect stale popups.
  if(current&&clock()-current.captured>SECONDS*1000)current=null;
  if(!current&&queue.length){const entry=queue.shift(),header=Buffer.alloc(32);sequence=sequence>=0xfffffffe?1:sequence+1;header.writeUInt32LE(MAGIC);header.writeUInt32LE(sequence,4);header.writeFloatLE(SECONDS,16);current={...entry,header,sequence,started:clock(),captured:clock()};}
  if(current){await rpc.write(mapped+SLOT,current.bytes);await rpc.write(mapped+HEADER,current.header);}else await rpc.write(mapped+HEADER,Buffer.alloc(32));
 }
 return {enqueue,capture,pending,publish,get report(){return {current:current?.name||null,queued:queue.length,seconds:SECONDS};}};
}
module.exports={create,MAGIC,HEADER,SLOT,SLOT_BYTES,SECONDS};
