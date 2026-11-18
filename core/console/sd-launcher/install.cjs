'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const {BASE,MAX_CHUNK}=require('./storage.cjs'),{crc32,decode,decodeHeader,build}=require('./package.cjs');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const modulePaths=require('../../module-storage.cjs');
function paths(id){assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id));const p=modulePaths.paths(id);return {...p,packagePath:p.virtualPath+'/launcher.iza'};}
async function ensure(storage,id){
 const components=paths(id).path.slice(1).split('/');
 let current='/sd';
 for(const component of components){
  const matches=(await storage.list(current)).filter(row=>row.name.toLowerCase()===component);
  assert(matches.length<=1,'Ambiguous SD application directory');
  const [existing]=matches;
  if(existing)assert.equal(existing.type,'dir','A file blocks the SD application directory');
  current+='/'+component;
  if(!existing)await storage.mkdir(current);
 }
 return current;
}
function verifyReadback(matches,message){if(!matches)throw Object.assign(Error(message),{code:'SD_READBACK_MISMATCH'});}
async function readback(storage,target,size,expected,notify,chunks){
 let file;async function close(){const current=file;file=null;try{await storage.close(current);}catch(e){e.resourcesRetained=true;throw e;}}
 try{
  file=await storage.openRead(target);verifyReadback(file.size===size,'SD file size differs');const check=chunks?null:crypto.createHash('sha256');
  const native=typeof storage.readHash==='function'&&!!chunks;
  let index=0;for(let offset=0;offset<size;){const count=Math.min(MAX_CHUNK,size-offset);
   const part=chunks?.[index++];
   if(chunks)assert(part&&part.offset===offset&&part.bytes===count&&/^[a-f0-9]{64}$/.test(part.sha256),'Incomplete SD verification plan');
   if(native)verifyReadback(await storage.readHash(file,offset,count)===part.sha256,'Full SD readback differs');
   else{
    const chunk=await storage.read(file,offset,count);
    assert(Buffer.isBuffer(chunk)&&chunk.length===count,'Short SD readback');
    if(part)verifyReadback(hash(chunk)===part.sha256,'Full SD readback differs');
    else check.update(chunk);
   }
   offset+=count;await notify('verify',count);
  }
  await close();if(chunks)assert.equal(index,chunks.length,'SD verification plan exceeds file');else verifyReadback(check.digest('hex')===expected,'Full SD readback differs');
  return {method:native?'console-sha256-chunks-after-reopen':'full-readback-sha256-after-reopen',readBytes:size,digestBytes:native?chunks.length*32:32,downloadedFileBytes:native?0:size};
 }catch(e){if(file&&!storage.uncertain&&!e.resourcesRetained)try{await close();}catch(cleanup){e.cleanupError=cleanup.message;e.resourcesRetained=true;}if(storage.uncertain)e.resourcesRetained=true;throw e;}
}
async function transfer(storage,target,source,notify){
 let file;const chunks=[];async function close(){const current=file;file=null;try{await storage.close(current);}catch(e){e.resourcesRetained=true;throw e;}}
 try{
  file=await storage.openWrite(target,source.bytes);const check=crypto.createHash('sha256');
  for(let offset=0;offset<source.bytes;){const count=Math.min(MAX_CHUNK,source.bytes-offset),chunk=await source.read(offset,count);assert(Buffer.isBuffer(chunk)&&chunk.length===count,'Short local SD source');check.update(chunk);chunks.push({offset,bytes:count,sha256:hash(chunk)});await storage.write(file,offset,chunk);offset+=count;await notify('write',count);}
  assert.equal(check.digest('hex'),source.sha256,'Local SD source changed during transfer');await storage.flush(file);await close();
 }catch(e){if(file&&!storage.uncertain&&!e.resourcesRetained)try{await close();}catch(cleanup){e.cleanupError=cleanup.message;e.resourcesRetained=true;}if(storage.uncertain)e.resourcesRetained=true;throw e;}
 return readback(storage,target,source.bytes,source.sha256,notify,chunks);
}
async function verificationPlan(source){
 const check=crypto.createHash('sha256'),chunks=[];
 for(let offset=0;offset<source.bytes;){const count=Math.min(MAX_CHUNK,source.bytes-offset),chunk=await source.read(offset,count);assert(Buffer.isBuffer(chunk)&&chunk.length===count,'Short local SD verification source');check.update(chunk);chunks.push({offset,bytes:count,sha256:hash(chunk)});offset+=count;}
 assert.equal(check.digest('hex'),source.sha256,'Local SD verification source changed');return chunks;
}
async function localSource(asset){
 const file=await fs.open(asset.file,'r');try{const stat=await file.stat();assert(stat.isFile()&&stat.size===asset.bytes,'SD asset source size changed');}catch(e){await file.close();throw e;}
 const buffer=Buffer.alloc(MAX_CHUNK);return {bytes:asset.bytes,sha256:asset.sha256,async read(offset,count){let done=0;while(done<count){const {bytesRead}=await file.read(buffer,done,count-done,offset+done);assert(bytesRead>0,'Short local SD asset');done+=bytesRead;}return buffer.subarray(0,count);},close:()=>file.close()};
}
async function publish(storage,stage,target,backup){
 if(backup)await storage.rename(target,backup);
 try{await storage.rename(stage,target);}catch(error){
  // Roll back only a confirmed native rejection, never a lost acknowledgement.
  if(backup&&error.nativeResult!==undefined&&!storage.uncertain&&!error.resourcesRetained){try{await storage.rename(backup,target);await storage.commit();error.previousVersionRestored=true;}catch(restore){error.restoreError=restore.message;if(storage.uncertain||restore.resourcesRetained)error.resourcesRetained=true;}}
  throw error;
 }
 await storage.commit();if(backup){await storage.remove(backup);await storage.commit();}
}
function interruptedBackup(entries){
 const files=entries.filter(row=>row.type==='file');
 if(files.length>4096)return null;
 const names=new Set(files.map(row=>row.name));
 const backups=files.filter(row=>/^\.launcher-[a-f0-9]{16}\.bak$/.test(row.name)&&names.has(row.name.slice(0,-4)+'.tmp'));
 return backups.length===1?backups[0].name:null;
}
function checkedHeader(header,size,id){
 try{const info=decodeHeader(header,size);return info.id===id?info:null;}
 catch(error){if(error.code!=='ERR_ASSERTION')throw error;return null;}
}
async function readExistingHeader(storage,target,id){
 const file=await storage.openRead(target);
 try{
  if(file.size<512)return null;
  const header=await storage.read(file,0,512);
  assert.equal(header.length,512,'Short SD application header read');
  return checkedHeader(header,file.size,id);
 }finally{
  if(!storage.uncertain)try{await storage.close(file);}catch(error){error.resourcesRetained=true;throw error;}
 }
}
function checkExecutionMode(previous,next){
 if(previous&&!previous.delegated&&next.delegated)
  throw Object.assign(Error('This update requires the PC server. The existing autonomous SD version was kept.'),{code:'SD_AUTONOMY_REGRESSION'});
}
async function restoreInterruptedInstall(storage,directory,destination,pkg,entries){
 const candidate=interruptedBackup(entries);
 if(!candidate)return false;
 const backup=directory+'/'+candidate,previous=await readExistingHeader(storage,backup,pkg.id);
 if(!previous)return false;
 checkExecutionMode(previous,pkg);
 // A fresh listing proved both the previous image and its unpublished
 // replacement exist. Restore only the previous image under its normal name.
 await storage.rename(backup,destination);await storage.commit();
 return true;
}
async function installBytes(storage,bytes,{progress=()=>{},assets=[]}={}){
 if(storage.uncertain)throw Object.assign(Error('Previous SD transfer outcome is unknown'),{resourcesRetained:true});
 assets=require('./assets.cjs').prepared(assets);const pkg=decode(bytes),{id}=pkg;const assetManifest=require('./assets.cjs').verifyBinding(pkg,assets);const digest=hash(bytes),directory=await ensure(storage,id),destination=paths(id).packagePath,nonce=crypto.randomBytes(8).toString('hex'),stage=directory+'/.launcher-'+nonce+'.tmp',backup=directory+'/.launcher-'+nonce+'.bak';
 const entries=await storage.list(directory),old=entries.find(r=>r.name.toLowerCase()==='launcher.iza');
 if(old)assert(old.type==='file'&&old.name==='launcher.iza','A conflicting entry blocks the SD launcher');
 if(old&&pkg.delegated)checkExecutionMode(await readExistingHeader(storage,destination,id),pkg);
 const replacing=!!old||await restoreInterruptedInstall(storage,directory,destination,pkg,entries);
 const total=bytes.length+assets.reduce((n,a)=>n+a.bytes,0),completed={write:0,verify:0},receipts=[];
 const notify=name=>async(phase,count)=>{completed[phase]+=count;assert(completed[phase]<=total);await progress({phase,bytes:completed[phase],total,file:name});};
 function trackedVerification(report){
  let reported=0;
  return ()=>{
   let checked=0;
   return async(phase,count)=>{
    if(phase==='write')return report(phase,count);
    checked+=count;
    // A replacement can verify the same prefix again. Count each byte once.
    if(checked>reported){const difference=checked-reported;reported=checked;await report('verify',difference);}
   };
  };
 }
 for(const asset of assets){
  const target=directory+'/'+asset.path,existing=entries.find(r=>r.name.toLowerCase()===asset.path.toLowerCase()),report=notify(asset.path);
  if(existing)assert(existing.type==='file'&&existing.name===asset.path,'A conflicting entry blocks an immutable SD asset');
  let verification,repaired=false;
  const source=await localSource(asset);
  try{
   const verificationProgress=trackedVerification(report);
   if(existing){
    const chunks=typeof storage.readHash==='function'?await verificationPlan(source):undefined;
    try{verification=await readback(storage,target,asset.bytes,asset.sha256,verificationProgress(),chunks);}catch(error){
     if(error.code!=='SD_READBACK_MISMATCH'||storage.uncertain||error.resourcesRetained)throw error;
     repaired=true;
    }
   }
   if(verification)completed.write+=asset.bytes;
   else{
    const temporary=directory+'/.asset-'+crypto.randomBytes(8).toString('hex');
    verification=await transfer(storage,temporary+'.tmp',source,verificationProgress());
    await publish(storage,temporary+'.tmp',target,repaired?temporary+'.bak':null);
   }
  }finally{await source.close();}
  receipts.push({relativePath:asset.path,path:target,displayPath:'SD:'+target.slice(3),bytes:asset.bytes,sha256:asset.sha256,readbackVerified:true,reused:!!existing&&!repaired,...(repaired?{repaired}:{}),verification});
 }
 const source={bytes:bytes.length,sha256:digest,read:async(offset,count)=>bytes.subarray(offset,offset+count)},report=notify('launcher.iza'),verificationProgress=trackedVerification(report);
 let verification;
 if(replacing){
  const chunks=await verificationPlan(source);
  try{verification=await readback(storage,destination,bytes.length,digest,verificationProgress(),chunks);}
  catch(error){if(error.code!=='SD_READBACK_MISMATCH'||storage.uncertain||error.resourcesRetained)throw error;}
 }
 const reused=!!verification;
 if(reused)await report('write',bytes.length);
 else{
  verification=await transfer(storage,stage,source,verificationProgress());
  await publish(storage,stage,destination,replacing?backup:null);
 }
 return {id,path:destination,displayPath:'SD:'+destination.slice(3),bytes:bytes.length,sha256:digest,headerSha256:hash(bytes.subarray(0,512)),headerCrc:require('./package.cjs').crc32(bytes.subarray(0,512)),readbackVerified:true,...(reused?{reused}:{}),verification,assets:receipts,assetManifest,totalBytes:total,...(storage.metrics?{transferMetrics:storage.metrics}:{})};
}
async function installModule({root,directory,id,shell,progress,expectedSha256,storage:providedStorage}){
 const location=paths(id),repo=await require('../../packages/repository.cjs').create(root,{shell,source:'current'}),pkg=await repo.inspect(id+'.module.zip');assert(pkg.supported,'This module is not supported by the selected launcher');
 const buildRoot=path.join(root,'data/build-cache/sd-apps',id+'-'+crypto.randomUUID());let attachment,storage=providedStorage,retained=false;
 try{
  await repo.install(pkg.file,buildRoot,{expectedSha256:expectedSha256??pkg.sha256});
  const appRoot=path.join(buildRoot,id);
  // The repository classifies the actual contents. A failed local adapter
  // must never be treated as a request to install a PC-only executor.
  const pcManaged=pkg.sd?.adapter==='pc-managed';
  const result=pcManaged
   ?await require('./managed-package.cjs').build(pkg,await require('../../services/app-catalog.cjs').readApp(appRoot),path.join(buildRoot,'managed'))
   :await build(appRoot,path.join(buildRoot,'package'));
  if(!storage){attachment=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));storage=await require('./storage.cjs').create(attachment);}
  // An existing visible package already occupies a slot. Only a new slot needs
  // the capacity scan, which stops as soon as the SD menu is full.
  const existing=await inspectInstalled(storage,location,id,true,true);
  if(!existing?.visible){
   const visible=await inventory(storage,{identities:true,validLimit:15,exclude:id});
   assert(visible.length<15,'The SD menu supports 15 applications; remove an existing application first');
  }
  return {...await installBytes(storage,result.bytes,{progress,assets:result.assets}),name:pkg.name,version:pkg.version,execution:result.execution||'console'};
 }catch(e){retained=!!e.resourcesRetained;throw e;}finally{
  // The package is a temporary transfer source. App launch never reads it.
  await fs.rm(buildRoot,{recursive:true,force:true});if(attachment&&storage&&!storage.uncertain&&!retained)await attachment.close();
 }
}
function missing(storage,error){
 return !storage.uncertain&&!error.resourcesRetained&&(error.nativeResult?.canonical??error.nativeResult)===0x202;
}
async function inspectInstalled(storage,p,id,identities,checkVisibility=false,identifyDigest){
 let file;
 try{file=await storage.openRead(p.packagePath);}catch(error){
  if(!missing(storage,error))throw error;
  if(!checkVisibility)return null;
  let entries;
  try{entries=await storage.list(p.virtualPath);}catch(error){if(missing(storage,error))return null;throw error;}
  if(entries.some(row=>row.name.toLowerCase()==='launcher.iza'))return null;
  const candidate=interruptedBackup(entries);
  if(!candidate)return null;
  try{file=await storage.openRead(p.virtualPath+'/'+candidate);}catch(error){if(missing(storage,error))return null;throw error;}
 }
 let headerSha256,headerValid,metadata,sha256,visible=false;
 try{
  if(identities)headerValid=false;
  if(identities&&file.size>=512){
   const header=await storage.read(file,0,512);assert.equal(header.length,512);
   headerSha256=hash(header);
   const info=checkedHeader(header,file.size,id);
   if(info){
    headerValid=true;metadata={headerCrc:crc32(header),name:info.name,execution:info.delegated?'pc':'console'};
    if(identifyDigest?.(id,file.size)){
     const digest=crypto.createHash('sha256');
     digest.update(header);
     for(let offset=header.length;offset<file.size;){
      const length=Math.min(MAX_CHUNK,file.size-offset),bytes=await storage.read(file,offset,length);
      assert.equal(bytes.length,length,'Short SD version readback');
      digest.update(bytes);offset+=length;
     }
     sha256=digest.digest('hex');
    }
    visible=true;
    if(checkVisibility&&info.delegated){
     // Native tiles can regenerate their icon. PC-managed tiles cannot.
     const {offset,length,crc}=info.ranges.tile;
     const tile=await storage.read(file,offset,length);
     assert.equal(tile.length,length,'Short SD tile read');
     visible=crc32(tile)===crc;
    }
   }
  }
 }finally{
  if(!storage.uncertain)try{await storage.close(file);}catch(error){error.resourcesRetained=true;throw error;}
 }
 return {id,headerSha256,sha256,...(identities?{headerValid,...metadata}:{}),...(checkVisibility?{visible}:{}),path:p.packagePath,displayPath:'SD:'+p.path+'/launcher.iza',bytes:file.size,source:'sd',readbackVerified:false};
}
async function inventory(storage,{identities=false,validLimit,exclude,identifyDigest}={}){
 let rows;
 try{rows=await storage.list(BASE);}catch(error){if(missing(storage,error))return [];throw error;}
 const apps=[],checkVisibility=validLimit!==undefined;assert(rows.length<=200000);
 for(const row of rows){
  if(modulePaths.identity(row.name)===exclude||row.type!=='dir'||!/^[a-z0-9][a-z0-9-]{0,46}$/.test(row.name))continue;
  // Probe the fixed path first. Only the capacity check needs to look for
  // interrupted installations when that path is absent.
  const app=await inspectInstalled(storage,paths(row.name),modulePaths.identity(row.name),identities||checkVisibility,checkVisibility,identifyDigest);
  if(!app||(checkVisibility&&!app.visible))continue;
  apps.push(app);
  if(apps.length===validLimit)break;
 }
 return apps;
}
module.exports={paths,ensure,installBytes,installModule,inventory,interruptedBackup};
