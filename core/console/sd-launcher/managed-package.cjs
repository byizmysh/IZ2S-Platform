'use strict';
// A card-resident original IZM plus a non-executable menu descriptor. This
// format never treats a PC service's icon/shaders as its autonomous program.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const P=require('./package.cjs'),assets=require('./assets.cjs');
const FLAGS=72,HEADER=512,TILE=131072,sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function encode({id,name,source,tile}){
 assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id)&&name&&Buffer.byteLength(name)<64);assert(Buffer.isBuffer(tile)&&tile.length===TILE);assets.binding([source]);
 const bytes=Buffer.alloc(HEADER+TILE);bytes.write('IZSDAPP1');bytes.writeUInt32LE(3,8);bytes.writeUInt32LE(HEADER,12);bytes.writeUInt32LE(bytes.length,16);bytes.writeUInt32LE(FLAGS,20);bytes.write(id,24,48);bytes.write(name,72,64);
 for(const at of [140,144,148])bytes.writeUInt32LE(1,at);bytes.writeUInt32LE(1,160);bytes.writeUInt32LE(source.bytes,164);
 Buffer.from(assets.binding([source]).sha256,'hex').copy(bytes,368);Buffer.from(source.sha256,'hex').copy(bytes,400);
 const at=176+P.SECTION.indexOf('tile')*16;bytes.writeUInt32LE(HEADER,at);bytes.writeUInt32LE(TILE,at+4);bytes.writeUInt32LE(P.crc32(tile),at+8);tile.copy(bytes,HEADER);decode(bytes);return bytes;
}
function decode(bytes){
 assert(Buffer.isBuffer(bytes)&&bytes.length===HEADER+TILE&&bytes.toString('ascii',0,8)==='IZSDAPP1'&&bytes.readUInt32LE(8)===3&&bytes.readUInt32LE(12)===HEADER&&bytes.readUInt32LE(16)===bytes.length&&bytes.readUInt32LE(20)===FLAGS,'Invalid managed SD package');
 const id=bytes.subarray(24,72).toString().split('\0')[0],name=bytes.subarray(72,136).toString().split('\0')[0];assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id)&&name&&bytes[135]===0);assert(bytes.subarray(136,140).every(b=>!b)&&[140,144,148].every(at=>bytes.readUInt32LE(at)===1)&&bytes.subarray(152,160).every(b=>!b)&&bytes.readUInt32LE(160)===1&&bytes.subarray(168,176).every(b=>!b)&&bytes.subarray(432,512).every(b=>!b));
 const digest=bytes.subarray(400,432).toString('hex'),source={path:'asset-'+digest+'.izdata',bytes:bytes.readUInt32LE(164),sha256:digest},assetManifest=assets.binding([source]);assert(bytes.subarray(400,432).some(b=>b),'Missing managed SD source identity');assert.equal(assetManifest.sha256,bytes.subarray(368,400).toString('hex'),'Managed SD source binding differs');
 const sections={};for(const [i,key]of P.SECTION.entries()){const at=176+i*16;if(key==='tile'){assert(bytes.readUInt32LE(at)===HEADER&&bytes.readUInt32LE(at+4)===TILE&&bytes.readUInt32LE(at+12)===0);sections.tile=bytes.subarray(HEADER);assert.equal(P.crc32(sections.tile),bytes.readUInt32LE(at+8),'Corrupt managed SD tile');}else{assert(bytes.subarray(at,at+16).every(b=>!b));sections[key]=Buffer.alloc(0);}}
 return {id,name,version:3,flags:FLAGS,native:false,delegated:true,source,assetManifest,sections,privateBytes:0,groups:[1,1,1],repeat:0};
}
async function build(pkg,app,outputDirectory){
 await fs.mkdir(outputDirectory,{recursive:true});const bytes=await fs.readFile(pkg.packagePath);assert.equal(sha(bytes),pkg.sha256,'Selected IZM changed');const inspected=require('../../archive.cjs').inspect(bytes);assert.equal(inspected.descriptor.id,pkg.id);
 const source={path:'asset-'+pkg.sha256+'.izdata',bytes:bytes.length,sha256:pkg.sha256},file=path.join(outputDirectory,source.path);await fs.writeFile(file,bytes);
 const tile=(await require('../../services/app-catalog.cjs').raster({apps:[app]},outputDirectory,'managed-sd-tile')).subarray(0,TILE),image=encode({id:pkg.id,name:pkg.name,source,tile}),output=path.join(outputDirectory,'launcher.izapp');await fs.writeFile(output,image);
 const companions=[{...source,file}],digest=sha(image),bundle=await assets.bundle(outputDirectory,{id:pkg.id,bytes:image.length,sha256:digest,assets:companions});
 return {id:pkg.id,name:pkg.name,bytes:image,output,assets:companions,bundle,execution:'pc',sha256:digest};
}
module.exports={FLAGS,encode,decode,build};
