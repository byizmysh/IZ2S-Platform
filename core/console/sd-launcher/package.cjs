'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const SECTION=['state','data','vertex','fragment','compute','computeBegin','computeAfter','memory','tile','pad','nativeCode','nativeData'];
function crc32(bytes){let c=0xffffffff;for(const b of bytes){c^=b;for(let k=0;k<8;k++)c=(c>>>1)^(0xedb88320&-(c&1));}return (c^0xffffffff)>>>0;}
// The native loader clears the full private allocation before every launch.
// Store only its nonzero prefix; do not transmit/write/read its zero tail.
function initialMemory(bytes){if(!bytes)return bytes;let end=bytes.length;while(end&&bytes[end-1]===0)end--;return bytes.subarray(0,Math.min(bytes.length,(end+3)&~3));}
function eligibility(app){
 const m=app.manifest||app,n=m.native;
 if(m.localRuntime&&!n)return {compatible:false,reason:'Build the launcher local instructions before encoding this application.'};
 try{require('./assets.cjs').declarations(m);}catch(e){return {compatible:false,reason:e.message};}
 if(n?.assets?.length&&![2,3,4,5,6].includes(n.abi))return {compatible:false,reason:'Native ABI 2 or later is required with companion SD assets.'};
 if(n&&(n.format!=='iz2s.native-app'||![1,2,3,4,5,6].includes(n.abi)||(n.abi===1&&m.computeSequence)))return {compatible:false,reason:'Unsupported native SD entry ABI or compute sequence.'};
 if(n?.abi===1&&m.memory?.profile==='resident-audio-128')return {compatible:false,reason:'Native ABI 2 is required with the large coherent GPU allocation profile.'};
 if(n&&n.capabilities!==undefined&&(!Array.isArray(n.capabilities)||n.capabilities.some(c=>c!=='audio-out')||new Set(n.capabilities).size!==n.capabilities.length||![2,3,4,5,6].includes(n.abi)))return {compatible:false,reason:'Unsupported native SD capabilities.'};
 if(m.hostRuntime&&!n)return {compatible:false,reason:'This module needs its native SD entry or a PC service; its GPU icon alone is not an autonomous application.'};
 if(m.builtin||m.displayOnly||m.shellContribution)return {compatible:false,reason:'This module is a shell extension; it needs a standalone SD entry.'};
 if(m.sdk&&(m.sdk.version!==1||!Array.isArray(m.sdk.capabilities)||m.sdk.capabilities.length||m.sdk.services?.length||m.sdk.endpoints?.length))return {compatible:false,reason:'This application requests host-managed SDK services.'};
 if(Object.keys(m.audio?.effects||{}).length)return {compatible:false,reason:'This application requires an autonomous audio provider.'};
 if(m.memory){try{require('../../services/private-memory-profile.cjs').validate(m.memory);}catch(e){return {compatible:false,reason:e.message};}}
 return {compatible:true};
}
function encode(app,{tile,pad}){
 const policy=eligibility(app);assert(policy.compatible,policy.reason);const h=Buffer.alloc(512);h.write('IZSDAPP1');const v2=[2,3,4,5,6].includes(app.manifest.native?.abi)||app.manifest.memory?.profile==='resident-audio-128';h.writeUInt32LE(v2?2:1,8);h.writeUInt32LE(512,12);assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(app.id));h.write(app.id,24,48);assert(Buffer.byteLength(app.name)<64);h.write(app.name,72,64);
 const native=app.manifest.native;let flags=native?1:0;if(app.manifest.memory?.profile==='resident-audio-128')flags|=2;if(native?.capabilities?.includes('audio-out'))flags|=4;if(native?.assets?.length)flags|=8;if(native?.abi>=3)flags|=16;if(native?.abi>=4)flags|=32;if(native?.abi>=5)flags|=128;if(native?.abi===6)flags|=256;h.writeUInt32LE(flags,20);if(native)h.writeUInt32LE(native.dataBytes,156);
 if(flags&8){const assets=require('./assets.cjs').binding(native.assets);h.writeUInt32LE(assets.count,160);h.writeUInt32LE(assets.bytes,164);Buffer.from(assets.sha256,'hex').copy(h,368);}
 h.writeUInt32LE(app.files.memory?.allocationBytes||0,136);(app.manifest.computeWorkgroups||[1,1,1]).forEach((n,i)=>h.writeUInt32LE(n,140+i*4));h.writeUInt32LE(app.manifest.computeSequence?.repeat||0,152);
 const chunks=[h];let offset=512;for(const [i,original]of SECTION.entries()){if(!v2&&i>=10)continue;const name=native&&!v2?(i===5?'nativeCode':i===6?'nativeData':original):original;const source=name==='tile'?tile:name==='pad'?pad:app.files[name]?.bytes,bytes=name==='memory'?initialMemory(source):source;if(!bytes?.length)continue;const padding=(4-offset%4)%4;if(padding){chunks.push(Buffer.alloc(padding));offset+=padding;}h.writeUInt32LE(offset,176+i*16);h.writeUInt32LE(bytes.length,180+i*16);h.writeUInt32LE(crc32(bytes),184+i*16);chunks.push(bytes);offset+=bytes.length;}
 h.writeUInt32LE(offset,16);const result=Buffer.concat(chunks);decode(result);return result;
}
function decode(bytes){
 if(Buffer.isBuffer(bytes)&&bytes.length>=512&&bytes.readUInt32LE(8)===3)return require('./managed-package.cjs').decode(bytes);
 assert(Buffer.isBuffer(bytes)&&bytes.length>=512);assert.equal(bytes.toString('ascii',0,8),'IZSDAPP1');const version=bytes.readUInt32LE(8),v2=version===2;assert(version===1||v2);assert(bytes.length<=(v2?80:48)*1024*1024);assert.equal(bytes.readUInt32LE(12),512);assert.equal(bytes.readUInt32LE(16),bytes.length);
 const flags=bytes.readUInt32LE(20),native=!!(flags&1);assert(!(flags&~(v2?447:1)));assert(!(flags&444)||(v2&&native));assert(!(flags&32)||(flags&16));assert(!(flags&256)||(flags&128));assert(!(flags&128)||(flags&32));const nativeDataBytes=native?bytes.readUInt32LE(156):0;assert(!native||(nativeDataBytes>0&&nativeDataBytes<=32*1024*1024&&nativeDataBytes%4096===0));assert(bytes.subarray(flags&8?168:native?160:156,176).every(b=>b===0)&&bytes.subarray(v2?(flags&8?400:368):336,512).every(b=>b===0));
 const assetManifest=flags&8?{count:bytes.readUInt32LE(160),bytes:bytes.readUInt32LE(164),sha256:bytes.subarray(368,400).toString('hex')}:{count:0,bytes:0,sha256:null};
 if(flags&8){assert(assetManifest.count>0&&assetManifest.count<=16&&assetManifest.bytes>0&&assetManifest.bytes<=512*1024*1024);assert(bytes.subarray(368,400).some(b=>b!==0));}
 const id=bytes.subarray(24,72).toString().split('\0')[0],name=bytes.subarray(72,136).toString().split('\0')[0];assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id));assert(name&&bytes[135]===0);const privateBytes=bytes.readUInt32LE(136);assert(privateBytes<=((flags&2)?128:32)*1024*1024&&privateBytes%4096===0);const groups=[140,144,148].map(o=>bytes.readUInt32LE(o));assert(groups.every(v=>v>=1&&v<=65535)&&groups.reduce((a,b)=>a*b,1)<=4096);const repeat=bytes.readUInt32LE(152);assert(repeat<=512);assert(!native||v2||!repeat);
 const sections={};let end=512;for(const [i,original]of SECTION.entries()){
  if(!v2&&i>=10)continue;const key=native&&!v2?(i===5?'nativeCode':i===6?'nativeData':original):original;const at=176+i*16,offset=bytes.readUInt32LE(at),length=bytes.readUInt32LE(at+4),crc=bytes.readUInt32LE(at+8);assert.equal(bytes.readUInt32LE(at+12),0);
  if(!length){assert.equal(offset,0);assert.equal(crc,0);sections[key]=Buffer.alloc(0);continue;}assert(offset>=end&&offset%4===0&&offset<=bytes.length&&length<=bytes.length-offset);end=offset+length;const b=bytes.subarray(offset,end);assert.equal(crc32(b),crc,'Corrupt SD section '+key);sections[key]=b;
 }
 assert.equal(sections.state.length,1024);assert(sections.data.length<=7*1024*1024);assert.equal(sections.tile.length,131072);assert(sections.memory.length<=privateBytes);
 for(const key of ['vertex','fragment','compute','pad',...(repeat?['computeBegin','computeAfter']:[])]){const b=sections[key];assert(b.length>=20&&b.length<=1048576&&b.length%4===0&&b.readUInt32LE()===0x07230203);}
 if(!repeat&&(v2||!native))assert(!sections.computeBegin.length&&!sections.computeAfter.length);
 if(native){assert(sections.nativeCode.length>=4&&sections.nativeCode.length<=16*1024*1024&&sections.nativeCode.length%4===0);assert(sections.nativeData.length<=nativeDataBytes);}else if(v2)assert(!sections.nativeCode.length&&!sections.nativeData.length);
 return {id,name,version,flags,privateBytes,groups,repeat,native,nativeAbi:native?(flags&256?6:flags&128?5:flags&32?4:flags&16?3:v2?2:1):0,nativeDataBytes,assetManifest,sections};
}
async function build(appDirectory,outputDirectory){await fs.mkdir(outputDirectory,{recursive:true});const catalog=require('../../services/app-catalog.cjs');let app=await catalog.readApp(appDirectory);app=await require('./adapters.cjs').prepare(app,outputDirectory);const policy=eligibility(app);assert(policy.compatible,policy.reason);if(app.manifest.native){const n=app.manifest.native;assert(Number.isInteger(n.dataBytes)&&n.dataBytes>0&&n.dataBytes<=32*1024*1024&&n.dataBytes%4096===0);if(!app.files.nativeCode)app.files.nativeCode=await catalog.asset(app.root,n.code,16*1024*1024);if(n.initial)app.files.nativeData=await catalog.asset(app.root,n.initial,n.dataBytes);}
 const raster=await catalog.raster({apps:[app]},outputDirectory,'sd-tile'),pad=await require('../../services/pad-normalize.cjs').shader(outputDirectory);const bytes=encode(app,{tile:raster.subarray(0,131072),pad});const companion=require('./assets.cjs'),assets=await companion.stage(app.sdAssets||[],outputDirectory),output=path.join(outputDirectory,'launcher.izapp'),sha256=require('node:crypto').createHash('sha256').update(bytes).digest('hex');const bundle=await companion.bundle(outputDirectory,{id:app.id,bytes:bytes.length,sha256,assets});
 const temporary=output+'.'+require('node:crypto').randomUUID()+'.tmp';let file;try{file=await fs.open(temporary,'wx');await file.writeFile(bytes);await file.sync();await file.close();file=null;await fs.rename(temporary,output);}finally{if(file)await file.close();await fs.rm(temporary,{force:true});}
 return {id:app.id,name:app.name,output,bytes,assets,bundle,sha256};}
module.exports={SECTION,crc32,initialMemory,eligibility,encode,decode,build};
