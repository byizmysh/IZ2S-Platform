'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
function requiresFilesystem(manifest={}){
 if(manifest.requirements?.filesystem!==undefined)return manifest.requirements.filesystem===true;
 // Legacy native RAM entries borrow FS/RO/process handles from this service.
 // A future independent executor can explicitly declare filesystem:false.
 return !!(manifest.native&&manifest.hostRuntime&&manifest.native.filesystem!==false);
}
async function preflight({manifest,directory,filesystem}){
 if(!requiresFilesystem(manifest))return;
 let ready=false;
 if(filesystem!==false){try{
  const state=JSON.parse(await fs.readFile(path.join(directory,'fs-rpc-state.json'),'utf8'));
  if(state.state==='ready'){
   const base=path.join(directory,'fs-rpc/switch2');
   const [acquisition,root]=await Promise.all(['current-acquisition.json','ro/root/current-root.json'].map(file=>fs.readFile(path.join(base,file),'utf8').then(JSON.parse)));
   ready=!!(acquisition.socketPath&&root.rootHandle);
  }
 }catch{}}
 if(!ready)throw Object.assign(Error('This application requires filesystem access. Start a new session with Filesystem access enabled.'),{preflight:true,code:'IZ2S_FILESYSTEM_REQUIRED'});
}
module.exports={requiresFilesystem,preflight};
