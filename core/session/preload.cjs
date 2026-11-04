const path=require('node:path');
const cacheRoot=root=>path.join(root,'data/state/module-cache');
async function qualifyResident(repo,packages,options){
 if(!options.residentGraphics)return;
 const fs=require('node:fs/promises'),temp=await fs.mkdtemp(path.join(require('node:os').tmpdir(),'run18-ram-preflight-'));
 try{
  const apps=[];for(const pkg of packages){await repo.install(pkg.file,temp);const app=await require('../services/app-catalog.cjs').readApp(path.join(temp,pkg.id),{validate:false});if(app.manifest.hostRuntime)require('../../shells/izlauncher/resident/services.cjs').policy(app);apps.push(app);}
  const memory=require('../../shells/izlauncher/resident/memory.cjs');memory.create({baseBytes:memory.BASE_BYTES}).plan(apps);
 }
 finally{await fs.rm(temp,{recursive:true,force:true});}
}
async function resolve(root,ids,options={}){
 if(!Array.isArray(ids)||ids.length>15||ids.some(id=>typeof id!=='string'))throw Error('Choose up to 15 available modules');
 if(!ids.length){if(options.residentGraphics)require('../../shells/izlauncher/resident/compatibility.cjs').assertSelection([]);return [];}
 const repo=await require('../packages/repository.cjs').create(root,options),list=await repo.list();
 const packages=[...new Set(ids)].map(id=>{const pkg=list.packages.find(p=>p.id===id||p.file===id);if(!pkg)throw Error('Selected module is no longer available: '+id);return pkg;});
 if(options.residentGraphics)require('../../shells/izlauncher/resident/compatibility.cjs').assertSelection(packages,options);
 await qualifyResident(repo,packages,options);
 const selected=packages.map(pkg=>{if(pkg.supported===false)throw Error('Module incompatible with this host or shell: '+pkg.id);return pkg.id;});
 await require('../modules.cjs').verify(root,selected);return selected;
}
async function prepare(root,ids,destination,options={}){
 if(!ids.length){if(options.residentGraphics)require('../../shells/izlauncher/resident/compatibility.cjs').assertSelection([]);return;}
 const repo=await require('../packages/repository.cjs').create(root,options),list=await repo.list();
 const packages=ids.map(id=>{const pkg=list.packages.find(p=>p.id===id);if(!pkg)throw Error('Selected module is no longer available: '+id);return pkg;});
 if(options.residentGraphics)require('../../shells/izlauncher/resident/compatibility.cjs').assertSelection(packages,options);
 for(const pkg of packages)await repo.install(pkg.file,destination);
 const catalog=await require('../services/app-catalog.cjs').scan(destination);if(catalog.rejected.length)throw Error(catalog.rejected.map(a=>a.error).join('; '));
 if(options.residentGraphics&&options.residentFiniteAudio)for(const app of catalog.apps)if(app.manifest.hostRuntime)require('../../shells/izlauncher/resident/services.cjs').policy(app);
 await require('../packages/shell-builder.cjs').build(catalog.apps,cacheRoot(root),{root,shell:options.shell||process.env.IZ2S_SHELL||process.env.IZ15_SHELL||'izlauncher'});
}
module.exports={resolve,prepare,cacheRoot};
