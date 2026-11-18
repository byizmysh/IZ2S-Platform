'use strict';
// Prove the actual SDK caller and its checked reserve, not a requested size.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const BYTES=4*1024*1024,PAGE=4096,GUARD=64,LOWER=Buffer.alloc(GUARD,0xa7),UPPER=Buffer.alloc(GUARD,0x5d),LIMIT=2**39;
function allocation(offset){assert(Number.isSafeInteger(offset)&&offset>=PAGE&&offset%PAGE===0);return {lowerOffset:offset+PAGE-GUARD,stackOffset:offset+PAGE,stackBytes:BYTES,upperOffset:offset+PAGE+BYTES,bytes:offset+2*PAGE+BYTES};}
function descriptor(base,layout){assert(Number.isSafeInteger(base)&&base>=PAGE&&base%PAGE===0&&base+layout.bytes<LIMIT);return {base:base+layout.stackOffset,bytes:BYTES,lower:base+layout.lowerOffset,upper:base+layout.upperOffset,guardBytes:GUARD};}
function verifier({native,report,manifest=require('./check.cjs').verify()}){
 const a={...(native.addresses??native.a)},L={...manifest.layout},stack={...report?.sdkStack},binding={...report},imageHash=manifest.sha256;let imageProof;
 assert(a?.sdk&&a?.scratch&&report?.bootstrapSha256===imageHash&&report.startedThread&&report.allocationsRetained&&report.handedOver&&!report.closed,'SDK worker ownership is unverified');
 assert(report.context>=PAGE&&Number.isSafeInteger(report.context)&&report.context+L.server<LIMIT&&Number.isSafeInteger(report.base)&&Number.isSafeInteger(report.bytes)&&report.bytes>BYTES);
 assert([stack.base,stack.lower,stack.upper].every(Number.isSafeInteger)&&stack.upper+GUARD<LIMIT);
 assert(stack.bytes===BYTES&&stack.guardBytes===GUARD&&stack.base%PAGE===0&&stack.lower===stack.base-GUARD&&stack.upper===stack.base+BYTES&&stack.lower>=report.base&&stack.upper+GUARD<=report.base+report.bytes,'SDK worker stack allocation differs');
 assert(Number.isSafeInteger(report.entry)&&report.entry>=PAGE&&report.entry%PAGE===0);
 return async()=>{try{
  imageProof??=(async()=>{const image=await fs.readFile(path.join(__dirname,'native/build/recovery.bin'));assert(crypto.createHash('sha256').update(image).digest('hex')===imageHash);assert((await native.read(binding.entry,image.length)).equals(image),'Live recovery worker code differs');})();await imageProof;
  const spans=[{address:binding.context,length:L.server},{address:stack.lower,length:GUARD},{address:stack.upper,length:GUARD}],parts=native.readMany?await native.readMany(spans):await Promise.all(spans.map(s=>native.read(s.address,s.length))),[b,lower,upper]=parts;
  assert(Buffer.isBuffer(b)&&b.length===L.server&&lower.equals(LOWER)&&upper.equals(UPPER),'SDK worker stack guards changed');
  const u=name=>b.readUInt32LE(L[name]),q=name=>Number(b.readBigUInt64LE(L[name]));
  assert(u('magic')===0x33524e49&&u('version')===3&&u('state')===2&&!u('error')&&!u('stop')&&u('foreground_state')===2,'SDK worker is not the parked foreground replacement');
  assert(q('sdk')===a.sdk&&q('scratch')===a.scratch&&q('scratch_bytes')===0x80000&&q('stack')===stack.base&&q('stack_bytes')===BYTES,'SDK worker address binding differs');
  assert(u('stack_guarded')===1&&!u('stack_fault')&&b.readBigUInt64LE(L.stack_checks)>0n&&q('observed_stack')>=stack.base&&q('observed_stack')<stack.upper,'Native SDK stack execution is unverified');
  assert(q('foreground_thread')===binding.foregroundThread&&q('thread_type')>=PAGE&&q('thread_type')!==binding.foregroundThread,'SDK caller identity differs');
  return Object.freeze({stackBytes:BYTES,sdk:a.sdk,scratch:a.scratch,workerContext:binding.context,stack:stack.base,guarded:true,nativeChecks:true,currentSDKThread:true});
 }catch(error){throw Object.assign(error,{resourcesRetained:true,code:'IZ2S_RECOVERY_STACK_UNVERIFIED'});}};
}
module.exports={BYTES,PAGE,GUARD,LOWER,UPPER,allocation,descriptor,verifier};
