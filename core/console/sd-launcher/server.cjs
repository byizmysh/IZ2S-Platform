'use strict';
// Durable SD installation jobs. Only this runtime consumes SD jobs; it never
// routes their images through the ordinary RAM/PC application loader.
const path=require('node:path'),assert=require('node:assert/strict');
const jobs=require('../../packages/jobs.cjs'),files=require('../../packages/files.cjs'),installer=require('./install.cjs'),host=require('./host-storage.cjs');
async function create({root,directory,stateRoot,shell,supervisor,initialState,observe=async()=>{},install=installer.installModule,remove=require('./remove.cjs').removeModule,readInventory,resolvePackage,clock=Date.now,progressIntervalMs=500}){
 const file=path.join(directory,'sd-installed.json'),queue=require('../../packages/job-watch.cjs').create(stateRoot);let installed=[],fenced=false;
 const versionFile=path.join(root,'data/state/sd-version-receipts.json');
 const versionReceipts=await files.read(versionFile)||{};
 const official=require('../../../engines/iz2s-core/tools/official-apps.json');
 const known=id=>[versionReceipts[id],...official.filter(row=>row.id===id)].filter(Boolean);
 const observed=rows=>rows.map(row=>{
  const record=row.sha256&&known(row.id).find(receipt=>receipt.sha256===row.sha256&&receipt.bytes===row.bytes);
  return {...row,version:record?.version,sourceSha256:record?.sourceSha256,versionVerified:!!record};
 });
 const inventory=readInventory||(()=>host.inventory({directory,identifyDigest:(id,size)=>known(id)
  .some(record=>!!record.sha256&&record.bytes===size)}));
 const sdCatalog=state=>new Map(state.applications.filter(app=>app.source==='sd').map(app=>[app.id,app]));
 let inventoryGeneration=null,inventoryAttempt=null,inventoryError=null;
 const describeScanError=state=>'SD catalog read rejected (0x'+state.auto_scan_error.toString(16)+')';
 if(initialState?.auto_scan_error)inventoryError=describeScanError(initialState);
 else{
  try{
   installed=observed(await inventory());
   if(initialState?.catalogStable)inventoryGeneration=initialState.scan_generation;
  }catch(error){
   if(error.resourcesRetained||require('../../session/offline-policy.cjs').isTransportLoss(error))throw error;
   inventoryError=error.message;
  }
  if(initialState?.catalogStable)inventoryAttempt=initialState.scan_generation;
 }
 const save=()=>files.write(file,{session:path.basename(directory),path:'SD:'+require('../../module-storage.cjs').BASE+'/',updated:new Date().toISOString(),scanGeneration:inventoryGeneration,inventoryError,installed});await save();
 async function synchronizeInventory(state){
  if(fenced||state.active||state.auto_scan_error||!state.catalogStable||!Number.isInteger(state.scan_generation)||state.scan_generation===inventoryAttempt)return state;
  let lease;
  try{
   // Observe native/offline changes once per completed scan. A stable menu
   // lease binds file presence and names to the same native catalog, and
   // prevents an offline removal or launch racing this observation.
   lease=await supervisor.holdMenu();assert(lease?.active,'SD inventory requires menu ownership');
   const current=await supervisor.status();if(current.state!==2||current.active||current.poisoned||!current.catalogStable)throw Object.assign(Error('SD inventory menu boundary changed'),{resourcesRetained:true});
   state=current;
   if(current.auto_scan_error)return state;
   inventoryAttempt=current.scan_generation;
   const rows=observed(await inventory()),previous=new Map(installed.map(row=>[row.id,row])),catalog=sdCatalog(current);
   installed=rows.map(row=>{
    const prior=previous.get(row.id),tile=catalog.get(row.id);
    const same=row.sha256&&prior?.sha256===row.sha256&&prior.bytes===row.bytes;
    const visible=tile&&Number.isInteger(row.headerCrc)&&tile.headerCrc===row.headerCrc;
    return {...(same?prior:null),...row,...(visible&&tile.name?{name:tile.name}:{})};
   });
   inventoryGeneration=current.scan_generation;inventoryError=null;await save();
  }catch(error){
   if(error.resourcesRetained||require('../../session/offline-policy.cjs').isTransportLoss(error)){fenced=true;throw Object.assign(error,{resourcesRetained:true});}
   if(error.menuBusy)return null;
   // An optional PC inventory failure cannot stop an autonomous menu. A
   // later native scan (including Refresh) retries it without hot polling.
   inventoryError=error.message;await save();
  }finally{
   if(lease?.active&&!fenced)try{await lease.release({rescan:false});}catch(error){fenced=true;throw Object.assign(error,{resourcesRetained:true});}
  }
  return state;
 }
 const receiptFile=path.join(directory,'sd-install-requests.json');let receipts;
 try{receipts=await files.read(receiptFile)||{};}catch(e){throw Object.assign(e,{nativeRequestsRetained:true});}
 let requestRepo;resolvePackage=resolvePackage||(async id=>{requestRepo=requestRepo||await require('../../packages/repository.cjs').create(root,{shell,source:'current'});return requestRepo.inspect(id+'.module.zip');});
 async function acceptRequest(state){
  const request=state.requestStable&&state.installRequests?.[0];if(!request)return;
  try{
   assert(Number.isInteger(request.sequence)&&request.sequence>0&&request.sequence<=0xffffffff&&/^[a-z0-9][a-z0-9-]{0,46}$/.test(request.id)&&/^[a-f0-9]{64}$/.test(request.sha256),'Invalid native SD request');
   const key=path.basename(directory)+':sd:'+request.sequence;let receipt=receipts[key];
   if(receipt)assert(receipt.id===request.id&&receipt.sha256===request.sha256,'Native SD request identity changed');
   else{
    receipt={...request,at:new Date().toISOString(),error:0};
    let pkg;try{
     pkg=await resolvePackage(request.id);assert(pkg.id===request.id&&pkg.sha256===request.sha256,'The selected package changed on the PC; select the new version again');assert(pkg.supported,'The selected package is unavailable for this launcher');
    }catch(e){
     if(e.code&&e.code!=='ERR_ASSERTION'&&e.code!=='ENOENT')throw e;
     receipt.error=0xe507;receipt.reason=e.message;
    }
    if(!receipt.error)try{const job=await jobs.create(stateRoot,pkg,'console',{target:'sd',requestKey:key,expectedSha256:request.sha256});receipt.job=job.id;}
    catch(e){if(e.message==='Package queue is busy; try again')return;if(!['Another operation is already queued for this module','Another version is already queued for this module'].includes(e.message))throw e;receipt.error=0xe507;receipt.reason=e.message;}
    receipts[key]=receipt;await files.write(receiptFile,receipts);if(receipt.error)await files.event(stateRoot,'error',receipt.reason,{target:'sd',packageId:request.id});
   }
   let error=receipt.error;
   if(!error){const job=(await queue.all()).find(j=>j.id===receipt.job);assert(job,'The recorded SD installation job is missing');if(!['ready','failed'].includes(job.status))return;if(job.resourcesRetained)throw Error('The SD installation outcome is uncertain');error=job.status==='failed'?0xe508:0;}
   // The console retains its request until the verified file is accepted by
   // the native menu, or a confirmed rejection is recorded. Queue creation
   // alone is not an acknowledgement of an installed application.
   await supervisor.acknowledgeInstall(request.sequence,error);
  }catch(e){fenced=true;throw Object.assign(e,{nativeRequestsRetained:true});}
 }
 // A restarted host cannot infer whether an interrupted publication occurred.
 const initialJobs=await jobs.all(stateRoot);
 for(const job of initialJobs)if(job.target==='sd'&&job.resourcesRetained&&(!job.consoleSession||job.consoleSession===path.basename(directory)))fenced=true;
 for(const job of initialJobs)if(job.target==='sd'&&job.status==='loading'){
  job.status='failed';job.error='The previous SD installation has an unknown outcome; inspect the card before retrying';job.resourcesRetained=true;await jobs.save(stateRoot,job);fenced=true;
 }
 let progressAt=-Infinity;
 async function progress(job,phase,{bytes=0,total=0}={}){
  const changed=job.progress?.phase!==phase,now=clock();
  job.progress={phase,label:phase==='sd-write'?'Writing to SD':phase==='sd-verify'?'Verifying SD':phase==='sd-publish'?'Waiting for the SD menu':phase==='ready'?(job.operation==='remove'?'Removed from SD':job.nativeCatalogAccepted===false?'Stored on SD':'Installed on SD'):job.operation==='remove'?'Removing SD application':'Preparing SD application',completedBytes:bytes,totalBytes:total||null,remainingBytes:total?total-bytes:null,percent:total?Math.floor(bytes*100/total):phase==='ready'?100:null,updated:new Date().toISOString()};
  // Byte accounting remains exact. Only optional progress snapshots and menu
  // observations are rate limited; the transfer/readback itself is untouched.
  if(!changed&&bytes!==total&&now-progressAt<progressIntervalMs)return false;
  await jobs.save(stateRoot,job);progressAt=now;return true;
 }
 async function step(state){
  assert(state.state===2,'SD supervisor is not ready');state=await synchronizeInventory(state);if(!state)return;const rows=await queue.all();
  for(const job of rows.filter(j=>j.target==='sd'&&j.status==='publishing')){
   const previousSession=!!job.consoleSession&&job.consoleSession!==path.basename(directory);
   const needsCardIdentity=previousSession||!!job.result?.headerSha256||job.operation==='remove';
   const inventoryFresh=inventoryGeneration===state.scan_generation&&inventoryError===null;
   const fresh=(previousSession||state.scan_generation>job.sdScanGeneration)&&(!needsCardIdentity||inventoryFresh);
   if(fresh&&state.active===0&&state.catalogStable&&!state.auto_scan_error){
    const cardMatches=!needsCardIdentity||host.matchesPublication(job,installed);
    const tile=state.applications.find(app=>app.source==='sd'&&app.id===job.packageId);
    const removing=job.operation==='remove';
    const accepted=cardMatches&&(removing?!tile:host.catalogMatches(job,tile));
    const storedOnly=cardMatches&&needsCardIdentity&&!removing&&!tile&&host.hiddenByMixedMenu(state,installed,job.packageId);
    job.storageVerified=cardMatches;
    job.nativeCatalogAccepted=accepted;
    job.completed=new Date().toISOString();
    if(accepted||storedOnly){
     job.status='ready';await progress(job,'ready');
     await files.event(stateRoot,storedOnly?'stored':removing?'unloaded':'loaded',job.name+(storedOnly?' stored on SD':removing?' removed from SD':' installed on SD'),{packageId:job.packageId,target:'sd'});
    }
    else{job.status='failed';job.error=cardMatches?'The file is verified on SD, but the native menu rejected this application':'The SD card no longer matches the previous installation or removal';job.progress={...job.progress,phase:'failed',percent:null};await jobs.save(stateRoot,job);}
   }
  }
  if(fenced)return state;
  await acceptRequest(state);
  const job=(await queue.all()).find(j=>j.target==='sd'&&j.status==='queued');if(!job||state.active!==0)return state;
  job.status='loading';job.consoleSession=path.basename(directory);job.started=new Date().toISOString();await progress(job,'sd-prepare');
  let lease;
  try{
   // Native offline removal and host publication share one filesystem boundary.
   // Keep input/presentation running while all host mutations hold this lease.
   assert(typeof supervisor.holdMenu==='function','SD changes require menu ownership');lease=await supervisor.holdMenu();
   if(job.operation==='remove'){job.result=await remove({directory,id:job.packageId,lease});assert(job.result.id===job.packageId&&job.result.removed,'SD removal was not verified');}
   else{job.result=await install({root,directory,id:job.packageId,shell,expectedSha256:job.expectedSha256,progress:async p=>{if(await progress(job,p.phase==='write'?'sd-write':'sd-verify',p))await observe();}});assert(job.result.id===job.packageId&&job.result.readbackVerified,'SD installation was not verified');}
   host.verify(job,job.result);
   if(job.operation==='remove')delete versionReceipts[job.packageId];
   else if(job.result.sha256&&job.result.version)versionReceipts[job.packageId]={version:job.result.version,sourceSha256:job.expectedSha256,sha256:job.result.sha256,bytes:job.result.bytes};
   await files.write(versionFile,versionReceipts);
   installed=installed.filter(p=>p.id!==job.packageId);if(job.operation!=='remove')installed.push({...job.result,source:'sd',versionVerified:!!(job.result.sha256&&job.result.version),sourceSha256:job.expectedSha256});await save();
   const current=await supervisor.status();job.sdScanGeneration=current.scan_generation;
   job.status='publishing';await progress(job,'sd-publish');if(lease)await lease.release();else await supervisor.rescan();
  }catch(e){
   if(lease?.active&&!e.resourcesRetained)try{await lease.release({rescan:job.operation==='remove'});}catch(release){e.resourcesRetained=true;e.releaseError=release.message;}
   if(e.menuBusy&&!e.resourcesRetained){job.status='queued';await jobs.save(stateRoot,job);return state;}
   job.status='failed';job.error=e.message;job.resourcesRetained=!!e.resourcesRetained;job.completed=new Date().toISOString();job.progress={...job.progress,phase:'failed',percent:null};await jobs.save(stateRoot,job);await files.event(stateRoot,'error',job.name+': '+e.message,{target:'sd'});
   if(e.resourcesRetained){fenced=true;throw e;}
  }
  return state;
 }
 return {step,get installed(){return installed.map(p=>({...p}));},get fenced(){return fenced;}};
}
module.exports={create};
