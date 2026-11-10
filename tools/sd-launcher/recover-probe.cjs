'use strict';
// Reconciles only a completed write probe whose host lost its cleanup closure.
// No worker or IPC operation is replayed; original attachment storage remains
// retained because its complete ownership list was not in the old receipt.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
async function main(file){
 file=path.resolve(file);const root=path.resolve(__dirname,'../..');assert(file.startsWith(path.join(root,'data/sessions')+path.sep));const directory=path.dirname(file),r=JSON.parse(await fs.readFile(file));assert(r.probe&&r.startedThread&&!r.joined&&r.session===path.basename(directory));
 const manifest=JSON.parse(await fs.readFile(path.join(root,'core/console/sd-launcher/native/build/manifest.json'))),rpc=require(path.join(directory,'shared/rpc.cjs')),L=manifest.layout;
 assert.equal(rpc.a.session,r.session);const b=await rpc.read(r.base+0x1000,L.bytes);assert.equal(b.readUInt32LE(L.state),3);assert.equal(b.readUInt32LE(L.poisoned),0);assert.equal(b.readUInt32LE(L.probe_result),0);assert.equal(b.readBigUInt64LE(L.opened),b.readBigUInt64LE(L.closed));
 const thread=require('../../core/console/abi/thread-code.json').functions;assert.equal((await rpc.callAddress(rpc.a.sdk+thread.tryWait.offset,[r.thread])).result,1n);assert.equal((await rpc.callAddress(rpc.a.sdk+thread.join.offset,[r.thread,r.base+0x808])).result,0n);r.joined=true;
 const a=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));const work=new a.Session('sd-probe-recovery',r.bytes);work.base=r.base;work.rpc=a.s.rpc;work.record=async()=>{};const ro=await a.service('ro:dmnt');const {success}=require('../../core/filesystem/ouncecake/engine/protocol.cjs');
 for(const m of [...r.mappings].reverse()){assert(m.source>=r.base+0x10000&&m.source<r.base+0x10000+Math.ceil(r.bootstrapBytes/4096)*4096);const input=Buffer.alloc(32);[r.pid,m.source,m.destination,4096].forEach((v,i)=>input.writeBigUInt64LE(BigInt(v),i*8));success(await work.ipc('Unmap completed SD probe',ro,0x524f0013,input,{pid:true,copies:[0xffff8001]}),'Unmap probe');m.unmapped=true;}
 const close=require(path.join(directory,'fs-rpc/shared/sdk-functions.json')).close;for(const h of [r.sdHandle,b.readUInt32LE(L.ro_handle)])assert.equal((await rpc.callAddress(rpc.a.sdk+close.offset,[h])).result,0n);
 await work.cleanup();await a.close();r.allocationsRetained=false;r.originalAttachmentRetained=true;r.recovered=new Date().toISOString();r.observed={state:3,probe_result:0,opened:Number(b.readBigUInt64LE(L.opened)),closed:Number(b.readBigUInt64LE(L.closed)),probe_crc:b.readUInt32LE(L.probe_crc)};await fs.writeFile(file,JSON.stringify(r,null,2)+'\n');console.log(JSON.stringify({probePassed:true,workerJoined:true,codeUnmapped:true,workFreed:true,originalAttachmentRetained:true}));
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
