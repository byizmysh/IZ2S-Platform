// Unload only the session's console catalog copy. The PC package stays installed.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {ID}=require('../descriptors.cjs');
async function queue(stateRoot,ids,installed,source,{target}={}){
 if(target!==undefined&&!['sd','session'].includes(target))throw Error('Unknown removal target');
 if(!Array.isArray(ids)||!ids.length||ids.length>15||ids.some(id=>typeof id!=='string'||!ID.test(id)))throw Error('Select loaded modules to remove');
 const selected=[...new Set(ids)].map(id=>{const app=installed.find(a=>a.id===id);if(!app)throw Error('Module is not loaded on the console: '+id);return app;});
 const result=[];for(const app of selected)result.push(await require('./jobs.cjs').create(stateRoot,{id:app.id,name:app.name||app.id,file:app.id+'.izm'},source,{operation:'remove',target}));return result;
}
async function remove(catalogRoot,id){
 if(typeof id!=='string'||!ID.test(id))throw Error('Invalid module id');
 const root=await fs.realpath(catalogRoot),target=path.join(root,id),stat=await fs.lstat(target);
 if(!stat.isDirectory()||stat.isSymbolicLink())throw Error('Only a session catalog directory can be unloaded');
 const manifest=JSON.parse(await fs.readFile(path.join(target,'app.json'),'utf8'));
 if(manifest.id!==id)throw Error('Catalog identity differs from removal request');
 const staged=path.join(root,'.removed-'+crypto.randomUUID());
 await fs.rename(target,staged);await fs.rm(staged,{recursive:true,force:true});
 return {id,removed:true};
}
async function atMenu(runtime,state,rpc,mapped){
 if(runtime.active||state.readFloatLE(964)>0||state.readUInt32LE(1012)>0)return false;
 return (await rpc.read(mapped+0x700004,4)).readUInt32LE()===0;
}
module.exports={queue,remove,atMenu};
