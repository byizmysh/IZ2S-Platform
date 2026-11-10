'use strict';
// Removes an installation, never the module's game/BIOS/save directories.
// The caller must own a stopped menu lease before invoking this mutation.
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const {paths}=require('./install.cjs');
async function remove(storage,id,{lease}={}){
 assert(lease&&lease.active===true,'SD removal requires a held menu lease');
 assert(!storage.uncertain,'Previous SD operation outcome is unknown');
 const p=paths(id),rows=await storage.list(p.virtualPath),entry=rows.find(r=>r.name==='launcher.izapp');
 assert(entry&&entry.type==='file','The SD application is no longer installed');
 const stage=p.virtualPath+'/.removed-'+crypto.randomBytes(8).toString('hex')+'.izapp';
 // Unpublish first. Never restore this name after deleting any companion.
 const deleted=[];let unpublished=false;
 try{
  assert(lease.active===true,'SD menu lease was released');
  await storage.rename(p.packagePath,stage);unpublished=true;await storage.commit();
  for(const row of rows){
   if(row.type!=='file'||!/^asset-[a-f0-9]{64}\.izdata$/.test(row.name))continue;
   assert(lease.active===true,'SD menu lease was released');await storage.remove(p.virtualPath+'/'+row.name);deleted.push(row.name);
  }
  await storage.remove(stage);deleted.push('launcher.izapp');await storage.commit();
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
