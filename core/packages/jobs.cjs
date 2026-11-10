const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),files=require('./files.cjs');
async function all(root){const folder=path.join(root,'jobs');await fs.mkdir(folder,{recursive:true});const out=[];for(const name of await fs.readdir(folder))if(/^[a-f0-9-]+\.json$/.test(name)){const value=await files.read(path.join(folder,name));if(value)out.push(value);}return out.sort((a,b)=>a.created.localeCompare(b.created));}
async function create(root,pkg,source,{operation='load',target,requestKey,expectedSha256=target==='sd'?pkg.sha256:undefined}={}){
 if(!['load','remove'].includes(operation))throw Error('Unknown module operation');
 if(target!==undefined&&!['sd','session'].includes(target))throw Error('Unknown package target');
 if(requestKey!==undefined&&(!/^[a-zA-Z0-9:-]{1,192}$/.test(requestKey)||target!=='sd'))throw Error('Invalid native SD request key');
 if(expectedSha256!==undefined&&!/^[a-f0-9]{64}$/.test(expectedSha256))throw Error('Invalid selected package hash');
 if(requestKey&&expectedSha256===undefined)throw Error('Native SD requests require a selected package hash');
 await fs.mkdir(root,{recursive:true});const lock=path.join(root,'queue.lock'),until=Date.now()+6000;
 for(;;)try{await fs.mkdir(lock);break;}catch(e){if(e.code!=='EEXIST')throw e;const stat=await fs.stat(lock).catch(()=>null);if(stat&&Date.now()-stat.mtimeMs>30000){await fs.rm(lock,{recursive:true,force:true});continue;}if(Date.now()>until)throw Error('Package queue is busy; try again');await new Promise(r=>setTimeout(r,20));}
 try{
  const rows=await all(root),previous=requestKey&&rows.find(j=>j.requestKeys?.includes(requestKey));
  if(previous){if(previous.packageId!==pkg.id||previous.target!==target||(previous.operation||'load')!==operation||previous.expectedSha256!==expectedSha256)throw Error('Native SD request identity changed');return previous;}
  const current=rows.find(j=>j.packageId===pkg.id&&(j.target||'session')===(target||'session')&&['queued','loading','publishing'].includes(j.status));
  if(current){
   if((current.operation||'load')!==operation)throw Error('Another operation is already queued for this module');
   let changed=false;if(expectedSha256&&current.expectedSha256!==expectedSha256){if(current.expectedSha256||current.status!=='queued')throw Error('Another version is already queued for this module');current.expectedSha256=expectedSha256;changed=true;}
   if(requestKey){current.requestKeys=[...new Set([...(current.requestKeys||[]),requestKey])];changed=true;}if(changed)await save(root,current);return current;
  }
  const row={id:crypto.randomUUID(),packageId:pkg.id,file:pkg.file,name:pkg.name,source,operation,...(target?{target}:{}),...(expectedSha256?{expectedSha256}:{}),...(requestKey?{requestKeys:[requestKey]}:{}),status:'queued',created:new Date().toISOString()};
  await save(root,row);await files.event(root,'queue',pkg.name+(operation==='remove'?' queued for removal':target==='sd'?' queued for SD installation':' queued'),{packageId:pkg.id,source});return row;
 }finally{await fs.rm(lock,{recursive:true,force:true});}
}
async function save(root,row){await files.write(path.join(root,'jobs',row.id+'.json'),row);}
module.exports={all,create,save};
