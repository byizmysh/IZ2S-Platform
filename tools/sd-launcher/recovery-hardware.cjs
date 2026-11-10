'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.resolve(__dirname,'../../data/sessions',session),rpc=require(path.join(directory,'shared/rpc.cjs'));
 const status=await rpc.request({command:'status'});assert(status.ready&&status.recovery?.available&&!status.recovery.enabled,'A healthy host with an unarmed recovery manager is required');
 const boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'native-recovery-hardware-test'});console.log('Original renderer joined.');
 const worker=await require('../../core/console/recovery/bootstrap.cjs').deploy({directory,displayToken:boundary.token,qualificationMode:1}),report={format:'iz-native-recovery-hardware-v3',session,started:new Date().toISOString(),bootstrapSha256:worker.manifest.sha256,foregroundTest:'pending',handedOver:false,hardwareQualified:false,qualifiedSteps:[]},file=path.join(directory,'native-recovery-hardware.json'),save=()=>require('../../core/session/ledger.cjs').saveJson(file,report);
 await save();let state;
 for(let stage=1;stage<=10;stage++){
  report.qualifying=stage;await save();if(stage>1)await worker.qualify(stage);const deadline=Date.now()+5000;
  do{state=await worker.status();if(state.state===2&&state.qualificationDone===stage)break;assert(state.state!==4,'SDK qualification failed: '+state.error);await pause(50);}while(Date.now()<deadline);
  assert.equal(state.qualificationDone,stage,'SDK qualification did not finish');report.qualifiedSteps.push({stage,state});await save();console.log(JSON.stringify({event:'sdk-step-verified',stage,ticks:state.workerTicks}));
 }
 assert(state.listener>=0&&state.threadType>4096);report.initial=state;await save();
 const functions=worker.report.foregroundFunctions,target=state.threadType;assert(target!==worker.report.foregroundThread);
 report.foregroundTest='suspending-auxiliary';await save();await rpc.callAddress(functions.suspend_thread.address,[target]);
 const held1=await worker.status();await pause(150);const held2=await worker.status();report.suspended={first:held1.workerTicks,second:held2.workerTicks};
 // Always resume the known auxiliary target before asserting the observation.
 await rpc.callAddress(functions.resume_thread.address,[target]);await pause(150);const resumed=await worker.status();report.resumed=resumed.workerTicks;report.foregroundTest='auxiliary-resumed';await save();assert.equal(held1.workerTicks,held2.workerTicks,'Auxiliary worker did not suspend');assert(resumed.workerTicks>held2.workerTicks,'Auxiliary worker did not resume');console.log(JSON.stringify({event:'suspend-resume-verified',suspended:report.suspended,resumed:report.resumed}));
 await worker.enableForegroundParking();await worker.qualify(0);report.foregroundTest='armed-for-authenticated-handover';await save();
 const armed=await rpc.request({command:'recovery-arm'});assert(armed.enabled&&armed.protocol===3);report.handedOver=true;report.link=armed;await save();
 const running=await worker.status();assert.equal(running.foregroundState,2);const current=require('../../core/console/abi/thread-code.json').functions.current;const thread=await rpc.callAddress(rpc.a.sdk+current.offset,[]);assert.equal(Number(thread.result),running.threadType);assert.notEqual(Number(thread.result),worker.report.foregroundThread);
 report.foregroundTest='basic-thread-parked-native-control-running';report.final=running;await boundary.release();report.rendererResumed=true;report.completed=new Date().toISOString();await save();console.log(JSON.stringify({passed:true,report:file,link:armed}));
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
