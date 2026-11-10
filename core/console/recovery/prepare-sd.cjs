'use strict';
// A recoverable native channel and parked BASIC foreground are prerequisites
// for both launcher modes, including hardware harnesses at a joined menu.
const path=require('node:path'),assert=require('node:assert/strict');
async function prepare({directory,displayToken},injected={}){
 const ledger=require('../../session/ledger.cjs'),rpc=injected.rpc||require(path.join(directory,'shared/rpc.cjs'));
 const read=injected.readJson||ledger.readJson,save=injected.saveJson||ledger.saveJson;
 const sleep=injected.sleep||(ms=>new Promise(r=>setTimeout(r,ms))),now=injected.now||Date.now;
 const manifest=injected.manifest||require('./check.cjs').verify(),L=manifest.layout;
 const deploy=injected.deploy||require('./bootstrap.cjs').deploy,session=path.basename(directory),reportFile=path.join(directory,'native-recovery-worker.json');
 assert.equal(rpc.a.session,session);const initial=await rpc.request({command:'recovery-status'});
 assert(initial.available,'This host cannot protect the launcher foreground; start a fresh launcher with recovery support');
 let installed=false;
 async function owner(){
  const r=await read(reportFile);assert(r&&r.session===session&&r.startedThread&&r.allocationsRetained&&!r.closed&&!r.pending&&!r.threadUnknown,'Recovery ownership is unverified');
  assert.equal(r.bootstrapSha256,manifest.sha256,'Recovery image differs from this runtime');
  assert(Number.isSafeInteger(r.context)&&r.context>=4096&&r.context<2**39-L.server,'Invalid recovery context');
  const b=await rpc.read(r.context,L.server);
  assert(b.readUInt32LE(L.magic)===0x33524e49&&b.readUInt32LE(L.version)===3&&b.readUInt32LE(L.state)===2,'Recovery worker is not running');
  assert(b.readBigUInt64LE(L.sdk)===BigInt(rpc.a.sdk)&&b.readBigUInt64LE(L.scratch)===BigInt(rpc.a.scratch),'Recovery worker belongs to another process');
  const foreground=Number(b.readBigUInt64LE(L.foreground_thread)),worker=Number(b.readBigUInt64LE(L.thread_type));
  assert(foreground===r.foregroundThread&&foreground>4096&&worker>4096&&foreground!==worker,'BASIC foreground identity is unverified');
  for(const field of ['suspend_thread','resume_thread'])assert(r.foregroundFunctions?.[field]?.address&&b.readBigUInt64LE(L[field])===BigInt(r.foregroundFunctions[field].address),'Foreground function binding differs');
  return {r,b};
 }
 async function wait(test){const until=now()+(injected.timeoutMs||10000);for(;;){const value=await test();if(value)return value;assert(now()<until,'Launcher foreground preparation did not complete');await sleep(50);}}
 try{
  if(!initial.enabled){
   if(await read(reportFile)){installed=true;throw Error('A recovery installation already exists; do not replay it');}
   installed=true;const worker=await deploy({directory,displayToken});
   const state=await wait(async()=>{const s=await worker.status();assert(s.state!==4,'Recovery worker initialization failed');return s.state===2&&s.threadType>4096&&s.listener>=0?s:null;});
   const target=state.threadType;assert.notEqual(target,worker.report.foregroundThread);
   // Prove that the identified auxiliary worker can be suspended and resumed
   // while BASIC still owns a healthy legacy channel. Resume before assertions.
   await rpc.callAddress(worker.report.foregroundFunctions.suspend_thread.address,[target]);
   let first,second;try{first=await worker.status();await sleep(80);second=await worker.status();}
   finally{await rpc.callAddress(worker.report.foregroundFunctions.resume_thread.address,[target]);}
   assert.equal(first.workerTicks,second.workerTicks,'Auxiliary worker did not suspend');
   await wait(async()=>{const s=await worker.status();return s.workerTicks>second.workerTicks?s:null;});
   worker.report.auxiliarySuspendResumeVerified=true;await worker.save();
   await worker.enableForegroundParking();const armed=await rpc.request({command:'recovery-arm'});
   assert(armed.enabled&&armed.protocol===3,'Authenticated recovery handover did not finish');
  }
  let bound=await owner();assert(bound.r.handedOver,'The native channel has not accepted ownership');
  if(bound.b.readUInt32LE(L.foreground_state)!==2){
   assert(bound.r.auxiliarySuspendResumeVerified||bound.r.foregroundParkingEnabled,'Qualify foreground parking before using this recovery worker');
   await rpc.write(bound.r.context+L.foreground_policy,Buffer.from([1,0,0,0]));
  }
  bound=await wait(async()=>{const s=await owner();return s.b.readUInt32LE(L.foreground_state)===2?s:null;});
  const status=await rpc.request({command:'recovery-status'});assert(status.enabled&&status.protocol===3&&status.state==='connected','Recovery channel is not synchronized');
  const proof={format:'iz-sd-offline-ready-v1',session,at:new Date().toISOString(),recoveryEnabled:true,foregroundParked:true,bootstrapSha256:manifest.sha256,context:bound.r.context,plusBinding:false};
  await save(path.join(directory,'sd-offline-ready.json'),proof);return proof;
 }catch(e){if(installed||initial.enabled)e.resourcesRetained=true;throw e;}
}
module.exports={prepare};
