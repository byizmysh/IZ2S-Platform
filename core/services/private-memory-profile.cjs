'use strict';
// Explicitly bounded logical allocation profiles. Driver availability is still
// checked by ordinary Vulkan allocation; this is not a memory availability claim.
const STANDARD_BYTES=32*1024*1024,RESIDENT_AUDIO_BYTES=128*1024*1024;
function limit(memory){
 if(memory?.profile===undefined)return STANDARD_BYTES;
 if(memory.profile==='resident-audio-128')return RESIDENT_AUDIO_BYTES;
 throw Error('Unsupported private GPU memory profile');
}
function validate(memory){
 const maximum=limit(memory);
 if(!memory||!Number.isInteger(memory.bytes)||memory.bytes<4096||memory.bytes>maximum||memory.bytes%4096)
  throw Error('Invalid private GPU memory size');
 return maximum;
}
function allocationFits(memory,size){
 validate(memory);
 return typeof size==='bigint'&&size>=BigInt(memory.bytes)&&size<=BigInt(limit(memory));
}
module.exports={limit,validate,allocationFits,STANDARD_BYTES,RESIDENT_AUDIO_BYTES};
