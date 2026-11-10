'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const {BASE,MAX_CHUNK}=require('./storage.cjs'),{decode,build}=require('./package.cjs');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const modulePaths=require('../../module-storage.cjs');
function paths(id){assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id));const p=modulePaths.paths(id);return {...p,packagePath:p.virtualPath+'/launcher.izapp'};}
async function ensure(storage,id){assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id));let current='/sd';for(const component of ['IZ2S','Homebrew','Modules',id]){const rows=await storage.list(current);const existing=rows.find(r=>r.name.toLowerCase()===component.toLowerCase());if(existing){assert.equal(existing.type,'dir','A file blocks the SD application directory');assert.equal(existing.name,component,'SD directory letter case differs from the native path');}else await storage.mkdir(current+'/'+component);current+='/'+component;}return current;}
async function readback(storage,target,size,expected,notify,chunks){
 let file;async function close(){const current=file;file=null;try{await storage.close(current);}catch(e){e.resourcesRetained=true;throw e;}}
 try{
  file=await storage.openRead(target);assert.equal(file.size,size,'SD file size differs');const check=crypto.createHash('sha256');
  const native=typeof storage.readHash==='function'&&Array.isArray(chunks);
  let index=0;for(let offset=0;offset<size;){const count=Math.min(MAX_CHUNK,size-offset);
   if(native){const part=chunks[index++];assert(part&&part.offset===offset&&part.bytes===count&&/^[a-f0-9]{64}$/.test(part.sha256),'Incomplete SD verification plan');assert.equal(await storage.readHash(file,offset,count),part.sha256,'Full SD readback differs');}
   else{const chunk=await storage.read(file,offset,count);assert(Buffer.isBuffer(chunk)&&chunk.length===count,'Short SD readback');check.update(chunk);}
   offset+=count;await notify('verify',count);
  }
  await close();if(native)assert.equal(index,chunks.length,'SD verification plan exceeds file');else assert.equal(check.digest('hex'),expected,'Full SD readback differs');
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
async function installBytes(storage,bytes,{progress=()=>{},assets=[]}={}){
 if(storage.uncertain)throw Object.assign(Error('Previous SD transfer outcome is unknown'),{resourcesRetained:true});
 assets=require('./assets.cjs').prepared(assets);const pkg=decode(bytes),{id}=pkg;const assetManifest=require('./assets.cjs').verifyBinding(pkg,assets);const digest=hash(bytes),directory=await ensure(storage,id),destination=paths(id).packagePath,nonce=crypto.randomBytes(8).toString('hex'),stage=directory+'/.launcher-'+nonce+'.tmp',backup=directory+'/.launcher-'+nonce+'.bak';
 const entries=await storage.list(directory),old=entries.find(r=>r.name==='launcher.izapp');if(old)assert.equal(old.type,'file');
 const total=bytes.length+assets.reduce((n,a)=>n+a.bytes,0),completed={write:0,verify:0},receipts=[];
 const notify=name=>async(phase,count)=>{completed[phase]+=count;assert(completed[phase]<=total);await progress({phase,bytes:completed[phase],total,file:name});};
 for(const asset of assets){
  const target=directory+'/'+asset.path,existing=entries.find(r=>r.name.toLowerCase()===asset.path.toLowerCase()),report=notify(asset.path);
  let verification;
  if(existing){assert(existing.type==='file'&&existing.name===asset.path,'A conflicting entry blocks an immutable SD asset');let source;try{source=await localSource(asset);const chunks=typeof storage.readHash==='function'?await verificationPlan(source):undefined;verification=await readback(storage,target,asset.bytes,asset.sha256,report,chunks);}finally{await source?.close();}completed.write+=asset.bytes;}
  else{
   const temporary=directory+'/.asset-'+crypto.randomBytes(8).toString('hex')+'.tmp',source=await localSource(asset);
   try{verification=await transfer(storage,temporary,source,report);}finally{await source.close();}
   await storage.rename(temporary,target);await storage.commit();
  }
  receipts.push({relativePath:asset.path,path:target,displayPath:'SD:'+target.slice(3),bytes:asset.bytes,sha256:asset.sha256,readbackVerified:true,reused:!!existing,verification});
 }
 const verification=await transfer(storage,stage,{bytes:bytes.length,sha256:digest,read:async(offset,count)=>bytes.subarray(offset,offset+count)},notify('launcher.izapp'));
 if(old)await storage.rename(destination,backup);
 try{await storage.rename(stage,destination);}catch(error){
  // Roll back only a confirmed native rejection, never a lost acknowledgement.
  if(old&&error.nativeResult!==undefined&&!storage.uncertain){try{await storage.rename(backup,destination);await storage.commit();error.previousVersionRestored=true;}catch(restore){error.restoreError=restore.message;}}
  throw error;
 }
 await storage.commit();if(old){await storage.remove(backup);await storage.commit();}
 return {id,path:destination,displayPath:'SD:'+destination.slice(3),bytes:bytes.length,sha256:digest,headerSha256:hash(bytes.subarray(0,512)),readbackVerified:true,verification,assets:receipts,assetManifest,totalBytes:total,...(storage.metrics?{transferMetrics:storage.metrics}:{})};
}
async function installModule({root,directory,id,shell,progress,expectedSha256,storage:providedStorage}){
 paths(id);const repo=await require('../../packages/repository.cjs').create(root,{shell,source:'current'}),pkg=await repo.inspect(id+'.izm');assert(pkg.supported,'This module is not supported by the selected launcher');
 const buildRoot=path.join(root,'data/build-cache/sd-apps',id+'-'+crypto.randomUUID());let attachment,storage=providedStorage,retained=false;
 try{
  await repo.install(pkg.file,buildRoot,{expectedSha256});let result,nativeBuildError;
  // Try the actual contents, rather than trusting an SD eligibility flag.
  // A host-dependent or unsupported local entry is preserved as its original
  // package and dispatched through the ordinary launcher when selected.
  try{result=await build(path.join(buildRoot,id),path.join(buildRoot,'package'));}
  catch(error){nativeBuildError=error.message;result=await require('./managed-package.cjs').build(pkg,await require('../../services/app-catalog.cjs').readApp(path.join(buildRoot,id)),path.join(buildRoot,'managed'));}
  if(!storage){attachment=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));storage=await require('./storage.cjs').create(attachment);}
  await ensure(storage,id);const existing=await inventory(storage);assert(existing.some(p=>p.id===id)||existing.length<15,'The SD menu supports 15 applications; remove an existing application first');
  return {...await installBytes(storage,result.bytes,{progress,assets:result.assets}),name:pkg.name,version:pkg.version,execution:result.execution||'console',...(nativeBuildError?{nativeBuildError}:{})};
 }catch(e){retained=!!e.resourcesRetained;throw e;}finally{
  // The package is a temporary transfer source. App launch never reads it.
  await fs.rm(buildRoot,{recursive:true,force:true});if(attachment&&storage&&!storage.uncertain&&!retained)await attachment.close();
 }
}
async function inventory(storage,{identities=false}={}){
 let rows;try{rows=await storage.list(BASE);}catch(error){if((error.nativeResult?.canonical??error.nativeResult)===0x202&&!storage.uncertain)return [];throw error;}const apps=[];assert(rows.length<=200000);
 for(const row of rows){if(row.type!=='dir'||! /^[a-z0-9][a-z0-9-]{0,46}$/.test(row.name))continue;const p=paths(row.name),entries=await storage.list(p.virtualPath),entry=entries.find(e=>e.name==='launcher.izapp'&&e.type==='file');if(!entry)continue;
  // Presence is observed from the card; full CRC validation belongs to the
  // native supervisor. Do not turn a cached PC receipt into SD inventory.
  let headerSha256;
  if(identities&&entry.size>=512){let file;try{file=await storage.openRead(p.packagePath);assert.equal(file.size,entry.size);const header=await storage.read(file,0,512);assert.equal(header.length,512);headerSha256=hash(header);}finally{if(file&&!storage.uncertain)await storage.close(file);}}
  apps.push({id:row.name,headerSha256,path:p.packagePath,displayPath:'SD:'+p.path+'/launcher.izapp',bytes:entry.size,source:'sd',readbackVerified:false});
 }
 return apps;
}
module.exports={paths,ensure,installBytes,installModule,inventory};
