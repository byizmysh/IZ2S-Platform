const removal=require('./removal.cjs');
const path=require('node:path'),catalog=require('../services/app-catalog.cjs'),files=require('./files.cjs'),jobs=require('./jobs.cjs');
async function create({rpc,menu,directory,runtime,root=path.resolve(__dirname,'../..'),stateRoot=process.env.IZ11_STATE_DIR||path.join(root,'data/state/current'),prepareShell=require('../services/gpu-app.cjs').prepare,redrawProgress=async()=>{},validateCandidate=async()=>{},validateRemoval=async()=>{},packageEligible=()=>true,acquireStorageLease,sdStorageDependencies={}},extension){
 const {id:shellId,name:shellName,serverDirectory,extraMenuData=()=>[]}=extension;
 if(!['izlauncher','izpslauncher'].includes(shellId))throw Error('Unknown console shell');
 const local=name=>require(path.join(serverDirectory,name+'.cjs'));
 const repo=await require('./repository.cjs').create(root,{shell:shellId,source:'current',sessionProviders:true});
 const report={session:rpc.a.session,stage:'Preparing menu',started:new Date().toISOString(),menuReady:false,available:[],installed:[],postMenuOnly:true,consoleFilesystemWrites:0};
 const save=()=>files.write(path.join(directory,'run11-runtime.json'),report);
 const telemetry=require('./telemetry.cjs').create({write:save,label:shellName+' panel telemetry'});
 const progress=require('./progress.cjs').create({rpc,mapped:menu.controls.mapped,stateRoot,redraw:redrawProgress});
 const sdStorage=await require('./sd-staging.cjs').create({root,directory,stateRoot,shell:shellId,session:rpc.a.session,acquireLease:acquireStorageLease,progress,...sdStorageDependencies});report.sdStorage=sdStorage.report;report.sdInstalled=sdStorage.report.installed;
 await save();await files.event(stateRoot,'boot','Original menu verified. Loading '+shellName+' interface.');
 const pipelines=new Map(),notifications=local('notifications').create({rpc,mapped:menu.controls.mapped});
 const activity=extension.createActivity?.({rpc,mapped:menu.controls.mapped,directory,stateRoot});
 const queue=require('./job-watch.cjs').create(stateRoot),extraPublications=new Map();let forceActivity=false;
 const {metadata:typeInfo,bytes:typeBytes}=await local('assets').loadTypeAtlas();

 async function applyShell(){
  const shell=await local('composition').build(runtime.report.catalog?.apps||[],require('../session/preload.cjs').cacheRoot(root));
  if(report.shell?.fingerprint===shell.fingerprint)return;
  let gpu=pipelines.get(shell.fingerprint);
  if(!gpu){await files.event(stateRoot,'graphics','Preparing '+shell.name+' after the original menu checks');gpu=await prepareShell(rpc,menu,shell,directory);if(!gpu.ready||!gpu.scratchRestored)throw Error('IZlauncher shell failed qualification');if(gpu.privateStorage){await require('../services/owned-buffer.cjs').initialize(rpc,gpu.privateStorage.mapped,gpu.privateStorage.bytes);await rpc.write(menu.controls.mapped+require('../services/pc-screen.cjs').HEADER,Buffer.alloc(96));}pipelines.set(shell.fingerprint,gpu);}
  menu.commands=gpu.commands;if(gpu.layout)menu.layout=gpu.layout;if(gpu.descriptor)menu.controls.descriptor=gpu.descriptor;menu.streamStorage=gpu.privateStorage||null;report.shell={id:shell.id,fingerprint:shell.fingerprint,privateStorage:gpu.privateStorage||null,ready:true};
 }
 await applyShell();
 let persistedStamp=null,heroStamp='',heroUpload='',heroBytes=Buffer.alloc(0),library,libraryBytes=Buffer.alloc(0),publishedPackages=[],dirty=true,lastInventory=0,decoratedGeneration=null,libraryUploadGeneration=null,libraryUploadStamp=null,lastSequence=0,lastRemoveSequence=0,publishing=[],pickerOpen=false;
 function catalogStamp(apps){return apps.map(a=>a.id+':'+a.fingerprint).join('|');}
 async function prepareHero(apps){const stamp=catalogStamp(apps);if(stamp!==heroStamp){heroBytes=(await local('raster').raster({apps},directory,'run11-hero-'+Date.now())).subarray(0,apps.length*catalog.SLOT_BYTES);heroStamp=stamp;}return stamp;}
 runtime.decorateMenuData=async(bytes,{catalog:next,generation}={})=>{typeBytes.copy(bytes,typeInfo.offset);for(const item of extraMenuData()){item.bytes.copy(bytes,item.offset);extraPublications.set(item.offset,generation+':'+item.bytes.toString('hex'));}let stamp;if(runtime.menuRasterizer===local('raster').raster){stamp=catalogStamp(next.apps);heroBytes=Buffer.from(bytes.subarray(catalog.ATLAS_BYTES,catalog.ATLAS_BYTES+next.apps.length*catalog.SLOT_BYTES));heroStamp=stamp;}else stamp=await prepareHero(next.apps);heroBytes.copy(bytes,catalog.ATLAS_BYTES);libraryBytes.copy(bytes,catalog.ATLAS_BYTES+16*catalog.SLOT_BYTES);decoratedGeneration=generation;heroUpload=stamp+':'+generation;libraryUploadGeneration=generation;libraryUploadStamp=library?.stamp;};
 async function inventory(force=false){if(!force&&Date.now()-lastInventory<1000)return;
  const next=await repo.list(),stamp=next.packages.map(p=>p.file+':'+p.sha256).join('|');
  if(library?.stamp===stamp){lastInventory=Date.now();return;}
  const preview=await repo.previewApps(path.join(stateRoot,'library-preview'),{filter:packageEligible,listing:next});
  const raster=await local('raster').raster(preview,directory,'run11-available-'+Date.now());libraryBytes=raster.subarray(0,preview.apps.length*catalog.SLOT_BYTES);preview.packages.forEach((pkg,i)=>libraryBytes.writeUInt32LE(pkg.sd?.compatible?1:2,i*catalog.SLOT_BYTES+102516));library={...preview,stamp};dirty=true;
  report.available=preview.packages.map(p=>({id:p.id,name:p.name,file:p.file,bytes:p.packageBytes}));report.rejected=preview.rejected;await save();lastInventory=Date.now();
 }
 async function publish(){
  if(runtime.active)return;
  if((await rpc.read(menu.controls.mapped+0x700004,4)).readUInt32LE()!==0)return;
  await inventory();if(runtime.menuDataPending)await runtime.publishMenuData();const generationId=runtime.report.generation;
  if(decoratedGeneration!==generationId){await require('../services/owned-upload.cjs').upload(rpc,menu.controls.mapped+1024+typeInfo.offset,typeBytes);decoratedGeneration=generationId;}
  for(const item of extraMenuData()){const stamp=generationId+':'+item.bytes.toString('hex');if(extraPublications.get(item.offset)!==stamp){await rpc.write(menu.controls.mapped+1024+item.offset,item.bytes);extraPublications.set(item.offset,stamp);}}
  if((libraryUploadGeneration!==generationId||libraryUploadStamp!==library?.stamp)&&libraryBytes.length){await require('../services/owned-upload.cjs').upload(rpc,menu.controls.mapped+1024+catalog.ATLAS_BYTES+16*catalog.SLOT_BYTES,libraryBytes);libraryUploadGeneration=generationId;libraryUploadStamp=library?.stamp;}
  const state=await rpc.read(menu.controls.mapped,1024);state.writeUInt32LE(library?.packages.length||0,832);state.writeUInt32LE(0,836);
  const pickerChanged=publishedPackages.map(p=>p.id+':'+p.sha256).join('|')!==(library?.packages||[]).map(p=>p.id+':'+p.sha256).join('|');if(pickerChanged){state.writeUInt32LE(0,784);state.writeUInt32LE(0,788);}
  const installed=runtime.report.catalog?.apps||[],stamp=catalogStamp(installed);
  if(stamp!==heroStamp){const apps=[];for(const app of installed)apps.push(await catalog.readApp(app.root));await prepareHero(apps);}
  const generation=stamp+':'+runtime.report.generation;if(heroUpload!==generation&&heroBytes.length){await require('../services/owned-upload.cjs').upload(rpc,menu.controls.mapped+1024+catalog.ATLAS_BYTES,heroBytes);heroUpload=generation;}
  let mask=0;for(let i=0;i<(library?.packages.length||0);i++)if(installed.some(a=>a.id===library.packages[i].id))mask|=1<<i;
  state.writeUInt32LE(require('./install-plan.cjs').consoleMask(library?.packages||[],{storageReady:sdStorage.report.enabled&&!sdStorage.fenced}),796);state.writeUInt32LE(0,792);
  state.writeUInt32LE(mask>>>0,840);state.writeUInt32LE(lastSequence,844);state.writeUInt32LE(0,812);if(publishing.length){state.writeUInt32LE(0,800);state.writeUInt32LE(0,804);}
  await rpc.write(menu.controls.mapped+784,state.subarray(784,848));
  // Invalidate menu checkpoints after legitimate host edits, at a stopped boundary.
  const epoch=Buffer.alloc(4);epoch.writeFloatLE(state.readFloatLE(108)+1);await rpc.write(menu.controls.mapped+108,epoch);
  publishedPackages=library?.packages||[];dirty=false;report.installed=installed.map(a=>({id:a.id,name:a.name,version:a.version,fingerprint:a.fingerprint}));report.menuReady=true;report.stage='Ready';report.updated=new Date().toISOString();
  if(persistedStamp!==stamp){await require('./installed-session.cjs').save(root,shellId,installed);persistedStamp=stamp;}
  const publishedJobs=publishing.length;for(const job of publishing){const index=publishedPackages.findIndex(p=>p.id===job.packageId);if(job.operation!=='remove'&&index>=0)notifications.enqueue({id:job.id,name:job.name,bytes:libraryBytes.subarray(index*catalog.SLOT_BYTES,(index+1)*catalog.SLOT_BYTES)});job.status='ready';job.completed=new Date().toISOString();await progress.update(job,'ready');await files.event(stateRoot,job.operation==='remove'?'unloaded':'loaded',job.name+(job.operation==='remove'?' removed from the console menu':' loaded into the console menu'),{packageId:job.packageId,source:job.source});}publishing=[];runtime.onUploadProgress=null;if(!publishedJobs)await progress.publish();await activity?.publish(installed,{generation:runtime.report.generation,force:forceActivity});forceActivity=false;await notifications.publish();report.notification=notifications.report;await save();
 }
 async function pending(state){
  if(runtime.active||state.readFloatLE(964)>0||state.readUInt32LE(1012)>0)return false;
  const opened=state.readUInt32LE(800)!==0,refreshCatalog=opened&&!pickerOpen;pickerOpen=opened;
  await inventory(refreshCatalog);
  const sequence=state.readUInt32LE(808),request=state.readUInt32LE(812);
  const rows=await queue.all(),sdPending=await sdStorage.pending(rows);
  const work=sdPending||await notifications.pending()||dirty||(request>0&&sequence!==lastSequence)||(state.readUInt32LE(944)>0&&state.readUInt32LE(948)!==lastRemoveSequence)||rows.some(j=>j.target!=='sd'&&j.status==='queued');
  if(!work)return false;
  // A built-in stream/calculator owns the same renderer until it returns home.
  const mode=(await rpc.read(menu.controls.mapped+0x700004,4)).readUInt32LE();return mode===0;
 }
 async function perform(state){
  if(!await removal.atMenu(runtime,state,rpc,menu.controls.mapped))return;
  await notifications.capture();await inventory();const sequence=state.readUInt32LE(808),request=state.readUInt32LE(812);
  if(request>0&&sequence!==lastSequence){lastSequence=sequence;const pkg=publishedPackages[request-1];if(pkg)try{
   const plan=require('./install-plan.cjs'),target=plan.consoleTarget(state);plan.create(publishedPackages,[pkg.id],{target,storageReady:sdStorage.report.enabled&&!sdStorage.fenced});await jobs.create(stateRoot,pkg,'console',{target,expectedSha256:pkg.sha256});
  }catch(error){await files.event(stateRoot,'error',error.message,{packageId:pkg.id,source:'console'});}}
  const removeRequest=state.readUInt32LE(944),removeSequence=state.readUInt32LE(948);
  if(removeRequest>0&&removeSequence!==lastRemoveSequence){
   lastRemoveSequence=removeSequence;const app=(runtime.report.catalog?.apps||[])[removeRequest-1];
   try{if(!app)throw Error('Selected module is no longer loaded');await removal.queue(stateRoot,[app.id],runtime.report.catalog.apps,'console');}
   catch(error){await rpc.write(menu.controls.mapped+952,Buffer.from([1,0,0,0]));await files.event(stateRoot,'error',error.message);}
   await rpc.write(menu.controls.mapped+940,Buffer.alloc(8));
  }
  const rows=await queue.all();await sdStorage.perform(rows);report.sdInstalled=sdStorage.report.installed;report.consoleFilesystemWrites=sdStorage.report.completedMutations;await save();
  const pending=rows.filter(j=>j.target!=='sd'&&j.status==='queued');
  const ids=new Set((runtime.report.catalog?.apps||[]).map(a=>a.id));
  for(const [position,job] of pending.entries()){job.status='loading';job.started=new Date().toISOString();await progress.update(job,'verify',{index:position+1,count:pending.length});
   try{
    if(job.operation==='remove'){
     const current=await catalog.scan(menu.catalog.root);const app=current.apps.find(a=>a.id===job.packageId);
     if(!app)throw Error('Module is no longer loaded on the console');
     await validateRemoval(app);
     await local('composition').build(current.apps.filter(a=>a.id!==app.id),require('../session/preload.cjs').cacheRoot(root));
     job.result=await removal.remove(menu.catalog.root,app.id);job.activityType='removed';job.version=app.version;
     ids.delete(app.id);job.status='publishing';publishing.push(job);await jobs.save(stateRoot,job);continue;
    }
    const pkg=await repo.inspect(job.file);if(pkg.id!==job.packageId)throw Error('Package changed identity');if(job.expectedSha256&&pkg.sha256!==job.expectedSha256)throw Error('The selected package changed; select the new version again');
    if(ids.size>=15&&!ids.has(pkg.id))throw Error('The menu supports 15 loaded apps per session');
    await files.event(stateRoot,'unpack','Verifying and unpacking '+job.name+' .izm');
    // Compile on the PC before publishing any catalog change to the console.
    const candidateRoot=path.join(stateRoot,'validated',job.id);await repo.install(job.file,candidateRoot,{expectedSha256:job.expectedSha256,onProgress:bytes=>progress.update(job,'prepare',bytes,false)});
    const candidate=await catalog.readApp(path.join(candidateRoot,pkg.id)),current=await catalog.scan(menu.catalog.root);
    await validateCandidate(candidate,current.apps);
    await progress.update(job,'graphics');await local('composition').build([...current.apps.filter(a=>a.id!==pkg.id),candidate],require('../session/preload.cjs').cacheRoot(root));
    const previous=current.apps.find(a=>a.id===pkg.id);job.activityType=previous?'updated':'installed';job.version=candidate.version;job.previousVersion=previous?.version||'';
    job.result=await repo.install(job.file,menu.catalog.root,{expectedSha256:job.expectedSha256,onProgress:bytes=>progress.update(job,'install',bytes,false)});ids.add(pkg.id);job.status='publishing';publishing.push(job);await progress.update(job,'transfer');
   }catch(e){job.status='failed';job.error=e.message;job.progress={...job.progress,phase:'failed',label:'Loading failed',percent:null};await jobs.save(stateRoot,job);await files.event(stateRoot,'error',job.name+': '+e.message);if(e.restartRequired)throw e;}
  }
  if(publishing.length){runtime.onUploadProgress=bytes=>progress.upload(bytes);await rpc.write(menu.controls.mapped+940,Buffer.alloc(8));const counter=Buffer.alloc(4);counter.writeFloatLE(state.readFloatLE(104)+1);await rpc.write(menu.controls.mapped+104,counter);}
 }
 await inventory(true);await publish();await files.event(stateRoot,'ready',shellName+' ready. Press X or touch the on-screen + to browse packages.');
 return {pending,perform,get acceptedInstallSequence(){return lastSequence;},observeNative:state=>sdStorage.observeNative(state),async preparingRAM(){if(publishing.length)await progress.update(publishing.at(-1),'graphics');},async atBoundary({menuReset=false}={}){if(menuReset){decoratedGeneration=null;libraryUploadGeneration=null;heroUpload='';extraPublications.clear();forceActivity=true;}if(!runtime.active)await applyShell();await publish();},async sample(batch,options={}){report.lastFrameBatch=batch;report.active=runtime.report.active||null;report.updated=new Date().toISOString();await telemetry.sample(options);},async close(){report.menuReady=false;report.stage='Disconnected';report.completed=new Date().toISOString();await save();},report};
}
module.exports={create};
