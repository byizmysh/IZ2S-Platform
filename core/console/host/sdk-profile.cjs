'use strict';
// Framework-owned synchronous SDK entry points. No arbitrary IPC sender,
// pthread entry, callback registration or application-supplied address.
const assert=require('node:assert/strict');
const hid=require('../abi/hid-functions.json'),services=require('../abi/service-functions.json');
const timing=require('../abi/timing-code.json').functions;
const serviceNames=['netStatus','netAddress','socketInitialized','irHandle','irStatus','socket','sendto','recv','socketClose','errno','nfcState','nfcInit','nfcFinalize','nfcList','nfcStart','nfcStop','nfcTag','nfcPad'];
const definitions=Object.freeze([...Object.entries(hid),...serviceNames.map(name=>[name,services[name]]),['serviceTick',{offset:timing.tick.offset,hex:timing.tick.bytes}]].map(([name,row])=>Object.freeze({name,offset:row.offset,hex:row.hex})));
assert(definitions.length<=64&&new Set(definitions.map(row=>row.offset)).size===definitions.length);
for(const row of definitions)assert(Number.isSafeInteger(row.offset)&&row.offset>=0&&row.offset<0x5e1000&&row.offset%4===0&&/^(?:[a-f0-9]{8}){1,64}$/.test(row.hex));
const cache=new WeakMap(),proofs=new WeakMap();
async function verify(native,{epoch}={}){
 assert(Number.isInteger(epoch)&&epoch>0&&epoch<=0xffffffff,'A fixed application epoch is required');
 const sdk=native.addresses?.sdk,output=native.output;assert(Number.isSafeInteger(sdk)&&sdk>=4096&&sdk<=2**39-0x5e1000&&sdk%4096===0&&Number.isSafeInteger(output));
 const previous=cache.get(native);if(previous?.sdk===sdk&&previous.epoch===epoch&&previous.targets.every(row=>native.functions.has(row.target)))return previous;
 const regions=[],targets=definitions.map(row=>Object.freeze({target:sdk+row.offset,effect:0}));
 // Reuse RX regions within this one observation. Copy two immutable prefixes
 // per SDK exchange; neither a worker nor an app allocation is needed.
 for(const [i,row]of definitions.entries()){
  const at=targets[i].target,n=row.hex.length/2;let region=regions.find(r=>at>=r.base&&at+n<=r.end);
  if(!region){const r=await native.query(at);region={base:Number(r.address),end:Number(r.address+r.size)};assert(Number.isSafeInteger(region.base)&&Number.isSafeInteger(region.end)&&region.end<=2**39&&r.permissions===5&&!r.attributes&&at>=region.base&&at+n<=region.end,'SDK service code is not immutable RX');regions.push(region);}
 }
 for(let i=0;i<definitions.length;i+=2){
  const rows=definitions.slice(i,i+2),calls=rows.map((row,j)=>({offset:0x586fb0,args:[output+j*256,sdk+row.offset,row.hex.length/2,0]})),reply=await native.exchange(calls);
  for(const [j,row]of rows.entries())assert(reply.results?.[j]===BigInt(output+j*256)&&Buffer.isBuffer(reply.output)&&reply.output.subarray(j*256,j*256+row.hex.length/2).toString('hex')===row.hex,'SDK service signature differs: '+row.name);
 }
 for(const row of targets)native.functions.add(row.target);
 const proof=Object.freeze({format:'iz2s.sdk-service-profile/1',sdk,epoch,targets:Object.freeze(targets),names:Object.freeze(definitions.map(row=>row.name))});cache.set(native,proof);proofs.set(proof,native);return proof;
}
function signatures(proof,native,epoch){assert(proofs.get(proof)===native&&proof.epoch===epoch,'SDK service profile belongs to another original host or epoch');return new Map(definitions.map(row=>[proof.sdk+row.offset,row.hex]));}
module.exports={definitions,verify,signatures};
