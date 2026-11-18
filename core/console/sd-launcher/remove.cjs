'use strict';
// Removes an installation, never the module's game/BIOS/save directories.
// The caller must own a stopped menu lease before invoking this mutation.
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const {paths}=require('./install.cjs');
const {decodeHeader}=require('./package.cjs');
const {binding,MAX_ASSETS,MAX_ASSET_BYTES}=require('./assets.cjs');
const {MAX_CHUNK}=require('./storage.cjs');

async function readFile(storage,path,read){
 const file=await storage.openRead(path);
 try{return await read(file);}
 finally{
  if(!storage.uncertain)await storage.close(file);
 }
}

async function provenAssets(storage,root,rows,header){
 const expected=header.assetManifest;
 if(!expected.count||expected.count>MAX_ASSETS)return [];
 const choices=rows.filter(row=>row.type==='file'&&/^asset-[a-f0-9]{64}\.izdata$/.test(row.name));
 if(choices.length!==expected.count)return [];
 const candidates=[];
 for(const {name} of choices){
  const bytes=await readFile(storage,root+'/'+name,file=>file.size);
  if(bytes<=0||bytes>MAX_ASSET_BYTES)return [];
  candidates.push({path:name,bytes,sha256:name.slice(6,70)});
 }
 if(candidates.reduce((sum,asset)=>sum+asset.bytes,0)!==expected.bytes)return [];
 if(binding(candidates).sha256!==expected.sha256)return [];
 const verified=[];
 for(const asset of candidates){
  const digest=await readFile(storage,root+'/'+asset.path,async file=>{
   if(file.size!==asset.bytes)return null;
   const hash=crypto.createHash('sha256');
   for(let offset=0;offset<file.size;offset+=MAX_CHUNK)
    hash.update(await storage.read(file,offset,Math.min(MAX_CHUNK,file.size-offset)));
   return hash.digest('hex');
  });
  if(digest===asset.sha256)verified.push(asset.path);
 }
 return verified;
}

async function remove(storage,id,{lease}={}){
 assert(lease&&lease.active===true,'SD removal requires a held menu lease');
 assert(!storage.uncertain,'Previous SD operation outcome is unknown');
 const p=paths(id),rows=await storage.list(p.virtualPath),entry=rows.find(r=>r.name==='launcher.iza');
 assert(entry&&entry.type==='file','The SD application is no longer installed');
 const header=await readFile(storage,p.packagePath,async file=>decodeHeader(await storage.read(file,0,512),file.size));
 assert.equal(header.id,id,'SD application identity differs');
 const ownedAssets=await provenAssets(storage,p.virtualPath,rows,header);
 const stage=p.virtualPath+'/.removed-'+crypto.randomBytes(8).toString('hex')+'.iza';
 // Unpublish first. Never restore this name after deleting any companion.
 const deleted=[];let unpublished=false;
 try{
  const pendingUpdates=rows.filter(row=>row.type==='file'&&/^\.launcher-(?:[a-f0-9]{16}\.(?:tmp|bak)|update\.bak)$/.test(row.name));
  let updatesRemoved=false;
  for(const row of pendingUpdates){
   const name=p.virtualPath+'/'+row.name;
   const owner=await readFile(storage,name,async file=>{
    if(file.size<512)return null;
    const bytes=await storage.read(file,0,512);
    try{return decodeHeader(bytes,file.size).id;}
    catch(error){if(error.code!=='ERR_ASSERTION')throw error;return null;}
   });
   if(owner!==id)continue;
   assert(lease.active===true,'SD menu lease was released');
   await storage.remove(name);updatesRemoved=true;
  }
  if(updatesRemoved)await storage.commit();
  assert(lease.active===true,'SD menu lease was released');
  await storage.rename(p.packagePath,stage);unpublished=true;await storage.commit();
  for(const name of ownedAssets){
   assert(lease.active===true,'SD menu lease was released');await storage.remove(p.virtualPath+'/'+name);deleted.push(name);
  }
  await storage.remove(stage);deleted.push('launcher.iza');await storage.commit();
  return {id,removed:true,target:'sd',path:p.packagePath,deleted,gameFilesPreserved:true};
 }catch(e){e.sdUnpublished=unpublished;e.deleted=[...deleted];if(storage.uncertain)e.resourcesRetained=true;throw e;}
}
async function removeModule({directory,id,lease}){
 const path=require('node:path'),a=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));let storage,retained=false;
 try{storage=await require('./storage.cjs').create(a);return await remove(storage,id,{lease});}
 catch(e){retained=!!e.resourcesRetained;throw e;}
 finally{if(storage&&!storage.uncertain&&!retained)await a.close();}
}
module.exports={remove,removeModule};
