'use strict';
// The card is authoritative. Cache only exact SHA-bound source bytes, and
// validate card presence/content again before using a cached PC executor.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const P=require('./package.cjs'),archive=require('../../archive.cjs'),{MAX_CHUNK}=require('./storage.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
async function read(storage,file,size){
 const parts=[];for(let at=0;at<size;at+=MAX_CHUNK){const n=Math.min(MAX_CHUNK,size-at),bytes=await storage.read(file,at,n);assert(Buffer.isBuffer(bytes)&&bytes.length===n,'Short SD package read');parts.push(Buffer.from(bytes));}return Buffer.concat(parts);
}
async function loadFromStorage({storage,directory,id,lease}){
 assert(lease?.active===true,'An SD application needs a held display boundary');const p=require('./install.cjs').paths(id);let file;
 async function close(){const handle=file;file=null;try{await storage.close(handle);}catch(error){error.resourcesRetained=true;throw error;}}
 try{
  file=await storage.openRead(p.packagePath);assert(file.size===131584,'Invalid managed SD descriptor size');const descriptor=P.decode(await read(storage,file,file.size));await close();assert(descriptor.delegated&&descriptor.id===id,'The SD application identity changed');
  const source=descriptor.source,cache=path.join(directory,'sd-executors',id+'-'+source.sha256),sourceFile=path.join(cache,'source.izm');let cached;
  try{const bytes=await fs.readFile(sourceFile);if(bytes.length===source.bytes&&sha(bytes)===source.sha256)cached=bytes;}catch(error){if(error.code!=='ENOENT')throw error;}
  file=await storage.openRead(p.virtualPath+'/'+source.path);assert.equal(file.size,source.bytes,'SD application source size differs');let bytes;
  if(cached&&typeof storage.readHash==='function'){
   for(let at=0;at<source.bytes;at+=MAX_CHUNK){const n=Math.min(MAX_CHUNK,source.bytes-at);assert.equal(await storage.readHash(file,at,n),sha(cached.subarray(at,at+n)),'SD application source changed');}bytes=cached;
  }else{bytes=await read(storage,file,source.bytes);assert.equal(sha(bytes),source.sha256,'SD application source checksum differs');}
  await close();const pkg=archive.inspect(bytes);assert.equal(pkg.descriptor.id,id,'SD package identity differs');assert(pkg.descriptor.switch,'The stored package has no console application');
  const target=path.join(cache,'module');await fs.mkdir(cache,{recursive:true});
  // Unpack/validate each launch. A same-name edited PC cache can never replace
  // immutable card bytes or silently supply another version's host code.
  await fs.rm(target,{recursive:true,force:true});await archive.unpack(bytes,target);await fs.writeFile(sourceFile,bytes);
  const app=await require('../../services/app-catalog.cjs').readApp(path.join(target,'switch'));assert.equal(app.id,id);
  const getComponent=async(moduleId,name)=>{
   if(moduleId!==id)return require('../../components.cjs').get(moduleId,name);
   assert(pkg.descriptor.server?.entry,'This SD package has no host executor');const entry=require(path.join(target,pkg.descriptor.server.entry));assert(typeof entry.load==='function','SD host executor has no component loader');return entry.load(name);
  };
  return {root:app.root,id:app.id,fingerprint:app.fingerprint,source:'sd',execution:'pc',getComponent};
 }catch(error){if(storage.uncertain)error.resourcesRetained=true;throw error;}
 finally{if(file&&!storage.uncertain)await close();}
}
async function load(options){
 const a=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(options.directory,'fs-rpc'));let storage,retained=false;
 try{storage=await require('./storage.cjs').create(a);return await loadFromStorage({...options,storage});}
 catch(error){retained=!!error.resourcesRetained;throw error;}
 finally{if(storage&&!storage.uncertain&&!retained)await a.close();}
}
module.exports={load,loadFromStorage};
