// Six-argument worker using verified instructions from the supported SDK.
// Runs on an owned pthread and exits through the SDK's pthread_exit.
const fs=require('node:fs/promises'),path=require('node:path');
const code=require('../abi/thread-code.json').functions;
const SENTINEL=0xfedcba9876543210n;
// The driver compiler can use substantially more than the old 64 KiB reserve.
// Keep two MiB below the AArch64 SP, with a canary above the context tables.
const STACK_BYTES=0x200000,STACK_TOP=STACK_BYTES+0x2000,FRAME_BYTES=STACK_TOP+0x2000,ALLOCATION_BYTES=FRAME_BYTES+0x1000;
const GUARD_OFFSET=0x2000,GUARD=Buffer.alloc(64,0xa7);
function frame(base,sdk,target,args){
 if(args.length>6)throw Error('At most six arguments');args=[...args];while(args.length<6)args.push(0);
 const b=Buffer.alloc(FRAME_BYTES),put=(o,v)=>b.writeBigUInt64LE(BigInt(v),o),sp=STACK_TOP;
 // Set x4/x5, pop the dispatch registers, then enter the qualified call4 path.
 put(0,sdk+code.loadTable.offset+8);put(8,args[5]);put(0x10,args[4]);put(0x30,args[0]);put(0x58,sdk+0x5604a8);put(0x68,base+sp);
 put(sp+8,sdk+code.loadTable.offset);put(sp+0x10,base+0x1000);
 put(0x1000,base+0x1010);put(0x1078,target);
 const next=sp+0x30;
 put(next+8,sdk+code.call4.offset);put(next+0x10,args[1]);put(next+0x20,args[2]);put(next+0x28,args[3]);
 put(next+0xd8,sdk+code.saveResult64.offset);put(next+0x118,base+0x800);put(next+0x128,sdk+code.exit.offset);put(0x800,SENTINEL);
 return b;
}
async function create(rpc,directory){
 if(rpc.generationGuarded===true){
  if(rpc.nativeArgumentLimit<6||typeof rpc.callExtended!=='function')throw Error('Six-argument SDK calls require a verified guarded stack');
  return Object.freeze({async call(target,args){if(!Array.isArray(args)||args.length>6)throw Error('At most six arguments');return (await rpc.callExtended(target,args)).result;}});
 }
 const sdk=rpc.a.sdk,original=require('../abi/sdk-checks.cjs');
 for(const name of ['create','join','longjmp','loadTable','call4','saveResult64','exit','tryWait']){
  const f=code[name],expected=Buffer.from(f.bytes,'hex').subarray(0,Math.min(256,f.bytes.length/2));
  await rpc.request({command:'verify-function',address:sdk+f.offset,hex:expected.toString('hex')});
 }
 await rpc.request({command:'verify-function',address:sdk+0x5604a8,hex:original.bytes(0x5604a8,36).toString('hex')});
 const slot=rpc.a.scratch+0x10000,saved=await rpc.read(slot,8);let base;
 try{if((await rpc.call(5732112,[slot,4096,ALLOCATION_BYTES])).result!==0n)throw Error('Worker allocation failed');base=Number((await rpc.read(slot,8)).readBigUInt64LE());}finally{await rpc.write(slot,saved);}
 if(!Number.isSafeInteger(base)||base%4096||!base)throw Error('Invalid worker allocation');
 await rpc.write(base+GUARD_OFFSET,GUARD);
 const record={session:rpc.a.session,cpuABI:'AArch64',base,bytes:ALLOCATION_BYTES,stackBytes:STACK_BYTES,stackTop:base+STACK_TOP,guardAddress:base+GUARD_OFFSET,calls:[]};const file=path.join(directory,'native-six.json');
 const save=()=>fs.writeFile(file,JSON.stringify(record,null,2));await save();
 async function call(target,args){
  // Explicitly verify the live target and never reuse a running worker's stack.
  const bytes=await rpc.read(target,32);await rpc.request({command:'verify-function',address:target,hex:bytes.toString('hex')});
  if(!(await rpc.read(base+GUARD_OFFSET,GUARD.length)).equals(GUARD))throw Error('Worker stack guard changed; native calls stopped');
  const bytesToWrite=frame(base,sdk,target,args);
  // Only upload the context and return chain, leaving the large stack private.
  await rpc.write(base,bytesToWrite.subarray(0,0x2000));await rpc.write(base+STACK_TOP,bytesToWrite.subarray(STACK_TOP));
  const out=base+FRAME_BYTES;await rpc.write(out,Buffer.alloc(16));
  const row={target,args:args.map(String),status:'creating',started:new Date().toISOString()};record.calls.push(row);await save();
  const rc=(await rpc.callAddress(sdk+code.create.offset,[out,0,sdk+code.longjmp.offset,base])).result;
  if(rc)throw Error('pthread_create: '+rc);
  const thread=(await rpc.read(out,8)).readBigUInt64LE();row.thread=String(thread);row.status='running';await save();
  const until=Date.now()+30000;let done=false;
  while(Date.now()<until){const state=(await rpc.callAddress(sdk+code.tryWait.offset,[thread])).result;if(state===1n){done=true;break;}if(state!==0n)throw Error('Invalid worker state');await new Promise(r=>setTimeout(r,20));}
  if(!done)throw Error('Worker timeout; allocation retained');
  const result=(await rpc.read(base+0x800,8)).readBigUInt64LE();if(result===SENTINEL)throw Error('Worker did not return');
  if((await rpc.callAddress(sdk+code.join.offset,[thread,out+8])).result!==0n)throw Error('Worker join failed');
  if((await rpc.read(out+8,8)).readBigUInt64LE()!==result)throw Error('Worker exit differs');
  if(!(await rpc.read(base+GUARD_OFFSET,GUARD.length)).equals(GUARD)){row.status='stack-guard-failed';await save();throw Error('Worker stack reserve exceeded; native calls stopped');}
  row.result=String(result);row.status='joined';row.completed=new Date().toISOString();await save();return result;
 }
 return {call,base,record};
}
module.exports={create,frame,STACK_BYTES,STACK_TOP,FRAME_BYTES,ALLOCATION_BYTES,GUARD_OFFSET};
if(require.main===module)(async()=>{
 const directory=path.resolve(process.argv[2]),rpc=require(path.join(directory,'shared/rpc.cjs')),worker=await create(rpc,directory);
 const data=worker.base+FRAME_BYTES+0x200;await rpc.write(data,Buffer.from('%llx:%llx:%llx\0'));
 const result=await worker.call(rpc.a.sdk+5888928,[data+0x100,256,data,0x123456789abcdef0n,0x8877665544332211n,0x1020304050607080n]);
 const output=(await rpc.read(data+0x100,256)).toString().split('\0')[0];
 if(output!=='123456789abcdef0:8877665544332211:1020304050607080'||result!==BigInt(output.length))throw Error('Six-register verification failed: '+output);
 console.log('Six-argument worker verified:',output);
})().catch(e=>{console.error(e);process.exitCode=1;});
