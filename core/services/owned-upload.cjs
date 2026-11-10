'use strict';
// Restore caller-owned menu bytes at a joined renderer boundary. A qualified
// memset clears padding locally; only nonzero runs cross the PC/console link.
const owned=require('./owned-buffer.cjs'),progress=require('../packages/progress.cjs');
const BLOCK=16384,MINIMUM_SAVING=65536;
function plan(bytes){
 if(!Buffer.isBuffer(bytes)||!bytes.length||bytes.length>32*1024*1024)throw Error('Invalid owned upload image');
 const zero=Buffer.alloc(BLOCK),spans=[];let transferredBytes=0;
 for(let at=0;at<bytes.length;at+=BLOCK){const size=Math.min(BLOCK,bytes.length-at);if(bytes.subarray(at,at+size).equals(zero.subarray(0,size)))continue;const last=spans.at(-1);if(last&&last.offset+last.bytes===at)last.bytes+=size;else spans.push({offset:at,bytes:size});}
 // Account for the unchanged engine's 32 KiB exchanges and one guarded
 // request per 512 KiB. Bridge a zero gap only when it removes an exchange.
 const cost=n=>Math.ceil(n/32768)+Math.ceil(n/524288),merged=[];
 for(const span of spans){const last=merged.at(-1),size=last?span.offset+span.bytes-last.offset:0,gap=last?span.offset-last.offset-last.bytes:Infinity;if(last&&gap<=32768&&cost(size)<cost(last.bytes)+cost(span.bytes))last.bytes=size;else merged.push({...span});}
 transferredBytes=merged.reduce((n,span)=>n+span.bytes,0);
 return {bytes:bytes.length,transferredBytes,spans:merged};
}
async function upload(rpc,address,bytes,notify){
 const next=plan(bytes),qualified=rpc.a?.sdk&&rpc.callAddress&&rpc.query;
 if(!qualified||bytes.length-next.transferredBytes<MINIMUM_SAVING){await progress.upload(rpc,address,bytes,notify);return {bytes:bytes.length,transferredBytes:bytes.length,locallyCleared:false};}
 await owned.initialize(rpc,address,bytes.length);
 let completedBytes=0;if(notify)await notify({completedBytes,totalBytes:next.transferredBytes});
 for(const span of next.spans)for(let at=span.offset;at<span.offset+span.bytes;at+=notify?262144:span.bytes){const part=bytes.subarray(at,Math.min(span.offset+span.bytes,at+(notify?262144:span.bytes)));await rpc.write(address+at,part);completedBytes+=part.length;if(notify)await notify({completedBytes,totalBytes:next.transferredBytes});}
 return {bytes:bytes.length,transferredBytes:completedBytes,locallyCleared:true};
}
module.exports={plan,upload,BLOCK,MINIMUM_SAVING};
