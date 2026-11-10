'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),pause=ms=>new Promise(r=>setTimeout(r,ms)),id='izdoom-prime18';
function instructionState(bytes){
 assert.equal(bytes.length,64);assert.equal(bytes.readUInt32LE(),0x53525a49,'Local instruction state is not initialized');
 const names=['magic','done_rules','rule','operation','read_done','init_done','stopped','failed','attempted','opened','started','stop_audio','flushed','pending','unknown','closed'];
 return Object.fromEntries(names.map((name,i)=>[name,bytes.readUInt32LE(i*4)]));
}
async function main(session,file,{installed=false,autoReturn=false}={}){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));assert(file);const directory=path.join(root,'data/sessions',session),read=async name=>JSON.parse(await fs.readFile(path.join(directory,name)));
 const bytes=await fs.readFile(file),pkg=require('../../core/console/sd-launcher/package.cjs').decode(bytes);assert(pkg.id===id&&pkg.native&&pkg.flags===7);
 const instructions=pkg.sections.nativeData.length>=64&&pkg.sections.nativeData.readUInt32LE()===0x434c5a49;
 let runtimeOffset=0;if(instructions){assert(pkg.sections.nativeCode.equals(await fs.readFile(path.join(root,'core/console/sd-launcher/instructions/runtime.bin'))),'Rebuild the package with the current instruction runtime');runtimeOffset=pkg.sections.nativeData.readUInt32LE(12);assert(runtimeOffset>=64&&runtimeOffset%16===0&&runtimeOffset+3136<=pkg.nativeDataBytes);}
 const boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'sd-izdoom-hardware',returnOnAppExit:autoReturn}),rpc=require(path.join(directory,'shared/rpc.cjs'));
 const report={format:'iz2s-sd-izdoom-hardware-v1',session,started:new Date().toISOString(),passed:false,instructions,autoReturn,hostModuleImageUploadBytes:0,resourcesRetained:true,observations:[]},save=()=>require('../../core/session/ledger.cjs').saveJson(path.join(directory,'sd-izdoom-hardware.json'),report);
 let supervisor,attachment,storage,clean=false,deploying=false,slot;await save();
 async function state(){const s=await supervisor.status();assert(s.state===2||s.supervisorClosed&&s.state===3,'SD supervisor stopped unexpectedly');assert(!s.poisoned,'Native resource ownership is uncertain');assert(!s.last_app_error,'SD application failed: 0x'+s.last_app_error.toString(16));let audio=null;
  if(!s.supervisorClosed&&s.active&&s.last_id===id&&s.native_data&&s.native_data_bytes>=4096){if(instructions)audio=instructionState(await rpc.read(s.native_data+runtimeOffset+3072,64));else{const b=await rpc.read(s.native_data+3072,56),names=['opened','attempted','started','stopped','flushed','pending','closed','unknown','opens','appends','starts','stops','releases','closes'];audio=Object.fromEntries(names.map((name,i)=>[name,b.readUInt32LE(i*4)]));}const c=await rpc.read(prepared.controls.mapped+784,176);audio.gpu={ack:c.readUInt32LE(32),phase:c.readUInt32LE(36),warm:c.readUInt32LE(44),error:c.readUInt32LE(48),writtenFrames:c.readUInt32LE(92)};}
  return {...s,audio};
 }
 async function wait(fn,ms=120000){const until=Date.now()+ms;for(;;){const value=await fn();if(value)return value;assert(Date.now()<until,'Native IZDOOM startup deadline expired');await pause(250);}}
 async function observe(event){const s=await state();report.observations.push({event,at:new Date().toISOString(),...s});await save();console.log(JSON.stringify({event,...s}));return s;}
 let prepared;
 try{
  assert((await rpc.request({command:'status'})).ready,'Control channel is not ready');
  attachment=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));storage=await require('../../core/console/sd-launcher/storage.cjs').create(attachment);let last=-1;
  if(installed){
   const location=require('../../core/console/sd-launcher/install.cjs').paths(id).packagePath,handle=await storage.openRead(location);assert.equal(handle.size,bytes.length);assert((await storage.read(handle,0,512)).equals(bytes.subarray(0,512)),'The installed package header differs');await storage.close(handle);
   report.install={id,path:location,bytes:handle.size,existing:true,headerVerified:true,readbackVerified:false,nativeSectionCrcValidationRequired:true};
  }else report.install=await require('../../core/console/sd-launcher/install.cjs').installBytes(storage,bytes,{progress:async p=>{const value=Math.floor(p.bytes*100/p.total/10)*10;if(value!==last){last=value;console.log(JSON.stringify({phase:p.phase,percent:Math.floor(p.bytes*100/p.total)}));}}});
  await attachment.close();attachment=null;report.installedAt=new Date().toISOString();await save();
  prepared=await read('launcher-auto.json');const loop=await read('launcher-auto-loop.json'),runtime=await read('run11-runtime.json');assert(!runtime.shell?.privateStorage);
  const parent=await rpc.read(prepared.controls.mapped+80,48);report.parentMenuBefore={selected:parent.readUInt32LE(0),count:parent.readUInt32LE(32)};
  const commands=await rpc.read(loop.base+0x2060,prepared.resources.images.length*8),menu={...prepared,commands:[Array.from({length:prepared.resources.images.length},(_,i)=>commands.readBigUInt64LE(i*8).toString())]};
  deploying=true;supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,menu,displayToken:boundary.token});deploying=false;report.bootstrapSha256=supervisor.report.bootstrapSha256;await save();
  const ready=await wait(async()=>{const s=await supervisor.status();assert(s.state!==4,'SD startup failed');return s.state===2&&s.catalogStable?s:null;});slot=ready.applications.findIndex(p=>p.id===id)+1;assert(slot>0,'Native catalog rejected IZDOOM');report.catalog=ready.applications;
  await supervisor.command(slot);
  await wait(async()=>{const s=await state();return s.audio?.started===1&&s.audio.gpu.ack===1?s:null;});const started=await observe('audio-started');if(instructions){assert.equal(started.audio.init_done,1);assert.equal(started.audio.failed,0);assert.equal(started.audio.done_rules&48,48);assert.equal(started.audio.opened,1);}else{assert.equal(started.audio.opens,1);assert.equal(started.audio.appends,32);assert.equal(started.audio.starts,1);}assert.equal(started.audio.pending,0xffffffff);assert.equal(started.audio.unknown,0);
  await pause(3000);await observe('playing');report.readyAt=new Date().toISOString();report.hostConsoleRequestsPaused=true;await save();console.log('READY: native SD IZDOOM and AudioOut running; no further console polling until a stdin command.');
  for await(const line of require('node:readline').createInterface({input:process.stdin,crlfDelay:Infinity})){
   const command=line.trim();
   if(command==='finish'){
    assert(autoReturn,'Automatic display return was not enabled');await wait(async()=>supervisor.report.autoReturn?.released,25000);assert(boundary.released);clean=true;report.resourcesRetained=false;report.automaticDisplayReturn={...supervisor.report.autoReturn};report.parentSnapshotBytes=supervisor.report.parentSnapshotBytes;const parent=await rpc.read(prepared.controls.mapped+80,48);report.parentMenuAfter={selected:parent.readUInt32LE(0),count:parent.readUInt32LE(32)};assert.equal(report.parentMenuAfter.count,report.parentMenuBefore.count,'The ordinary menu catalog was not restored');report.passed=true;report.closedAt=new Date().toISOString();await save();console.log('RETURNED: automatic native cleanup completed and ordinary launcher resumed with its original catalog.');return;
   }
   if(command==='status'){await observe('manual-status');continue;}
   if(command==='exit'){await rpc.write(prepared.controls.mapped+1016,Buffer.from([1,0,0,0]));await wait(async()=>!(await state()).active);await observe('returned');continue;}
   if(command==='launch'){const current=await state();assert.equal(current.active,0);assert(!current.supervisorClosed,'Native supervisor has already returned display ownership');await supervisor.command(slot);await wait(async()=>(await state()).audio?.started===1);await observe('relaunched');continue;}
   if(command==='close'){
    if((await state()).active){await rpc.write(prepared.controls.mapped+1016,Buffer.from([1,0,0,0]));await wait(async()=>!(await state()).active);}
    await supervisor.close();clean=true;await boundary.release();report.resourcesRetained=false;report.passed=true;report.closedAt=new Date().toISOString();await save();console.log('CLOSED: joined SD supervisor, ordinary launcher resumed.');return;
   }
   if(command==='rebooted'){report.retiredAfterReboot=true;await save();return;}
  }
  throw Object.assign(Error('Harness input lost; resources retained'),{resourcesRetained:true});
 }catch(e){report.error=e.message;await save();if(!require('../../core/session/offline-policy.cjs').isTransportLoss(e)&&!e.resourcesRetained&&supervisor){try{await supervisor.close();clean=true;}catch(cleanup){report.cleanupError=cleanup.message;await save();}}
  if(attachment&&storage&&!storage.uncertain)await attachment.close();if(clean||(!supervisor&&!deploying&&!storage?.uncertain)){await boundary.release();report.resourcesRetained=false;await save();}throw e;
 }
}
if(require.main===module)main(process.argv[2],process.argv[3],{installed:process.argv.includes('--installed'),autoReturn:process.argv.includes('--auto-return')}).then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1);});module.exports={main,instructionState};
