'use strict';
// Immutable companion files. Native code/data chooses the declared hashed
// relative path; the installer publishes these before launcher.izapp changes.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const MAX_ASSETS=16,MAX_ASSET_BYTES=256*1024*1024,MAX_TOTAL_BYTES=512*1024*1024;
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function identity(a){
 assert(a&&typeof a==='object'&&!Array.isArray(a),'Invalid SD asset declaration');
 assert(typeof a.sha256==='string'&&/^[a-f0-9]{64}$/.test(a.sha256),'Invalid SD asset SHA256');
 assert.equal(a.path,'asset-'+a.sha256+'.izdata','SD asset path must contain its exact SHA256');
 assert(Number.isSafeInteger(a.bytes)&&a.bytes>0&&a.bytes<=MAX_ASSET_BYTES,'Invalid SD asset size');
}
function declarations(manifest){
 const rows=manifest.native?.assets;if(rows===undefined)return [];
 assert(Array.isArray(rows)&&rows.length<=MAX_ASSETS,'Invalid SD asset list');
 let total=0;const names=new Set();return rows.map(a=>{
  identity(a);assert.deepEqual(Object.keys(a).sort(),['bytes','file','path','sha256']);
  assert(typeof a.file==='string'&&a.file.length>0&&Buffer.byteLength(a.file)<769&&!path.isAbsolute(a.file)&&!/[\x00-\x1f\x7f\\:]/.test(a.file)&&a.file.split('/').every(p=>p&&p!=='.'&&p!=='..'),'SD asset source must stay inside its app');
  assert(!names.has(a.path),'Duplicate SD asset path');names.add(a.path);total+=a.bytes;assert(total<=MAX_TOTAL_BYTES,'SD assets exceed total size limit');
  return {file:a.file,path:a.path,bytes:a.bytes,sha256:a.sha256};
 });
}
function prepared(rows){
 assert(Array.isArray(rows)&&rows.length<=MAX_ASSETS,'Invalid prepared SD assets');let total=0;const names=new Set();
 return rows.map(a=>{identity(a);assert(typeof a.file==='string'&&path.isAbsolute(a.file)&&!a.file.includes('\0'),'Prepared SD asset source must be an absolute file');assert(!names.has(a.path),'Duplicate SD asset path');names.add(a.path);total+=a.bytes;assert(total<=MAX_TOTAL_BYTES,'SD assets exceed total size limit');return {file:a.file,path:a.path,bytes:a.bytes,sha256:a.sha256};});
}
function binding(rows){
 assert(Array.isArray(rows)&&rows.length<=MAX_ASSETS,'Invalid SD asset binding');const names=new Set();let bytes=0;
 const identities=rows.map(a=>{identity(a);assert(!names.has(a.path),'Duplicate SD asset path');names.add(a.path);bytes+=a.bytes;assert(bytes<=MAX_TOTAL_BYTES,'SD assets exceed total size limit');return [a.path,a.bytes,a.sha256];}).sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
 return {count:identities.length,bytes,sha256:identities.length?sha(Buffer.from('iz2s.sd-assets.v1\n'+JSON.stringify(identities))):null};
}
function verifyBinding(pkg,rows){
 const actual=binding(rows);assert.equal(!!(pkg.flags&8),actual.count>0,'SD companion assets are missing or do not match the application');
 assert.deepEqual(actual,pkg.assetManifest,'SD companion identities differ from the application');return actual;
}
async function digest(file,bytes){
 const f=await fs.open(file,'r');try{
  const stat=await f.stat();assert(stat.isFile()&&stat.size===bytes,'SD asset source size differs');const hash=crypto.createHash('sha256'),buffer=Buffer.alloc(65536);let offset=0;
  while(offset<bytes){const {bytesRead}=await f.read(buffer,0,Math.min(buffer.length,bytes-offset),offset);assert(bytesRead>0,'Short SD asset source');hash.update(buffer.subarray(0,bytesRead));offset+=bytesRead;}
  assert.equal((await f.stat()).size,bytes,'SD asset source size changed');return hash.digest('hex');
 }finally{await f.close();}
}
async function inspect(root,manifest){
 const rows=[];for(const a of declarations(manifest)){
  const file=await fs.realpath(path.resolve(root,a.file));assert(file.startsWith(root+path.sep),'SD asset escapes its app folder');assert.equal(await digest(file,a.bytes),a.sha256,'SD asset source checksum differs');rows.push({...a,file});
 }return rows;
}
function inspectPacked(manifest,files){
 for(const a of declarations(manifest)){const file=files.find(f=>f.name===a.file);assert(file&&Buffer.isBuffer(file.bytes)&&file.bytes.length===a.bytes,'Missing or wrong-sized packaged SD asset: '+a.file);assert.equal(sha(file.bytes),a.sha256,'Packaged SD asset checksum differs: '+a.file);}
}
async function stage(rows,outputDirectory){
 rows=prepared(rows);if(!rows.length)return [];
 const root=await fs.realpath(outputDirectory),result=[];for(const a of rows){
  const file=path.join(root,a.path),temporary=path.join(root,'.asset-'+crypto.randomUUID()+'.tmp');if(file===a.file){assert.equal(await digest(file,a.bytes),a.sha256,'Staged SD asset changed');result.push({...a,file});continue;}
  try{await fs.copyFile(a.file,temporary,require('node:fs').constants.COPYFILE_EXCL);assert.equal(await digest(temporary,a.bytes),a.sha256,'Staged SD asset source changed');await fs.rename(temporary,file);}finally{await fs.rm(temporary,{force:true});}result.push({...a,file});
 }return result;
}
async function bundle(directory,{id,bytes,sha256,assets}){
 const file=path.join(directory,'sd-bundle.json'),temporary=file+'.'+crypto.randomUUID()+'.tmp',manifest={format:'iz2s.sd-bundle',version:1,id,launcher:{bytes,sha256},assets:assets.map(a=>({file:a.path,path:a.path,bytes:a.bytes,sha256:a.sha256}))};
 try{await fs.writeFile(temporary,JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});await fs.rename(temporary,file);}finally{await fs.rm(temporary,{force:true});}return file;
}
async function loadBundle(packageFile,bytes){
 const pkg=require('./package.cjs').decode(bytes),root=await fs.realpath(path.dirname(path.resolve(packageFile)));let manifest;
 try{const file=await fs.open(path.join(root,'sd-bundle.json'),'r');try{assert((await file.stat()).size<=16384,'SD bundle manifest is too large');manifest=JSON.parse(await file.readFile('utf8'));}finally{await file.close();}}
 catch(e){if(e.code!=='ENOENT')throw e;assert(!(pkg.flags&8),'This SD application requires its sd-bundle.json and companion files');return [];}
 assert(manifest.format==='iz2s.sd-bundle'&&manifest.version===1&&manifest.id===pkg.id,'SD bundle identity differs');assert(manifest.launcher?.bytes===bytes.length&&manifest.launcher.sha256===sha(bytes),'SD bundle launcher changed');
 const rows=declarations({native:{assets:manifest.assets}});assert(Array.isArray(manifest.assets),'SD bundle companion list differs');verifyBinding(pkg,rows);
 const assets=[];for(const a of rows){assert.equal(a.file,a.path,'SD bundle source name differs');const file=await fs.realpath(path.join(root,a.file));assert(file.startsWith(root+path.sep),'SD bundle asset escapes its folder');assets.push({...a,file});}return prepared(assets);
}
module.exports={MAX_ASSETS,MAX_ASSET_BYTES,MAX_TOTAL_BYTES,declarations,prepared,binding,verifyBinding,digest,inspect,inspectPacked,stage,bundle,loadBundle};
