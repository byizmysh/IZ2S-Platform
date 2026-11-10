'use strict';
// SD-only variant of the PS2 large-transfer protocol. No internal mount is
// opened. A failed transport permanently fences this instance from replay.
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const {success,pathRequest,packet}=require('../../filesystem/ouncecake/engine/protocol.cjs');
const sparse=require('../../services/owned-upload.cjs');
const BASE='/sd'+require('../../module-storage.cjs').BASE,MAX_CHUNK=0x70000;
function scoped(value,{parents=false}={}){
 assert.equal(typeof value,'string');assert(!/[\x00-\x1f\x7f\\:]/.test(value));assert(value.split('/').slice(1).every(p=>p&&p!=='.'&&p!=='..'));assert(Buffer.byteLength(value)<769);
 assert(value===BASE||value.startsWith(BASE+'/')||parents&&['/sd','/sd/IZ2S','/sd/IZ2S/Homebrew'].includes(value),'SD launcher path left its module subtree');return value.slice(3)||'/';
}
async function create(attachment){
 const {s,root,domain,filesystem}=attachment;assert.equal(s.bytes,0x80000);assert.equal(s.threads.size,0);
 const sd=domain.oneHandle(success(await s.ipc('Open launcher SD transfer filesystem',root,18),'Open SD'));const address=s.base+0x6000,openFiles=new Map();assert(address+MAX_CHUNK<=s.base+s.bytes);let uncertain=false,queue=Promise.resolve();
 const metrics={kind:'verified-sdk-io-v1',uploadedFileBytes:0,locallyClearedBytes:0,writtenFileBytes:0,readbackFileBytes:0,downloadedFileBytes:0,digestBytes:0};
 function call(fn){const result=queue.then(async()=>{assert(!uncertain,'Previous SD transfer outcome is unknown');try{return await fn();}catch(e){if(e.nativeResult===undefined){uncertain=true;e.resourcesRetained=true;}throw e;}});queue=result.catch(()=>{});return result;}
 async function pathCall(command,input,values){const b=Buffer.alloc(256),original=packet(command,input);b.writeUInt32LE(4|values.length<<16);b.writeUInt32LE(original.readUInt32LE(4),4);
  for(let i=0;i<values.length;i++){const a=s.base+0x2000+i*0x400,buffer=Buffer.alloc(0x301);buffer.write(scoped(values[i],{parents:true}));await s.write(a,buffer);pathRequest(command,input,a).copy(b,8+i*8,8,16);}original.copy(b,(8+values.length*8+15)&~15,16,32+input.length);return b;}
 async function mutate(value,command,input=Buffer.alloc(0),second){scoped(value,{parents:command===2});if(second)scoped(second);return success(await domain.send(s,'Launcher SD change '+command,sd,await pathCall(command,input,second?[value,second]:[value])),'Launcher SD change');}
 async function close(file){assert(openFiles.has(file.handle));success({result:await s.call('close',file.handle,0,0)},'Close SD transfer file');s.handles.delete(file.handle);openFiles.delete(file.handle);}
 return {get uncertain(){return uncertain;},get metrics(){return {...metrics};},filesystem:sd,writableMounts:['sd'],
  list:value=>call(async()=>{scoped(value,{parents:true});const mode=Buffer.alloc(4);mode.writeUInt32LE(3);const dir=domain.oneHandle(success(await filesystem.pathMethod(s,sd,9,scoped(value,{parents:true}),mode),'Open SD directory'));const rows=[];
   try{while(rows.length<=200000){const r=success(await s.ipc('List launcher SD directory',dir,0,Buffer.alloc(0),{receive:[{address,size:32*0x310,mode:0}]}),'List SD');const n=Number(r.data.readBigUInt64LE());assert(Number.isInteger(n)&&n>=0&&n<=32);if(!n)return rows;const data=await filesystem.verifiedBytes(s,address,n*0x310,{directory:value});for(let i=0;i<n;i++){const b=data.subarray(i*0x310,(i+1)*0x310),name=b.subarray(0,0x301).toString().split('\0')[0];assert(name&&!/[\x00-\x1f\x7f/\\]/.test(name)&&!['.','..'].includes(name));assert(b[0x304]<=1);const size=Number(b.readBigUInt64LE(0x308));assert(Number.isSafeInteger(size));rows.push({name,type:b[0x304]===0?'dir':'file',size});}}throw Error('Too many SD directory entries');}
   catch(e){if(e.nativeResult===undefined)uncertain=true;throw e;}finally{if(!uncertain)await s.close(dir);}}),
  mkdir:value=>call(()=>mutate(value,2)),
  openWrite:(value,size)=>call(async()=>{assert(Number.isSafeInteger(size)&&size>0);const input=Buffer.alloc(16);input.writeBigUInt64LE(BigInt(size),8);await mutate(value,0,input);const mode=Buffer.alloc(4);mode.writeUInt32LE(2);const r=await mutate(value,8,mode),file={handle:domain.oneHandle(r),size,path:value,write:true};openFiles.set(file.handle,file);return file;}),
  openRead:value=>call(async()=>{const opened=await filesystem.openFile(s,sd,scoped(value));success({result:opened.result},'Open SD verification file');const file={handle:opened.handle,size:Number(await filesystem.fileSize(s,opened.handle)),path:value};assert(Number.isSafeInteger(file.size)&&file.size>=0);openFiles.set(file.handle,file);return file;}),
  close:file=>call(()=>close(file)),flush:file=>call(async()=>{assert(openFiles.has(file.handle));success(await s.ipc('Flush SD app',file.handle,2),'Flush SD app');}),
  rename:(from,to)=>call(()=>mutate(from,5,Buffer.alloc(0),to)),remove:value=>call(()=>mutate(value,1)),
  commit:()=>call(async()=>{success(await s.ipc('Commit launcher SD',sd,10),'Commit SD');}),
  write:(file,offset,bytes)=>call(async()=>{assert(openFiles.has(file.handle)&&file.write&&Buffer.isBuffer(bytes)&&bytes.length>0&&bytes.length<=MAX_CHUNK&&Number.isSafeInteger(offset)&&offset>=0&&offset+bytes.length<=file.size);const input=Buffer.alloc(24);input.writeBigUInt64LE(BigInt(offset),8);input.writeBigUInt64LE(BigInt(bytes.length),16);
   const plan=sparse.plan(bytes);if(bytes.length-plan.transferredBytes>=sparse.MINIMUM_SAVING){
    // The Session already qualified this SDK memset and owns this idle buffer.
    // Its complete SHA256 is still checked before any IFile write is sent.
    const result=await s.call('memset',address,0,bytes.length);assert.equal(Number(BigInt.asUintN(32,BigInt(result))),Number(BigInt.asUintN(32,BigInt(address))),'SD upload buffer clear failed');metrics.locallyClearedBytes+=bytes.length;
    for(const span of plan.spans){await s.write(address+span.offset,bytes.subarray(span.offset,span.offset+span.bytes));metrics.uploadedFileBytes+=span.bytes;}
   }else{await s.write(address,bytes);metrics.uploadedFileBytes+=bytes.length;}
   assert.equal(await filesystem.nativeHash(s,address,bytes.length),crypto.createHash('sha256').update(bytes).digest('hex'));success(await s.ipc('Write SD app',file.handle,1,input,{send:[{address,size:bytes.length,mode:1}]}),'Write SD app');metrics.writtenFileBytes+=bytes.length;
  }),
  read:(file,offset,count)=>call(async()=>{assert(openFiles.has(file.handle)&&!file.write&&Number.isSafeInteger(offset)&&offset>=0&&Number.isInteger(count)&&count>0&&count<=MAX_CHUNK&&offset+count<=file.size);const input=Buffer.alloc(24);input.writeBigUInt64LE(BigInt(offset),8);input.writeBigUInt64LE(BigInt(count),16);const r=success(await s.ipc('Verify SD app',file.handle,0,input,{receive:[{address,size:count,mode:1}]}),'Read SD app');assert.equal(Number(r.data.readBigUInt64LE()),count,'Short SD read');const bytes=await filesystem.verifiedBytes(s,address,count,{path:file.path,offset});metrics.readbackFileBytes+=count;metrics.downloadedFileBytes+=count;return bytes;}),
  readHash:(file,offset,count)=>call(async()=>{assert(openFiles.has(file.handle)&&!file.write&&Number.isSafeInteger(offset)&&offset>=0&&Number.isInteger(count)&&count>0&&count<=MAX_CHUNK&&offset+count<=file.size);const input=Buffer.alloc(24);input.writeBigUInt64LE(BigInt(offset),8);input.writeBigUInt64LE(BigInt(count),16);const r=success(await s.ipc('Verify SD app digest',file.handle,0,input,{receive:[{address,size:count,mode:1}]}),'Read SD app digest');assert.equal(Number(r.data.readBigUInt64LE()),count,'Short SD read');const digest=await filesystem.nativeHash(s,address,count);assert(/^[a-f0-9]{64}$/.test(digest),'Invalid console SD digest');metrics.readbackFileBytes+=count;metrics.digestBytes+=32;return digest;}),
 };
}
module.exports={create,scoped,BASE,MAX_CHUNK};
