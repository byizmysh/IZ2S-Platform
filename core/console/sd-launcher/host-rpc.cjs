'use strict';
// Framework-produced bindings only; neither targets nor effects come from an
// application's claimed capability flag. These bytes are mapped immutable RX.
const assert=require('node:assert/strict');
const MAX=128,PLAN_BYTES=2104,CODE_POOL_BYTES=1048576,BINDING_BYTES=24,THUNK_BYTES=80,LIMIT=2**39,REJECT=0xfdfefdfefdfefd01n;
function address(value){const n=Number(value);assert(Number.isSafeInteger(n)&&n>=4096&&n<LIMIT&&n%4===0,'Invalid host RPC address');return n;}
function plan(targets,{boundedTargets=[],fullWrite,frameMode=0,backing}={}){
 assert(Array.isArray(targets)&&targets.length<=MAX,'Invalid host RPC binding count');assert(frameMode===0||frameMode===1,'Invalid host RPC frame coordination mode');const known=new Set(boundedTargets.map(value=>String(address(value)))),seen=new Set(),out=Buffer.alloc(PLAN_BYTES);out.writeUInt32LE(1);out.writeUInt32LE(PLAN_BYTES,4);out.writeUInt32LE(targets.length,8);
 for(const [i,row]of targets.entries()){const target=address(row.target),effect=row.effect??0;assert((effect===0||effect===1)&&!seen.has(target),'Invalid or duplicate host RPC binding');assert(effect===1||known.has(String(target)),'A bounded RPC target needs a trusted platform binding');seen.add(target);out.writeBigUInt64LE(BigInt(target),16+i*16);out.writeUInt32LE(effect,24+i*16);}
 if(fullWrite){const entry=address(fullWrite.entry),receipt=address(fullWrite.receipt);assert(entry!==receipt);out.writeBigUInt64LE(BigInt(entry),2064);out.writeBigUInt64LE(BigInt(receipt),2072);}out.writeUInt32LE(frameMode,2080);if(backing){assert(fullWrite&&address(backing.base)%4096===0&&backing.bytes===4096);out.writeBigUInt64LE(BigInt(backing.base),2088);out.writeBigUInt64LE(4096n,2096);}return out;
}
function thunks({bindings,epoch,dispatcher,count}){
 bindings=address(bindings);dispatcher=address(dispatcher);assert(bindings%8===0&&Number.isInteger(epoch)&&epoch>0&&epoch<=0xffffffff&&Number.isInteger(count)&&count>=0&&count<=MAX&&bindings+count*BINDING_BYTES<LIMIT,'Invalid host RPC thunk owner');const bytes=Buffer.alloc(count*THUNK_BYTES),offsets=[];
 function literal(at,target){const delta=(target-at)/4;assert(Number.isInteger(delta)&&delta>=0&&delta<0x40000);return (0x58000010|(delta<<5))>>>0;}
 for(let i=0;i<count;i++){
  const at=i*THUNK_BYTES;offsets.push(at);const instructions=[0xd50324df,0xd10083ff,literal(8,48),0xf90003f0,literal(16,56),0xf90007f0,0xf9000bfe,literal(28,64),0xd63f0200,0xf9400bfe,0x910083ff,0xd65f03c0];instructions.forEach((word,j)=>bytes.writeUInt32LE(word,at+j*4));bytes.writeBigUInt64LE(BigInt(bindings+i*BINDING_BYTES),at+48);bytes.writeBigUInt64LE(BigInt(epoch),at+56);bytes.writeBigUInt64LE(BigInt(dispatcher),at+64);
 }
 return {format:'iz2s.host-rpc-thunks/1',bytes,offsets,stride:THUNK_BYTES,bindingStride:BINDING_BYTES,epoch};
}
function packetThunks({owner,epoch,fullWrite,fullReceipt}){
 assert(address(owner)%4096===0,'Packet owner must be page aligned');
 const first=thunks({bindings:owner,epoch,dispatcher:fullWrite,count:1}),second=thunks({bindings:owner,epoch,dispatcher:fullReceipt,count:1});
 return {format:'iz2s.host-rpc-packet-thunks/1',bytes:Buffer.concat([first.bytes,second.bytes]),offsets:{fullWrite:0,fullReceipt:THUNK_BYTES},epoch,owner};
}
module.exports={MAX,PLAN_BYTES,CODE_POOL_BYTES,BINDING_BYTES,THUNK_BYTES,REJECT,plan,thunks,packetThunks};
