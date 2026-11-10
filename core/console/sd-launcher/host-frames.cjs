'use strict';
const assert=require('node:assert/strict');
const BYTES=168,SHARED=8388608,LIMIT=2**39;
function address(value){const n=Number(value);assert(Number.isSafeInteger(n)&&n>=4096&&n<LIMIT,'Invalid host frame storage address');return n;}
function plan({id,controls,commands,imageCount,menuControls,supervisorRange,menuCommands=[]}){
 assert(typeof id==='string'&&/^[a-z0-9][a-z0-9-]{0,47}$/.test(id),'Invalid host frame application identity');
 const mapped=address(controls?.mapped);assert(mapped%4096===0&&controls.bytes===SHARED&&mapped+SHARED<=LIMIT,'Host frames require a separate 8 MiB control allocation');
 function disjoint(base,bytes){base=address(base);assert(Number.isSafeInteger(bytes)&&bytes>0&&base+bytes<=LIMIT,'Invalid protected host frame range');assert(mapped+SHARED<=base||mapped>=base+bytes,'Host frame controls overlap the menu or supervisor');}
 disjoint(menuControls,SHARED);assert(supervisorRange,'Missing supervisor allocation proof');disjoint(supervisorRange.base,supervisorRange.bytes);
 assert(Number.isInteger(imageCount)&&imageCount>=1&&imageCount<=8&&Array.isArray(commands)&&commands.length===imageCount,'Host frame swapchain command count differs');
 const handles=commands.map(value=>{let n;try{n=BigInt(value);}catch{assert.fail('Invalid host frame command');}assert(n>0n&&n<=0xffffffffffffffffn,'Invalid host frame command');return n;});
 assert(new Set(handles.map(String)).size===handles.length&&!handles.some(handle=>menuCommands.some(menu=>BigInt(menu)===handle)),'Host frames must use distinct app command buffers');
 const out=Buffer.alloc(BYTES);out.writeUInt32LE(1);out.writeUInt32LE(BYTES,4);out.writeBigUInt64LE(BigInt(mapped),8);out.writeBigUInt64LE(BigInt(SHARED),16);handles.forEach((handle,i)=>out.writeBigUInt64LE(handle,24+i*8));out.write(id,88,64);return out;
}
async function verifyControls(rpc,controls){
 const begin=address(controls.mapped),end=begin+SHARED;assert(typeof rpc.query==='function','Host frame storage needs native RW ownership checks');
 for(let at=begin;at<end;){const region=await rpc.query(at),base=Number(region.address),size=Number(region.size),limit=base+size;
  assert(Number.isSafeInteger(base)&&Number.isSafeInteger(size)&&size>0&&base<=at&&Number.isSafeInteger(limit)&&limit>at&&region.permissions===3&&!region.ipcReferences,'Host frame control allocation is not ordinary writable memory');at=limit;
 }
}
module.exports={BYTES,SHARED,plan,verifyControls};
