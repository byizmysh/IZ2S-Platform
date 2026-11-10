'use strict';
// The ordinary menu can store autonomous images without becoming the native
// SD renderer. This reports verified storage, never native-menu acceptance.
const path=require('node:path'),fs=require('node:fs/promises'),assert=require('node:assert/strict');
const files=require('./files.cjs'),jobs=require('./jobs.cjs'),host=require('../console/sd-launcher/host-storage.cjs');
async function create({root,directory,stateRoot,shell,session,acquireLease,progress,install=require('../console/sd-launcher/install.cjs').installModule,remove=require('../console/sd-launcher/remove.cjs').removeModule,inventory=host.inventory,readEndpoint=()=>files.read(path.join(directory,'fs-rpc-state.json'))}){
 const endpoint=await readEndpoint();let scanNeeded=!!acquireLease&&endpoint?.state==='ready'&&endpoint.verified===true,fenced=false,nativeSignature=null;
 const report={enabled:scanNeeded,state:scanNeeded?'pending':'unavailable',path:'SD:'+require('../module-storage.cjs').BASE+'/',installed:[],nativeCatalogAccepted:false,completedMutations:0};
 const save=()=>files.write(path.join(directory,'sd-installed.json'),{session,...report,updated:new Date().toISOString()});
 const initial=await jobs.all(stateRoot);
 for(const job of initial.filter(j=>j.target==='sd'&&(j.status==='loading'||j.resourcesRetained&&(!j.consoleSession||j.consoleSession===session)))){fenced=true;job.status='failed';job.resourcesRetained=true;job.error='The interrupted SD operation has an unknown outcome; inspect the card before retrying';await jobs.save(stateRoot,job);}
 if(fenced)report.state='uncertain';await save();
 async function held(action){
  assert(report.enabled&&typeof acquireLease==='function','SD changes require an available stopped menu');
  let lease;try{lease=await acquireLease();assert(lease?.active===true,'The renderer must be joined before SD access');return await action(lease);}
  catch(error){if(error.resourcesRetained){fenced=true;report.state='uncertain';await save();}throw error;}
  finally{if(lease?.active&&!fenced)try{await lease.release();}catch(error){fenced=true;report.state='uncertain';await save();throw Object.assign(error,{resourcesRetained:true});}}
 }
 async function scan(){report.installed=await inventory({directory});report.state='ready';scanNeeded=false;await save();}
 const request=path.join(directory,'sd-launcher-inventory.request');
 return {report,get fenced(){return fenced;},observeNative(state){if(!state.catalogStable||!Number.isInteger(state.auto_scan_signature))return;if(nativeSignature!==state.auto_scan_signature){nativeSignature=state.auto_scan_signature;scanNeeded=true;}const names=new Map((state.applications||[]).filter(a=>a.source==='sd').map(a=>[a.id,a]));for(const app of report.installed){if(names.get(app.id)){app.name=names.get(app.id).name;app.execution=names.get(app.id).execution||'console';}app.nativeCatalogAccepted=names.has(app.id);}report.nativeCatalogAccepted=report.installed.every(app=>app.nativeCatalogAccepted);},async pending(rows){if(fenced||!report.enabled)return false;return scanNeeded||rows.some(j=>j.target==='sd'&&j.status==='queued')||await fs.access(request).then(()=>true,()=>false);},async perform(rows){
  if(!report.enabled||fenced)return;
  if(await fs.access(request).then(()=>true,()=>false)){scanNeeded=true;await fs.rm(request);}
  if(scanNeeded)try{await held(scan);}catch(error){if(error.resourcesRetained)throw error;scanNeeded=false;report.state='error';report.error=error.message;await save();await files.event(stateRoot,'error','SD inventory: '+error.message,{target:'sd'});}
  for(const job of rows.filter(j=>j.target==='sd'&&j.status==='queued')){
   job.status='loading';job.consoleSession=session;job.started=new Date().toISOString();await progress.update(job,'sd-prepare');
   try{
    await held(async lease=>{
     job.result=host.verify(job,job.operation==='remove'?await remove({directory,id:job.packageId,lease}):await install({root,directory,id:job.packageId,shell,expectedSha256:job.expectedSha256,progress:p=>progress.update(job,p.phase==='write'?'sd-write':'sd-verify',{completedBytes:p.bytes,totalBytes:p.total},false)}));
     // Re-read presence after the real commit, not from the receipt list.
     await scan();assert(report.installed.some(p=>p.id===job.packageId)===(job.operation!=='remove'),'The committed SD application is not reflected by the card');
    });
    report.completedMutations++;await save();job.storageVerified=true;job.nativeCatalogAccepted=false;job.status='ready';job.completed=new Date().toISOString();await progress.update(job,'ready');await files.event(stateRoot,job.operation==='remove'?'unloaded':'stored',job.name+(job.operation==='remove'?' removed from SD':' stored on SD'),{target:'sd',packageId:job.packageId});
   }catch(error){
    job.status='failed';job.error=error.message;job.resourcesRetained=!!error.resourcesRetained;job.completed=new Date().toISOString();job.progress={...job.progress,phase:'failed',percent:null};await jobs.save(stateRoot,job);await files.event(stateRoot,'error',job.name+': '+error.message,{target:'sd',packageId:job.packageId});if(error.resourcesRetained)throw error;
   }
  }
 }};
}
module.exports={create};
