const removal=require('../../../core/packages/removal.cjs');
const fs=require('node:fs/promises'),path=require('node:path'),catalog=require('../../../core/services/app-catalog.cjs'),files=require('../../../core/packages/files.cjs'),jobs=require('../../../core/packages/jobs.cjs');
async function create({rpc,menu,directory,runtime,root=path.resolve(__dirname,'../../..'),stateRoot=process.env.IZ11_STATE_DIR||path.join(root,'data/state/current'),prepareShell=require('../../../core/services/gpu-app.cjs').prepare,redrawProgress=async()=>{},validateCandidate=async()=>{},validateRemoval=async()=>{},packageEligible=()=>true}){
 const repo=await require('../../../core/packages/repository.cjs').create(root,{shell:require('../shell.json').id});
 const report={session:rpc.a.session,stage:'Preparing menu',started:new Date().toISOString(),menuReady:false,available:[],installed:[],postMenuOnly:true,consoleFilesystemWrites:0};
 const save=()=>files.write(path.join(directory,'run11-runtime.json'),report);
 const telemetry=require('../../../core/packages/telemetry.cjs').create({write:save,label:'IZPSlauncher panel telemetry'});
 const progress=require('../../../core/packages/progress.cjs').create({rpc,mapped:menu.controls.mapped,stateRoot,redraw:redrawProgress});
 await save();await files.event(stateRoot,'boot','Original menu verified. Loading IZPSlauncher interface.');
 const pipelines=new Map(),notifications=require('./notifications.cjs').create({rpc,mapped:menu.controls.mapped});
 const activity=require('./activity.cjs').create({rpc,mapped:menu.controls.mapped,directory,stateRoot});
 const {metadata:typeInfo,bytes:typeBytes}=await require('./assets.cjs').loadTypeAtlas();
 runtime.decorateMenuData=bytes=>{typeBytes.copy(bytes,typeInfo.offset);require('./clock.cjs').header().copy(bytes,0x50000);};
 async function applyShell(){
  const shell=await require('./composition.cjs').build(runtime.report.catalog?.apps||[],require('../../../core/session/preload.cjs').cacheRoot(root));
  if(report.shell?.fingerprint===shell.fingerprint)return;
  let gpu=pipelines.get(shell.fingerprint);
  if(!gpu){await files.event(stateRoot,'graphics','Preparing '+shell.name+' after the original menu checks');gpu=await prepareShell(rpc,menu,shell,directory);if(!gpu.ready||!gpu.scratchRestored)throw Error('IZlauncher shell failed qualification');if(gpu.privateStorage){await require('../../../core/services/owned-buffer.cjs').initialize(rpc,gpu.privateStorage.mapped,gpu.privateStorage.bytes);await rpc.write(menu.controls.mapped+require('../../../core/services/pc-screen.cjs').HEADER,Buffer.alloc(96));}pipelines.set(shell.fingerprint,gpu);}
  menu.commands=gpu.commands;menu.streamStorage=gpu.privateStorage||null;report.shell={id:shell.id,fingerprint:shell.fingerprint,privateStorage:gpu.privateStorage||null,ready:true};
 }
 await applyShell();
 let heroStamp='',heroUpload='',heroBytes=Buffer.alloc(0),library,libraryBytes=Buffer.alloc(0),publishedPackages=[],dirty=true,lastInventory=0,lastSequence=0,lastRemoveSequence=0,publishing=[];
 async function inventory(force=false){if(!force&&Date.now()-lastInventory<1000)return;lastInventory=Date.now();
  const next=await repo.list(),stamp=next.packages.map(p=>p.file+':'+p.sha256).join('|');
  if(library?.stamp===stamp)return;
  const preview=await repo.previewApps(path.join(stateRoot,'library-preview'),{filter:packageEligible});
  const raster=await require('./raster.cjs').raster(preview,directory,'run11-available-'+Date.now());libraryBytes=raster.subarray(0,preview.apps.length*catalog.SLOT_BYTES);library={...preview,stamp};dirty=true;
  report.available=preview.packages.map(p=>({id:p.id,name:p.name,file:p.file,bytes:p.packageBytes}));report.rejected=preview.rejected;await save();
 }
 async function publish(){
  if(runtime.active)return;
  if((await rpc.read(menu.controls.mapped+0x700004,4)).readUInt32LE()!==0)return;
  await inventory();await rpc.write(menu.controls.mapped+1024+typeInfo.offset,typeBytes);await rpc.write(menu.controls.mapped+1024+0x50000,require('./clock.cjs').header());if(dirty&&libraryBytes.length)await rpc.write(menu.controls.mapped+1024+catalog.ATLAS_BYTES+16*catalog.SLOT_BYTES,libraryBytes);
  const state=await rpc.read(menu.controls.mapped,1024);state.writeUInt32LE(library?.packages.length||0,832);state.writeUInt32LE(0,836);
  const installed=runtime.report.catalog?.apps||[],stamp=installed.map(a=>a.id+':'+a.fingerprint).join('|');
  if(stamp!==heroStamp){const apps=[];for(const app of installed)apps.push(await catalog.readApp(app.root));heroBytes=(await require('./raster.cjs').raster({apps},directory,'run11-hero-'+Date.now())).subarray(0,apps.length*catalog.SLOT_BYTES);heroStamp=stamp;}
  const generation=stamp+':'+runtime.report.generation;if(heroUpload!==generation&&heroBytes.length){await rpc.write(menu.controls.mapped+1024+catalog.ATLAS_BYTES,heroBytes);heroUpload=generation;}
  let mask=0;for(let i=0;i<(library?.packages.length||0);i++)if(installed.some(a=>a.id===library.packages[i].id))mask|=1<<i;
  state.writeUInt32LE(mask>>>0,840);state.writeUInt32LE(lastSequence,844);state.writeUInt32LE(0,812);if(publishing.length){state.writeUInt32LE(0,800);state.writeUInt32LE(0,804);}
  await rpc.write(menu.controls.mapped+800,state.subarray(800,816));await rpc.write(menu.controls.mapped+832,state.subarray(832,848));
  // Invalidate menu checkpoints after legitimate host edits, at a stopped boundary.
  const epoch=Buffer.alloc(4);epoch.writeFloatLE(state.readFloatLE(108)+1);await rpc.write(menu.controls.mapped+108,epoch);
  publishedPackages=library?.packages||[];dirty=false;report.installed=installed.map(a=>({id:a.id,name:a.name,version:a.version,fingerprint:a.fingerprint}));report.menuReady=true;report.stage='Ready';report.updated=new Date().toISOString();
  for(const job of publishing){const index=publishedPackages.findIndex(p=>p.id===job.packageId);if(job.operation!=='remove'&&index>=0)notifications.enqueue({id:job.id,name:job.name,bytes:libraryBytes.subarray(index*catalog.SLOT_BYTES,(index+1)*catalog.SLOT_BYTES)});job.status='ready';job.completed=new Date().toISOString();await progress.update(job,'ready');await files.event(stateRoot,job.operation==='remove'?'unloaded':'loaded',job.name+(job.operation==='remove'?' removed from the console menu':' loaded into the console menu'),{packageId:job.packageId,source:job.source});}publishing=[];runtime.onUploadProgress=null;await progress.publish();await activity.publish(installed);await notifications.publish();report.notification=notifications.report;await save();
 }
 async function pending(state){
  await inventory();if(!await removal.atMenu(runtime,state,rpc,menu.controls.mapped))return false;
  const sequence=state.readUInt32LE(808),request=state.readUInt32LE(812);
  const work=await notifications.pending()||dirty||(request>0&&sequence!==lastSequence)||(state.readUInt32LE(944)>0&&state.readUInt32LE(948)!==lastRemoveSequence)||(await jobs.all(stateRoot)).some(j=>j.status==='queued');
  if(!work)return false;
  // A built-in stream/calculator owns the same renderer until it returns home.
  const mode=(await rpc.read(menu.controls.mapped+0x700004,4)).readUInt32LE();return mode===0;
 }
 async function perform(state){
  if(!await removal.atMenu(runtime,state,rpc,menu.controls.mapped))return;
  await notifications.capture();await inventory();const sequence=state.readUInt32LE(808),request=state.readUInt32LE(812);
  if(request>0&&sequence!==lastSequence){lastSequence=sequence;const pkg=publishedPackages[request-1];if(pkg)await jobs.create(stateRoot,pkg,'console');}
  const removeRequest=state.readUInt32LE(944),removeSequence=state.readUInt32LE(948);
  if(removeRequest>0&&removeSequence!==lastRemoveSequence){
   lastRemoveSequence=removeSequence;const app=(runtime.report.catalog?.apps||[])[removeRequest-1];
   try{if(!app)throw Error('Selected module is no longer loaded');await removal.queue(stateRoot,[app.id],runtime.report.catalog.apps,'console');}
   catch(error){await files.event(stateRoot,'error',error.message);}
   await rpc.write(menu.controls.mapped+940,Buffer.alloc(8));
  }
  const pending=(await jobs.all(stateRoot)).filter(j=>j.status==='queued');
  const ids=new Set((runtime.report.catalog?.apps||[]).map(a=>a.id));
  for(const [position,job] of pending.entries()){job.status='loading';job.started=new Date().toISOString();await progress.update(job,'verify',{index:position+1,count:pending.length});
   try{
    if(job.operation==='remove'){
     const current=await catalog.scan(menu.catalog.root);const app=current.apps.find(a=>a.id===job.packageId);
     if(!app)throw Error('Module is no longer loaded on the console');
     await validateRemoval(app);
     await require('./composition.cjs').build(current.apps.filter(a=>a.id!==app.id),require('../../../core/session/preload.cjs').cacheRoot(root));
     job.result=await removal.remove(menu.catalog.root,app.id);job.activityType='removed';job.version=app.version;
     ids.delete(app.id);job.status='publishing';publishing.push(job);await jobs.save(stateRoot,job);continue;
    }
    const pkg=await repo.inspect(job.file);if(pkg.id!==job.packageId)throw Error('Package changed identity');
    if(ids.size>=15&&!ids.has(pkg.id))throw Error('The menu supports 15 loaded apps per session');
    await files.event(stateRoot,'unpack','Verifying and unpacking '+job.name+' .izm');
    // Compile on the PC before publishing any catalog change to the console.
    const candidateRoot=path.join(stateRoot,'validated',job.id);await repo.install(job.file,candidateRoot,{onProgress:bytes=>progress.update(job,'prepare',bytes,false)});
    const candidate=await catalog.readApp(path.join(candidateRoot,pkg.id)),current=await catalog.scan(menu.catalog.root);
    await validateCandidate(candidate,current.apps);
    await progress.update(job,'graphics');await require('./composition.cjs').build([...current.apps.filter(a=>a.id!==pkg.id),candidate],require('../../../core/session/preload.cjs').cacheRoot(root));
    const previous=current.apps.find(a=>a.id===pkg.id);job.activityType=previous?'updated':'installed';job.version=candidate.version;job.previousVersion=previous?.version||'';
    job.result=await repo.install(job.file,menu.catalog.root,{onProgress:bytes=>progress.update(job,'install',bytes,false)});ids.add(pkg.id);job.status='publishing';publishing.push(job);await progress.update(job,'transfer');
   }catch(e){job.status='failed';job.error=e.message;job.progress={...job.progress,phase:'failed',label:'Loading failed',percent:null};await jobs.save(stateRoot,job);await files.event(stateRoot,'error',job.name+': '+e.message);if(e.restartRequired)throw e;}
  }
  if(publishing.length){runtime.onUploadProgress=bytes=>progress.upload(bytes);await rpc.write(menu.controls.mapped+940,Buffer.alloc(8));const counter=Buffer.alloc(4);counter.writeFloatLE(state.readFloatLE(104)+1);await rpc.write(menu.controls.mapped+104,counter);}
 }
 await inventory(true);await publish();await files.event(stateRoot,'ready','IZPSlauncher ready. Press X or touch the on-screen + to browse packages.');
 return {pending,perform,async preparingRAM(){if(publishing.length)await progress.update(publishing.at(-1),'graphics');},async atBoundary(){if(!runtime.active)await applyShell();dirty=true;await publish();},async sample(batch){report.lastFrameBatch=batch;report.active=runtime.report.active||null;report.updated=new Date().toISOString();await telemetry.sample();},async close(){report.menuReady=false;report.stage='Disconnected';report.completed=new Date().toISOString();await save();},report};
}
module.exports={create};
