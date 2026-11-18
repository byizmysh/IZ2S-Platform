'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const SECTION=['state','data','vertex','fragment','compute','computeBegin','computeAfter','memory','tile','pad','nativeCode','nativeData'];
const {crc32}=require('../../archive.cjs');
const SB4_COMPAT=1024;
const SB4_CAPABILITIES={'audio-out':4,'engine-image':16,'cooperative-image':48,'worker':176,'presentation':432};
function nativeFlags(n){
 if(!n)return 0;
 let flags=1|(n.assets?.length?8:0);
 if(n.abi===7)return flags|SB4_COMPAT|((n.capabilities||[]).reduce((mask,key)=>mask|SB4_CAPABILITIES[key],0));
 return flags|(n.capabilities?.includes('audio-out')?4:0)|(n.abi>=3?16:0)|(n.abi>=4?32:0)|(n.abi>=5?128:0)|(n.abi===6?256:0);
}
// The native loader clears the private allocation after the stored prefix.
// Store only its nonzero prefix; do not transmit/write/read its zero tail.
function initialMemory(bytes){if(!bytes)return bytes;let end=bytes.length;while(end&&bytes[end-1]===0)end--;return bytes.subarray(0,Math.min(bytes.length,(end+3)&~3));}
function eligibility(app,{sb4=false}={}){
 const m=app.manifest||app,n=m.native;
 if(n?.abi===7&&!sb4)return {compatible:false,reason:'This application requires the standalone IZ2S launcher.'};
 if(n?.abi===7&&(m.hostRuntime||m.localRuntime))return {compatible:false,reason:'This application must run directly on IZ2S.'};
 if(m.localRuntime&&!n)return {compatible:false,reason:'Build the launcher local instructions before encoding this application.'};
 try{require('./assets.cjs').declarations(m);}catch(e){return {compatible:false,reason:e.message};}
 if(n?.assets?.length&&![2,3,4,5,6,7].includes(n.abi))return {compatible:false,reason:'Native ABI 2 or later is required with companion SD assets.'};
 if(n&&(n.format!=='iz2s.native-app'||![1,2,3,4,5,6,7].includes(n.abi)||(n.abi===1&&m.computeSequence)))return {compatible:false,reason:'Unsupported native SD entry ABI or compute sequence.'};
 if(n?.abi===1&&m.memory?.profile==='resident-audio-128')return {compatible:false,reason:'Native ABI 2 is required with the large coherent GPU allocation profile.'};
 if(n&&n.capabilities!==undefined&&(!Array.isArray(n.capabilities)||n.capabilities.some(c=>n.abi===7?!Object.hasOwn(SB4_CAPABILITIES,c):c!=='audio-out')||new Set(n.capabilities).size!==n.capabilities.length||![2,3,4,5,6,7].includes(n.abi)))return {compatible:false,reason:'Unsupported native SD capabilities.'};
 if(m.hostRuntime&&!n)return {compatible:false,reason:'This module needs its native SD entry or a PC service; its GPU icon alone is not an autonomous application.'};
 if(m.builtin||m.displayOnly||m.shellContribution)return {compatible:false,reason:'This module is a shell extension; it needs a standalone SD entry.'};
 try{if(require('./sdk-config.cjs').validate(m.sdk)&&(nativeFlags(n)&256))return {compatible:false,reason:'Native SDK services need a launcher-owned frame boundary.'};}catch(error){return {compatible:false,reason:error.message};}
 if(Object.keys(m.audio?.effects||{}).length)return {compatible:false,reason:'This application requires an autonomous audio provider.'};
 if(m.memory){try{require('../../services/private-memory-profile.cjs').validate(m.memory);}catch(e){return {compatible:false,reason:e.message};}}
 return {compatible:true};
}
function encode(app,{tile,pad,sb4=false}){
 const policy=eligibility(app,{sb4});assert(policy.compatible,policy.reason);const h=Buffer.alloc(512),sdk=require('./sdk-config.cjs').encode(app.manifest.sdk);h.write('IZSDAPP1');const v2=[2,3,4,5,6,7].includes(app.manifest.native?.abi)||app.manifest.memory?.profile==='resident-audio-128';h.writeUInt32LE(v2?2:1,8);h.writeUInt32LE(512,12);assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(app.id));h.write(app.id,24,48);assert(app.name&&!app.name.includes('\0')&&Buffer.byteLength(app.name)<64);h.write(app.name,72,64);
 const native=app.manifest.native;let flags=nativeFlags(native);if(app.manifest.memory?.profile==='resident-audio-128')flags|=2;if(sdk){flags|=512;sdk.copy(h,400);}h.writeUInt32LE(flags,20);if(native)h.writeUInt32LE(native.dataBytes,156);
 if(flags&8){const assets=require('./assets.cjs').binding(native.assets);h.writeUInt32LE(assets.count,160);h.writeUInt32LE(assets.bytes,164);Buffer.from(assets.sha256,'hex').copy(h,368);}
 h.writeUInt32LE(app.files.memory?.allocationBytes||0,136);(app.manifest.computeWorkgroups||[1,1,1]).forEach((n,i)=>h.writeUInt32LE(n,140+i*4));h.writeUInt32LE(app.manifest.computeSequence?.repeat||0,152);
 const chunks=[h];let offset=512;for(const [i,original]of SECTION.entries()){if(!v2&&i>=10)continue;const name=native&&!v2?(i===5?'nativeCode':i===6?'nativeData':original):original;const source=name==='tile'?tile:name==='pad'?pad:app.files[name]?.bytes,bytes=name==='memory'?initialMemory(source):source;if(!bytes?.length)continue;const padding=(4-offset%4)%4;if(padding){chunks.push(Buffer.alloc(padding));offset+=padding;}h.writeUInt32LE(offset,176+i*16);h.writeUInt32LE(bytes.length,180+i*16);h.writeUInt32LE(crc32(bytes),184+i*16);chunks.push(bytes);offset+=bytes.length;}
 h.writeUInt32LE(offset,16);const result=Buffer.concat(chunks);decode(result);return result;
}
function packageId(bytes){
 const field=bytes.subarray(24,72),end=field.indexOf(0);
 assert(end>0&&field.subarray(end).every(b=>!b),'Invalid SD application ID padding');
 const id=field.subarray(0,end).toString('ascii');
 assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id)&&field.subarray(0,end).every(b=>b<128),'Invalid SD application ID');
 return id;
}
function decodeHeader(bytes,size){
 assert(Buffer.isBuffer(bytes)&&bytes.length===512&&Number.isSafeInteger(size)&&size>=512);
 if(bytes.readUInt32LE(8)===3)return require('./managed-package.cjs').decodeHeader(bytes,size);
 assert.equal(bytes.toString('ascii',0,8),'IZSDAPP1');const version=bytes.readUInt32LE(8),v2=version===2;assert(version===1||v2);assert(size<=0xffffffff,'IZA exceeds its 32-bit size field');assert.equal(bytes.readUInt32LE(12),512);assert.equal(bytes.readUInt32LE(16),size);
 const flags=bytes.readUInt32LE(20),native=!!(flags&1);assert(!(flags&~(v2?(959|SB4_COMPAT):513)));assert(!(flags&SB4_COMPAT)||native);assert(!(flags&444)||(v2&&native));assert(!(flags&32)||(flags&16));assert(!(flags&256)||(flags&128));assert(!(flags&128)||(flags&32));const nativeDataBytes=native?bytes.readUInt32LE(156):0;assert(!native||(nativeDataBytes>0&&nativeDataBytes<=32*1024*1024&&nativeDataBytes%4096===0));assert(bytes.subarray(flags&8?168:native?160:156,176).every(b=>b===0)&&bytes.subarray(flags&8?400:368,flags&512?400:512).every(b=>b===0));
 const sdk=flags&512?require('./sdk-config.cjs').decode(bytes.subarray(400,512)):null;
 assert(!sdk||!(flags&256),'Native SDK services need a launcher-owned frame boundary');
 const assetManifest=flags&8?{count:bytes.readUInt32LE(160),bytes:bytes.readUInt32LE(164),sha256:bytes.subarray(368,400).toString('hex')}:{count:0,bytes:0,sha256:null};
 if(flags&8){assert(assetManifest.count>0&&assetManifest.count<=16&&assetManifest.bytes>0&&assetManifest.bytes<=0xffffffff);assert(bytes.subarray(368,400).some(b=>b!==0));}
 const id=packageId(bytes),name=bytes.subarray(72,136).toString().split('\0')[0];assert(name&&bytes[135]===0);const privateBytes=bytes.readUInt32LE(136);assert(privateBytes<=((flags&2)?128:32)*1024*1024&&privateBytes%4096===0);const groups=[140,144,148].map(o=>bytes.readUInt32LE(o));assert(groups.every(v=>v>=1&&v<=65535)&&groups.reduce((a,b)=>a*b,1)<=4096);const repeat=bytes.readUInt32LE(152);assert(repeat<=512);assert(!native||v2||!repeat);
 if(!v2)assert(bytes.subarray(336,368).every(b=>!b),'Unexpected sections in version 1 SD package');
 const ranges={};let end=512;for(const [i,original]of SECTION.entries()){
  if(!v2&&i>=10)continue;const key=native&&!v2?(i===5?'nativeCode':i===6?'nativeData':original):original;const at=176+i*16,offset=bytes.readUInt32LE(at),length=bytes.readUInt32LE(at+4),crc=bytes.readUInt32LE(at+8);assert.equal(bytes.readUInt32LE(at+12),0);
  if(!length){assert.equal(offset,0);assert.equal(crc,0);}else{assert(offset>=end&&offset%4===0&&offset<=size&&length<=size-offset);end=offset+length;}
  ranges[key]={offset,length,crc};
 }
 assert.equal(ranges.state.length,1024);assert(ranges.data.length<=7*1024*1024);assert.equal(ranges.tile.length,131072);assert(ranges.memory.length<=privateBytes);
 for(const key of ['vertex','fragment','compute','pad',...(repeat?['computeBegin','computeAfter']:[])]){const n=ranges[key].length;assert(n>=20&&n<=1048576&&n%4===0);}
 if(!repeat&&(v2||!native))assert(!ranges.computeBegin.length&&!ranges.computeAfter.length);
 if(native){assert(ranges.nativeCode.length>=4&&ranges.nativeCode.length<=16*1024*1024&&ranges.nativeCode.length%4===0);assert(ranges.nativeData.length<=nativeDataBytes);}else if(v2)assert(!ranges.nativeCode.length&&!ranges.nativeData.length);
 return {id,name,version,flags,privateBytes,groups,repeat,native,nativeAbi:native?(flags&SB4_COMPAT?7:flags&256?6:flags&128?5:flags&32?4:flags&16?3:v2?2:1):0,nativeDataBytes,assetManifest,sdk,ranges};
}
function decode(bytes){
 assert(Buffer.isBuffer(bytes));
 const {ranges,...header}=decodeHeader(bytes.subarray(0,512),bytes.length),sections={};
 for(const [key,{offset,length,crc}]of Object.entries(ranges)){
  const section=bytes.subarray(offset,offset+length);
  if(length)assert.equal(crc32(section),crc,header.delegated?'Corrupt managed SD tile':'Corrupt SD section '+key);
  sections[key]=section;
 }
 if(!header.delegated)for(const key of ['vertex','fragment','compute','pad',...(header.repeat?['computeBegin','computeAfter']:[])])assert.equal(sections[key].readUInt32LE(),0x07230203);
 return {...header,sections};
}
async function build(appDirectory,outputDirectory,{sb4=false,entry,includeLibrary}={}){await fs.mkdir(outputDirectory,{recursive:true});const catalog=require('../../services/app-catalog.cjs');let app=await catalog.readApp(appDirectory,{entry});app=await require('./adapters.cjs').prepare(app,outputDirectory,{includeLibrary});const policy=eligibility(app,{sb4});assert(policy.compatible,policy.reason);if(app.manifest.native){const n=app.manifest.native;assert(Number.isInteger(n.dataBytes)&&n.dataBytes>0&&n.dataBytes<=32*1024*1024&&n.dataBytes%4096===0);if(!app.files.nativeCode)app.files.nativeCode=await catalog.asset(app.root,n.code,16*1024*1024);if(n.initial)app.files.nativeData=await catalog.asset(app.root,n.initial,n.dataBytes);}
 const raster=await catalog.raster({apps:[app]},outputDirectory,'sd-tile'),pad=await require('../../services/pad-normalize.cjs').shader(outputDirectory,{stdio:['ignore',2,2]});const bytes=encode(app,{tile:raster.subarray(0,131072),pad,sb4});const companion=require('./assets.cjs'),assets=await companion.stage(app.sdAssets||[],outputDirectory),output=path.join(outputDirectory,'launcher.iza'),sha256=require('node:crypto').createHash('sha256').update(bytes).digest('hex');const bundle=await companion.bundle(outputDirectory,{id:app.id,bytes:bytes.length,sha256,assets});
 const temporary=output+'.'+require('node:crypto').randomUUID()+'.tmp';let file;try{file=await fs.open(temporary,'wx');await file.writeFile(bytes);await file.sync();await file.close();file=null;await fs.rename(temporary,output);}finally{if(file)await file.close();await fs.rm(temporary,{force:true});}
 return {id:app.id,name:app.name,output,bytes,assets,bundle,sha256};}
module.exports={SECTION,packageId,crc32,initialMemory,eligibility,encode,decodeHeader,decode,build};
