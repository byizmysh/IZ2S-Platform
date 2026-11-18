'use strict';
// Runs in the original native host with its unchanged NativeClient. All views
// share a queue and sequence; no mutation is retried after an unknown result.
const assert=require('node:assert/strict');
const MAGIC=0x495a5250,HEADER=192,CAPACITY=32768,DATA=CAPACITY-HEADER,MAX=64*1024*1024,LIMIT=2**39,MAX_ID=0x00ffffffffffffffn;
const OP={CALL:1,WRITE:2,FILL:3,WRITE_CALL:4,PAIR:5,COPY:6,HAPTICS:7,UDP:8,READ_FILE:9},STATUS={OK:0,REVOKED:1,INVALID:2,STALE:3,UNCERTAIN:4};
const hosts=new WeakMap();
function u64(value){
 if(typeof value==='number')assert(Number.isSafeInteger(value),'Use exact native integers');
 assert(typeof value==='number'||typeof value==='bigint'||typeof value==='string'&&/^(0|[1-9][0-9]*)$/.test(value),'Invalid native argument');
 const n=BigInt(value);assert(n>=0n&&n<=0xffffffffffffffffn,'Native argument exceeds uint64');return n;
}
function span(address,n){assert(Number.isSafeInteger(address)&&address>=4096&&Number.isSafeInteger(n)&&n>=0&&address+n<LIMIT,'Invalid guarded memory span');}
function args(list,limit=4){assert(Array.isArray(list)&&list.length<=limit,'Invalid guarded call argument count');return list.map(u64);}
function encode({op,id,destination=0,source=0,length=0,first=0,second=0,value=0,args:list=[],otherArgs=[]},data=Buffer.alloc(0)){
 assert(Object.values(OP).includes(op)&&Buffer.isBuffer(data)&&data.length<=DATA,'Invalid guarded packet operation/data');
 assert(Number.isInteger(length)&&length>=0&&length<=0xffffffff);assert([first,second,value].every(n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff));args(list,8);args(otherArgs,8);
 const bytes=Buffer.alloc(HEADER+data.length);bytes.writeUInt32LE(MAGIC);bytes.writeUInt32LE(1,4);bytes.writeUInt32LE(bytes.length,8);bytes.writeUInt32LE(op,12);
 bytes.writeBigUInt64LE(u64(id),16);bytes.writeBigUInt64LE(u64(destination),24);bytes.writeBigUInt64LE(u64(source),32);
 for(const [at,n]of [[40,length],[44,first],[48,second],[52,value]])bytes.writeUInt32LE(n,at);
 for(const [i,row]of [list,otherArgs].entries())for(const [j,n]of row.entries())bytes.writeBigUInt64LE(u64(n),64+i*64+j*8);
 data.copy(bytes,HEADER);return bytes;
}
async function create({native,owner,epoch,targets,packet,receipt,fullWrite,fullReceipt,addresses=native?.addresses,verifyExtended,execute,sdkProfile}){
 assert(native&&typeof native.exchange==='function'&&addresses?.sdk&&addresses?.scratch);span(owner,4096);span(addresses.scratch,0x80000);
 assert(owner%4096===0&&Number.isInteger(epoch)&&epoch>0&&epoch<=0xffffffff);assert(Array.isArray(targets)&&targets.length<=128);
 let host=hosts.get(native);if(!host){host={tail:Promise.resolve(),owners:new Map()};hosts.set(native,host);}
 const key=owner+':'+epoch;let generation=host.owners.get(key);if(!generation){generation={sequence:0n,closed:false,uncertain:null};host.owners.set(key,generation);}
 function retained(error){generation.uncertain||=error;return Object.assign(Error('Guarded native completion is unknown; resources retained',{cause:error}),{code:'IZ2S_RPC_PACKET_UNCERTAIN',resourcesRetained:true,generation:epoch,requestReplayed:false});}
 function available(){if(generation.uncertain)throw retained(generation.uncertain);if(generation.closed)throw Object.assign(Error('The console revoked this application generation'),{code:'IZ2S_RPC_GENERATION_REVOKED',generation:epoch});}
 let exposed=false;assert(execute===undefined||typeof execute==='function');
 function hostClosed(error){generation.closed=true;return Object.assign(Error('The host revoked this application generation',{cause:error}),{code:'IZ2S_RPC_GENERATION_REVOKED',generation:epoch,nativeRequestSent:false,requestReplayed:false});}
 function serialize(action){const task=host.tail.then(()=>{available();return exposed&&execute?execute(action):action();}).catch(error=>{if(error.code==='IZ2S_HOST_DISPATCH_REVOKED'&&error.nativeRequestSent===false&&error.generationRevoked===true)throw hostClosed(error);throw error;});host.tail=task.catch(()=>{});return task;}
 const helpers=[packet,receipt,fullWrite,fullReceipt];assert(helpers.every(f=>Number.isSafeInteger(f?.address)&&Buffer.isBuffer(f.expected)),'All four native packet entries need verified instructions');
 assert(new Set(helpers.map(f=>f.address)).size===helpers.length,'Guarded packet entries must be distinct');
 await serialize(async()=>{for(const f of helpers)await native.verifyFunction(f.address,f.expected);});
 const indexes=new Map();targets.forEach((target,index)=>{span(target,4);assert(target%4===0&&!indexes.has(target));indexes.set(target,index);});let extended;
 const sdkSignatures=sdkProfile?require('../host/sdk-profile.cjs').signatures(sdkProfile,native,epoch):new Map();
 // Establish worker proof before exposing any app call. A late QueryMemory
 // would overwrite SDK output which the next target may consume.
 if(verifyExtended)await serialize(async()=>{try{extended=await verifyExtended();assert(extended?.stackBytes>=4*1024*1024&&extended?.sdk===addresses.sdk&&extended?.scratch===addresses.scratch&&extended?.guarded===true&&extended?.nativeChecks===true&&extended?.currentSDKThread===true,'Extended call worker proof differs');}catch(error){if(error.resourcesRetained)throw retained(error);throw error;}});
 function target(address){assert(Number.isSafeInteger(address)&&indexes.has(address),'Native target is not registered for this generation');return indexes.get(address);}
 function sdkTarget(offset){assert(Number.isInteger(offset)&&offset>=0&&offset<0x5e1000&&offset%4===0);return target(addresses.sdk+offset);}
 function next(){available();assert(generation.sequence<MAX_ID,'Guarded command sequence exhausted');return ++generation.sequence;}
 async function exchange(calls,data){try{return await native.exchange(calls,data);}catch(error){if(error.code==='IZ2S_HOST_DISPATCH_REVOKED'&&error.nativeRequestSent===false){if(error.generationRevoked===true)throw hostClosed(error);throw error;}throw retained(error);}}
 function completed(reply,id,op){
  const status=reply?.results?.[0];if(typeof status!=='bigint'||status<0n||status>4n||typeof reply.results[1]!=='bigint'||!Buffer.isBuffer(reply.output))throw retained(Error('Invalid native packet reply'));
  if(status!==0n){
   if(status===4n){let message='Native operation completion is unverified';if(op===OP.HAPTICS&&reply.output.length>=16&&reply.output.readUInt32LE()===0x495a4850)message='Native haptic stop is unverified (SDK result 0x'+reply.output.readUInt32LE(12).toString(16)+')';if(op===OP.UDP&&reply.output.length>=32&&reply.output.readUInt32LE()===0x495a5544)message='Native UDP socket closure is unverified (SDK result 0x'+reply.output.readUInt32LE(16).toString(16)+')';throw retained(Error(message));}
   if(status===1n)generation.closed=true;
   if(status===3n)throw retained(Error('The native dispatcher refused a reused command identifier'));
   throw Object.assign(Error(status===1n?'The console revoked this application generation':'The console rejected a guarded command'),{code:status===1n?'IZ2S_RPC_GENERATION_REVOKED':'IZ2S_RPC_PACKET_REJECTED',nativeStatus:Number(status),generation:epoch,commandId:id.toString(),requestReplayed:false});
  }
  return {result:reply.results[1],output:Buffer.from(reply.output),id};
 }
 async function command(fields,data){
  const id=next(),bytes=encode({...fields,id},data);
  return completed(await exchange([{address:packet.address,args:[owner,epoch,addresses.scratch+0x40080,bytes.length]},{address:receipt.address,args:[owner,epoch,id,0]}],bytes),id,fields.op);
 }
 async function writePacket(address,bytes,index=255,list=[]){
  const id=next(),tag=(BigInt(index)<<56n)|id,padded=[...list,...Array(4-list.length).fill(0n)];
  return completed(await exchange([{address:fullWrite.address,args:[address,addresses.scratch+0x40080,bytes.length,tag]},{address:fullReceipt.address,args:padded}],bytes),id);
 }
 function result(reply){return {result:reply.result,output:reply.output};}
 const api={a:addresses,output:native.output,generationGuarded:true,nativeArgumentLimit:extended?8:4,
  get uncertain(){return !!generation.uncertain;},
  async read(address,n){span(address,n);assert(n<=MAX);return serialize(()=>native.read(address,n));},
  async readMany(spans){assert(Array.isArray(spans));for(const row of spans)span(row.address,row.length);return serialize(()=>native.readMany(spans));},
  async query(address){span(address,1);return serialize(()=>native.query(address));},
  async write(address,bytes){
   assert(Buffer.isBuffer(bytes)&&bytes.length<=MAX);span(address,bytes.length);
   return serialize(async()=>{if(!bytes.length)return;await native.guard(address,bytes.length,2);for(let at=0;at<bytes.length;at+=CAPACITY){const part=bytes.subarray(at,at+CAPACITY),reply=await writePacket(address+at,part);if(reply.result!==BigInt(address+at))throw retained(Error('Guarded write receipt differs'));}});
  },
  async fill(address,value,n){span(address,n);assert(n<=0xffffffff);const byte=Number(u64(value)&255n);return serialize(async()=>{if(n)await native.guard(address,n,2);return result(await command({op:OP.FILL,destination:address,length:n,value:byte}));});},
  async copy(destination,source,n){span(destination,n);span(source,n);assert(n<=0xffffffff);return serialize(async()=>{if(n){await native.guard(destination,n,2);await native.guard(source,n,1);}return result(await command({op:OP.COPY,destination,source,length:n}));});},
  async callAddress(address,list=[]){const first=target(address),values=args(list);assert(native.functions.has(address),'Native function has not been verified');return serialize(async()=>result(await command({op:OP.CALL,first,args:values})));},
  async call(offset,list=[]){
   const values=args(list);assert(Number.isInteger(offset)&&offset>=0&&offset<0x5e1000&&offset%4===0);
   // Preserve SDK output: do not insert QueryMemory before an ordinary call.
   if(offset===5797280&&values.length===3){const destination=Number(values[0]),length=Number(values[2]);span(destination,length);assert(length<=0xffffffff);return serialize(async()=>result(await command({op:OP.FILL,destination,length,value:Number(values[1]&255n)})));}
   if([0x586fb0,0x5872f0].includes(offset)&&values.length===3){const [destination,source,length]=values.map(Number);span(destination,length);span(source,length);assert(length<=0xffffffff);return serialize(async()=>result(await command({op:OP.COPY,destination,source,length})));}
   const first=sdkTarget(offset);return serialize(async()=>result(await command({op:OP.CALL,first,args:values})));
  },
  async callExtended(address,list=[]){
   const first=target(address),values=args(list,8);assert(native.functions.has(address),'Native function has not been verified');
   if(values.length>4)assert(extended,'Extended native calls require an independently verified worker stack');
   return serialize(async()=>result(await command({op:OP.CALL,first,args:values})));
  },
  async haptics({handles,steps}){
   assert(Array.isArray(handles)&&handles.length>=1&&handles.length<=2&&new Set(handles).size===handles.length&&handles.every(n=>Number.isInteger(n)&&n>=0&&n<=0xffffffff),'Invalid native vibration handles');
   const normalized=require('../host/haptics.cjs').validate({side:2,steps}).steps,hid=require('../abi/hid-functions.json'),send=addresses.sdk+hid.vibrationSend.offset,read=addresses.sdk+hid.vibrationRead.offset,first=target(send),second=target(read);assert(native.functions.has(send)&&native.functions.has(read),'Vibration SDK functions are not verified');
   const data=Buffer.alloc(normalized.length*20);normalized.forEach((step,i)=>{data.writeUInt32LE(step.milliseconds,i*20);[step.lowAmplitude,step.lowFrequency,step.highAmplitude,step.highFrequency].forEach((value,j)=>data.writeFloatLE(value,i*20+4+j*4));});
   return serialize(async()=>{const reply=await command({op:OP.HAPTICS,first,second,length:normalized.length,value:handles.length,args:handles},data),out=reply.output;
    if(![0n,3n].includes(reply.result)||out.length<48||out.readUInt32LE()!==0x495a4850||out.readUInt32LE(4)!==1||out.readUInt32LE(8)>normalized.length)throw retained(Error('Native haptic receipt differs'));
    return {result:reply.result,nativeResult:out.readUInt32LE(12),completedSteps:out.readUInt32LE(8),actual:Buffer.from(out.subarray(16,32)),afterStop:Buffer.from(out.subarray(32,48))};});
  },
  async udp(destination,payload,{timeout=1000}={}){
   const peer=require('../host/network.cjs').endpoint(destination);assert(Buffer.isBuffer(payload)&&payload.length>=1&&payload.length<=1024,'UDP payload must be 1..1024 bytes');assert(Number.isInteger(timeout)&&timeout>=1&&timeout<=5000,'UDP timeout must be 1..5000ms');
   for(const name of ['socketInitialized','socket','sendto','recv','socketClose','errno','serviceTick']){const row=require('../host/sdk-profile.cjs').definitions.find(row=>row.name===name),address=addresses.sdk+row.offset;target(address);assert(native.functions.has(address),'UDP SDK function is not verified');}
   const ip=peer.address.split('.').reduce((n,part)=>n*256+Number(part),0),data=Buffer.from(payload);
   return serialize(async()=>{const reply=await command({op:OP.UDP,length:data.length,value:timeout,args:[ip,peer.port]},data),out=reply.output;
    if(![0n,3n,4n].includes(reply.result)||out.length<32||out.readUInt32LE()!==0x495a5544||out.readUInt32LE(4)!==1||out.readUInt32LE(8)>1024||out.readUInt32LE(16)!==0||![0,3].includes(out.readUInt32LE(24))||out.readUInt32LE(28)!==Number(reply.result)||reply.result!==0n&&out.readUInt32LE(8)!==0||reply.result===0n&&out.readUInt32LE(24)!==3)throw retained(Error('Native UDP receipt differs'));
    return {result:reply.result,nativeResult:out.readUInt32LE(12),polls:out.readUInt32LE(20),socketClosed:out.readUInt32LE(24)===3,data:Buffer.from(out.subarray(32,32+out.readUInt32LE(8)))};});
  },
  async readFile(path,offset=0,length=require('../host/files.cjs').MAX){
   require('../host/files.cjs').validate(path,offset,length);const data=Buffer.from(path+'\0');
   return serialize(async()=>{const reply=await command({op:OP.READ_FILE,length:data.length,value:length,args:[offset]},data),out=reply.output;
    if(![0n,3n].includes(reply.result)||out.length!==32768||out.readUInt32LE()!==0x495a4652||out.readUInt32LE(4)!==1||out.readUInt32LE(8)>length||out.readUInt32LE(16)!==0||![0,3].includes(out.readUInt32LE(20))||out.readUInt32LE(24)!==Number(reply.result)||out.readUInt32LE(28)||out.readBigUInt64LE(32)>0x7fffffffffffffffn||out.readBigUInt64LE(40)!==BigInt(offset)||!out.subarray(48,64).equals(Buffer.alloc(16))||reply.result!==0n&&(out.readUInt32LE(8)!==0||out.readUInt32LE(12)===0)||reply.result===0n&&(out.readUInt32LE(20)!==3||out.readUInt32LE(12)!==0))throw retained(Error('Native SD file receipt differs'));
    const size=out.readBigUInt64LE(32),n=out.readUInt32LE(8);if(reply.result===0n&&(size<BigInt(offset)||BigInt(n)>size-BigInt(offset)))throw retained(Error('Native SD file size differs'));
    return {result:reply.result,nativeResult:out.readUInt32LE(12),fileClosed:out.readUInt32LE(20)===3,size,offset:BigInt(offset),data:Buffer.from(out.subarray(64,64+n))};});
  },
  async pair(calls){
   assert(Array.isArray(calls)&&calls.length>=1&&calls.length<=2);const values=calls.map(call=>{assert((call.address===undefined)!==(call.offset===undefined),'Invalid paired native target');const first=call.address===undefined?sdkTarget(call.offset):target(call.address);assert(call.address===undefined||native.functions.has(call.address),'Native function has not been verified');return {first,args:args(call.args||[])};});
   return serialize(async()=>{const first=values[0],second=values[1],reply=await command({op:second?OP.PAIR:OP.CALL,...first,...(second?{second:second.first,otherArgs:second.args}:{})});if(!second)return {results:[reply.result],output:reply.output};let tail;try{tail=await native.callAddress(receipt.address,owner,epoch,reply.id,1);}catch(error){if(error.code==='IZ2S_HOST_DISPATCH_REVOKED'&&error.nativeRequestSent===false){if(error.generationRevoked===true)throw hostClosed(error);throw error;}throw retained(error);}return {results:[reply.result,tail.result],output:reply.output};});
  },
  async writeCall(address,bytes,functionAddress,list=[]){
   assert(Buffer.isBuffer(bytes)&&bytes.length>0&&bytes.length<=CAPACITY,'Combined guarded transfer exceeds its packet');span(address,bytes.length);const first=target(functionAddress),values=args(list);assert(native.functions.has(functionAddress),'Native function has not been verified');
   return serialize(async()=>{await native.guard(address,bytes.length,2);return {result:(await writePacket(address,bytes,first,values)).result};});
  },
  async request(row){assert(row?.command==='verify-function','Guarded view has no raw command fallback');assert(typeof row.hex==='string'&&/^(?:[0-9a-f]{2}){4,256}$/i.test(row.hex));return serialize(async()=>{if(!native.functions.has(row.address)||!sdkSignatures.get(row.address)?.startsWith(row.hex.toLowerCase()))await native.verifyFunction(row.address,Buffer.from(row.hex,'hex'));return {verified:true};});},
  revoke(){generation.closed=true;},
  inspect(){return {owner,epoch,lastCommand:generation.sequence.toString(),closed:generation.closed,uncertain:!!generation.uncertain,nativeArgumentLimit:extended?8:4,scope:'native-packet-generation',nativeOwnershipReleased:false};}
 };
 exposed=true;return Object.freeze(api);
}
module.exports={create,encode,OP,STATUS,MAGIC,HEADER,CAPACITY,DATA};
