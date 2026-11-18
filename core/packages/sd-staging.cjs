'use strict';
// The ordinary menu verifies storage and, when present, native publication.
const path=require('node:path'),fs=require('node:fs/promises'),assert=require('node:assert/strict');
const files=require('./files.cjs'),jobs=require('./jobs.cjs'),host=require('../console/sd-launcher/host-storage.cjs');
async function create({root,directory,stateRoot,shell,session,acquireLease,progress,install=require('../console/sd-launcher/install.cjs').installModule,remove=require('../console/sd-launcher/remove.cjs').removeModule,inventory=host.inventory,readEndpoint=()=>files.read(path.join(directory,'fs-rpc-state.json'))}){
 const endpoint=await readEndpoint();let scanNeeded=!!acquireLease&&endpoint?.state==='ready'&&endpoint.verified===true,fenced=false,nativeSignature=null,nativeScanFailed=false,nativeGeneration=null,inventoryGeneration=null,nativeApps=null;
 const report={enabled:scanNeeded,state:scanNeeded?'pending':'unavailable',path:'SD:'+require('../module-storage.cjs').BASE+'/',installed:[],nativeCatalogAccepted:false,completedMutations:0};
 const save=()=>files.write(path.join(directory,'sd-installed.json'),{session,...report,updated:new Date().toISOString()});
 const queue=require('./job-watch.cjs').create(stateRoot),initial=await queue.all();
 for(const job of initial.filter(j=>j.target==='sd'&&(j.status==='loading'||j.resourcesRetained&&(!j.consoleSession||j.consoleSession===session)))){fenced=true;job.status='failed';job.resourcesRetained=true;job.error='The interrupted SD operation has an unknown outcome; inspect the card before retrying';await jobs.save(stateRoot,job);}
 if(fenced)report.state='uncertain';await save();
 async function held(action){
  assert(report.enabled&&typeof acquireLease==='function','SD changes require an available stopped menu');
  let lease;try{lease=await acquireLease();assert(lease?.active===true,'The renderer must be joined before SD access');return await action(lease);}
  catch(error){if(error.resourcesRetained){fenced=true;report.state='uncertain';await save();}throw error;}
  finally{if(lease?.active&&!fenced)try{await lease.release();}catch(error){fenced=true;report.state='uncertain';await save();throw Object.assign(error,{resourcesRetained:true});}}
 }
 async function scan(){report.installed=await inventory({directory});annotateInventory();report.state='ready';delete report.error;inventoryGeneration=nativeGeneration;scanNeeded=false;await save();}
 const request=path.join(directory,'sd-launcher-inventory.request');
 function annotateInventory(){
  if(!nativeApps)return;
  for(const app of report.installed){
   const native=nativeApps.get(app.id);
   const accepted=!!native&&Number.isInteger(app.headerCrc)&&native.headerCrc===app.headerCrc;
   if(accepted){
    if(native.name)app.name=native.name;
    app.execution=native.execution||'console';
   }
   app.nativeCatalogAccepted=accepted;
  }
  report.nativeCatalogAccepted=report.installed.every(app=>app.nativeCatalogAccepted);
 }
 async function observeNative(state){
  if(!state.catalogStable||!Number.isInteger(state.auto_scan_signature))return;
  if(Number.isInteger(state.scan_generation))nativeGeneration=state.scan_generation;
  if(state.auto_scan_error){nativeScanFailed=true;return;}
  if(nativeScanFailed||nativeSignature!==state.auto_scan_signature){
   nativeScanFailed=false;nativeSignature=state.auto_scan_signature;scanNeeded=true;
  }
  const names=new Map((state.applications||[]).filter(a=>a.source==='sd').map(a=>[a.id,a]));
  nativeApps=names;annotateInventory();
  if(!report.enabled||fenced||state.active||state.rescan||!Number.isInteger(state.scan_generation))return;
  for(const job of await queue.all()){
   if(job.target!=='sd'||job.status!=='publishing')continue;
   const previousSession=job.consoleSession!==session;
   if(!previousSession&&nativeGeneration<=job.sdScanGeneration)continue;
   if(inventoryGeneration===null||!previousSession&&inventoryGeneration<=job.sdScanGeneration){
    if(report.state!=='error')scanNeeded=true;
   }
   if(scanNeeded||report.state!=='ready')continue;
   const present=names.has(job.packageId),removing=job.operation==='remove';
   const cardMatches=host.matchesPublication(job,report.installed,{requireIdentity:previousSession});
   // A full mixed catalog can hide SD entries until the user opens the SD page.
   const storedOnly=cardMatches&&!removing&&!present&&host.hiddenByMixedMenu(state,report.installed,job.packageId);
   const accepted=cardMatches&&(removing?!present:host.catalogMatches(job,names.get(job.packageId)));
   const ready=accepted||storedOnly;
   job.storageVerified=cardMatches;
   job.nativeCatalogAccepted=accepted;
   job.completed=new Date().toISOString();
   job.status=ready?'ready':'failed';
   if(!ready)job.error=cardMatches?'The card change was verified, but the SD menu did not reflect it':'The SD card no longer matches the previous installation or removal';
   job.progress={...job.progress,phase:job.status,label:storedOnly?'Stored on SD':accepted?(removing?'Removed from SD':'Ready in SD menu'):job.error,percent:ready?100:null,updated:job.completed};
   await jobs.save(stateRoot,job);
   await files.event(stateRoot,storedOnly?'stored':accepted?(removing?'unloaded':'loaded'):'error',job.name+(storedOnly?' stored on SD':accepted?(removing?' removed from SD':' ready in the SD menu'):': '+job.error),{target:'sd',packageId:job.packageId});
  }
 }
 return {report,get fenced(){return fenced;},observeNative,requestScan(){scanNeeded=report.enabled;},async pending(rows){
  if(fenced||!report.enabled)return false;
  return rows.some(j=>j.target==='sd'&&j.status==='queued')||
   !nativeScanFailed&&(scanNeeded||await fs.access(request).then(()=>true,()=>false));
 },async perform(rows){
  if(!report.enabled||fenced)return;
  if(!nativeScanFailed&&await fs.access(request).then(()=>true,()=>false)){scanNeeded=true;await fs.rm(request);}
  if(scanNeeded&&!nativeScanFailed)try{await held(scan);}catch(error){if(error.resourcesRetained)throw error;scanNeeded=false;report.state='error';report.error=error.message;await save();await files.event(stateRoot,'error','SD inventory: '+error.message,{target:'sd'});}
  for(const job of rows.filter(j=>j.target==='sd'&&j.status==='queued')){
   job.status='loading';job.consoleSession=session;job.started=new Date().toISOString();await progress.update(job,'sd-prepare');
   try{
    await held(async lease=>{
     job.result=host.verify(job,job.operation==='remove'?await remove({directory,id:job.packageId,lease}):await install({root,directory,id:job.packageId,shell,expectedSha256:job.expectedSha256,progress:p=>progress.update(job,p.phase==='write'?'sd-write':'sd-verify',{completedBytes:p.bytes,totalBytes:p.total},false)}));
     await scan();assert(host.matchesPublication(job,report.installed,{requireIdentity:false}),'The card does not reflect the committed SD package change');
    });
    report.completedMutations++;await save();job.storageVerified=true;job.nativeCatalogAccepted=false;
    if(nativeGeneration!==null){
     job.sdScanGeneration=nativeGeneration;job.status='publishing';
     await progress.update(job,'sd-publish');continue;
    }
    job.status='ready';job.completed=new Date().toISOString();await progress.update(job,'ready');await files.event(stateRoot,job.operation==='remove'?'unloaded':'stored',job.name+(job.operation==='remove'?' removed from SD':' stored on SD'),{target:'sd',packageId:job.packageId});
   }catch(error){
    job.status='failed';job.error=error.message;job.resourcesRetained=!!error.resourcesRetained;job.completed=new Date().toISOString();
    if(error.resourcesRetained){
     job.progress={...job.progress,phase:'failed',percent:null};await jobs.save(stateRoot,job);
    }else await progress.update(job,'failed');
    await files.event(stateRoot,'error',job.name+': '+error.message,{target:'sd',packageId:job.packageId});if(error.resourcesRetained)throw error;
   }
  }
 }};
}
module.exports={create};
