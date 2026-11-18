'use strict';
// One native owner for a RAM entry. The whole immutable emulator/engine is
// uploaded to RAM; no IZPSX executable or companion is installed on SD here.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function main(){
 const [directory,appDirectory,fingerprint]=process.argv.slice(2),session=path.basename(directory||''),root=path.resolve(directory||'','../../..');assert(/^launcher-[A-Za-z0-9]+$/.test(session));assert(directory===path.join(root,'data/sessions',session));assert(/^[a-f0-9]{64}$/.test(fingerprint||''));
 const read=async name=>JSON.parse(await fs.readFile(path.join(directory,name))),saveJson=require(path.join(root,'core/session/ledger.cjs')).saveJson;
 const report={format:'izpsx-native-entry/1',source:'session-memory',session,fingerprint,started:new Date().toISOString(),emulatorFilesWrittenToSd:false,passed:false,resourcesRetained:false};
 const file=path.join(directory,'izpsx-launch-'+Date.now()+'.json'),save=()=>saveJson(file,report);await save();await saveJson(path.join(directory,'izpsx-launch-current.json'),{report:file});
 let boundary,supervisor,deploying=false,stop=false;process.on('SIGINT',()=>{stop=true;});process.on('SIGTERM',()=>{stop=true;});
 try{
  const app=await require(path.join(root,'core/services/app-catalog.cjs')).readApp(appDirectory);assert(app.id==='izpsx'&&app.fingerprint===fingerprint,'The selected IZPSX package changed');
  // The parent closes its small entry view before it can yield the renderer.
  const until=Date.now()+15000;for(;;){const runtime=await read('app-runtime.json');assert(!runtime.unsafe&&!runtime.restartRequired&&!runtime.boundaryFailure,'Console cleanup is unverified');if(!runtime.active)break;assert(runtime.active.id==='izpsx','Another app owns the console');assert(Date.now()<until,'The launcher did not leave the IZPSX entry view');await pause(50);}
  const packed=await require(path.join(root,'core/console/sd-launcher/package.cjs')).build(app.root,path.join(directory,'izpsx-ram-'+fingerprint.slice(0,16)));
  const assets=[];for(const a of packed.assets)assets.push({...a,data:await fs.readFile(a.file)});
  boundary=await require(path.join(root,'core/console/sd-launcher/boundary.cjs')).acquire(directory,{id:'izpsx-native-ram'});
  const prepared=await read('launcher-auto.json'),loop=await read('launcher-auto-loop.json'),rpc=require(path.join(directory,'shared/rpc.cjs'));assert(rpc.a.session===session);const raw=await rpc.read(loop.base+0x2060,prepared.resources.images.length*8),commands=Array.from({length:prepared.resources.images.length},(_,i)=>raw.readBigUInt64LE(i*8).toString());
  deploying=true;supervisor=await require(path.join(root,'core/console/sd-launcher/bootstrap.cjs')).deploy({directory,menu:{...prepared,commands:[commands]},displayToken:boundary.token,ramApplication:{bytes:packed.bytes,assets}});deploying=false;report.resourcesRetained=true;await save();
  const readyUntil=Date.now()+30000;let state;do{state=await supervisor.status();assert(state.state!==4,'Native RAM launcher failed: '+state.error);if(state.state===2&&state.catalogStable)break;assert(Date.now()<readyUntil,'Native RAM launcher is still initializing');await pause(100);}while(!stop);
  assert(state.state===2&&state.catalogStable);const slot=state.applications.findIndex(a=>a.id==='izpsx')+1;assert(slot>0&&state.ram_only===1);await supervisor.command(slot);report.launchIssued=true;await save();
  while(!stop&&!boundary.released){await pause(1000);state=await supervisor.status();report.observed=state;assert(state.state!==4&&!state.poisoned,'Native RAM application stopped with error '+state.error);if(state.active&&state.native_launches){const b=await rpc.read(prepared.controls.mapped+784,64);report.emulator={magic:b.readUInt32LE(),phase:b.readUInt32LE(4),error:b.readUInt32LE(12),jitProbeExecutions:b.readUInt32LE(20),revision:b.readUInt32LE(60)};if(report.emulator.magic===0x58505349&&!(report.emulator.revision&1)&&[1,2,3,7].includes(report.emulator.phase)&&!report.emulator.error&&report.emulator.jitProbeExecutions===64){report.passed=true;report.runningAt=new Date().toISOString();}}await save();if(state.supervisorClosed)break;}
  await supervisor.close();await boundary.release();report.resourcesRetained=false;report.cleanClose=true;report.finished=new Date().toISOString();await save();
 }catch(error){report.error=error.stack;report.resourcesRetained=deploying||!!error.resourcesRetained||!!report.resourcesRetained;await save();
  if(!report.resourcesRetained&&boundary){await boundary.release();report.cleanClose=true;await save();}
  if(report.resourcesRetained){console.error('IZPSX retains its native resources; restart the console application before another launch.');for(;;)await pause(1000);}
  throw error;
 }
}
if(require.main===module)main().catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={main};
