'use strict';
// Once ready, this harness makes no console requests until an explicit stdin
// command. The original host renderer stays joined throughout the trial.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.resolve(__dirname,'../../data/sessions',session),read=async n=>JSON.parse(await fs.readFile(path.join(directory,n)));
 const boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'sd-physical-offline-trial',returnOnAppExit:false}),rpc=require(path.join(directory,'shared/rpc.cjs'));
 const prepared=await read('launcher-auto.json'),loop=await read('launcher-auto-loop.json'),runtime=await read('run11-runtime.json');assert(!runtime.shell?.privateStorage);
 const b=await rpc.read(loop.base+0x2060,prepared.resources.images.length*8),commands=Array.from({length:prepared.resources.images.length},(_,i)=>b.readBigUInt64LE(i*8).toString());
 const supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,menu:{...prepared,commands:[commands]},displayToken:boundary.token});
 let state;const deadline=Date.now()+15000;do{state=await supervisor.status();if(state.state===2)break;assert(state.state!==4);await pause(100);}while(Date.now()<deadline);assert.equal(state.state,2);assert(state.count>=2);
 const file=path.join(directory,'sd-physical-trial.json'),report={format:'iz-sd-physical-trial-v1',session,readyAt:new Date().toISOString(),initial:state,bootstrapSha256:supervisor.report.bootstrapSha256,hostModuleImageUploadBytes:0,hostConsoleRequestsPaused:true,physicalResult:'pending',resourcesRetained:true};
 const save=()=>require('../../core/session/ledger.cjs').saveJson(file,report);await save();console.log(JSON.stringify({ready:true,applications:state.count,consolePolling:false,report:file}));
 for await(const line of require('node:readline').createInterface({input:process.stdin,crlfDelay:Infinity})){
  const command=line.trim();
  if(command==='close'){report.final=await supervisor.status();await supervisor.close();await boundary.release();report.resourcesRetained=false;report.closedAt=new Date().toISOString();await save();console.log('Joined and released.');return;}
  if(command==='offline'){report.physicalDisconnectedAt=new Date().toISOString();await save();console.log('Offline observation recorded; no console call issued.');}
  if(command==='rebooted'){report.retiredAfterPhysicalReboot=true;report.retiredAt=new Date().toISOString();await save();console.log('Retired after physical reboot; no old-session call issued.');return;}
 }
 // EOF does not establish that a console-owned worker has stopped.
 throw Error('Trial harness lost its input; console resources remain retained');
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
