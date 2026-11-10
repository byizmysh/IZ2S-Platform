'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..'),pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.join(root,'data/sessions',session),rpc=require(path.join(directory,'shared/rpc.cjs'));assert.equal(rpc.a.session,session);
 const read=async n=>JSON.parse(await fs.readFile(path.join(directory,n))),write=(n,v)=>require('../../core/session/ledger.cjs').saveJson(path.join(directory,n),v);
 const original=await read('app-runtime.json');assert(!original.active,'Another application owns this session');assert(!await fs.access(path.join(directory,'native-owner.json')).then(()=>true,()=>false),'A native owner is active');
 const token=crypto.randomBytes(16).toString('hex'),ownerPid=process.pid,request={version:1,session,token,ownerPid,state:'requested'},report={format:'iz-sd-hardware-test-v1',session,started:new Date().toISOString(),passed:false,observations:[],hostModuleImageUploadBytes:0};
 const save=()=>write('sd-hardware-test.json',report);let paused=false,supervisor,clean=false;
 async function wait(test,timeout=15000){const end=Date.now()+timeout;for(;;){const v=await test();if(v)return v;assert(Date.now()<end,'Hardware test deadline expired');await pause(100);}}
 try{
  await write('app-runtime.json',{...original,active:{id:'sd-supervisor-hardware',ownerPid}});await write('native-display-handoff.json',request);
  await wait(async()=>{const r=await read('native-display-handoff.json');return r.token===token&&r.state==='paused'&&r.workerJoined;});paused=true;report.originalRendererJoined=true;await save();console.log('Original renderer joined.');
  const prepared=await read('launcher-auto.json'),loop=await read('launcher-auto-loop.json'),runtime=await read('run11-runtime.json');assert(!runtime.shell?.privateStorage,'This trial requires an ordinary menu without private shell storage');
  const b=await rpc.read(loop.base+0x2060,prepared.resources.images.length*8),commands=Array.from({length:prepared.resources.images.length},(_,i)=>b.readBigUInt64LE(i*8).toString());
  const menu={...prepared,commands:[commands]};supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,menu,displayToken:token});
  report.bootstrapSha256=supervisor.report.bootstrapSha256;
  const ready=await wait(async()=>{const s=await supervisor.status();if(s.state===4)throw Error('SD startup 0x'+s.error.toString(16));return s.state===2?s:null;});report.observations.push({event:'ready',...ready});await save();console.log(JSON.stringify({event:'ready',...ready}));assert(ready.count>=2);
  for(let attempt=1;attempt<=2;attempt++){
   await supervisor.command(1);await wait(async()=>{const s=await supervisor.status();if(s.state!==2||s.poisoned)throw Error('Native SD launch failed: '+JSON.stringify(s));return s.native_launches>=attempt&&s.active===1?s:null;});
   // No file, graphics or input commands are issued while the CPU module runs.
   await pause(2600);const returned=await wait(async()=>{const s=await supervisor.status();if(s.state!==2||s.poisoned)throw Error('Native SD return failed: '+JSON.stringify(s));return s.active===0&&s.native_launches===attempt?s:null;});
   assert.equal(returned.native_status,0);assert.equal(returned.last_app_error,0);report.observations.push({event:'native-return-'+attempt,...returned});await save();console.log(JSON.stringify({event:'native-return-'+attempt,...returned}));
  }
  await supervisor.command(2);const active=await wait(async()=>{const s=await supervisor.status();if(s.last_app_error)throw Error('GPU SD launch failed: '+JSON.stringify(s));return s.active===2?s:null;});report.observations.push({event:'gpu-from-sd',...active});await save();console.log(JSON.stringify({event:'gpu-from-sd',...active}));
  // This requests the same standard exit mailbox used by hbExit().
  await rpc.write(prepared.controls.mapped+1016,Buffer.from([1,0,0,0]));await wait(async()=>{const s=await supervisor.status();return s.active===0?s:null;});
  await supervisor.close();clean=true;await write('app-runtime.json',original);await write('native-display-handoff.json',{...request,state:'release',cleanupKnown:true});await wait(async()=>{const r=await read('native-display-handoff.json');return r.token===token&&r.state==='resumed';});report.originalRendererResumed=true;report.passed=true;report.finished=new Date().toISOString();await save();console.log(JSON.stringify({passed:true,report:path.join(directory,'sd-hardware-test.json')}));
 }catch(e){report.error=e.message;if(supervisor)report.last=await supervisor.status().catch(()=>null);await save();console.error(e);process.exitCode=1;
  if(supervisor&&!clean){try{await supervisor.close();clean=true;}catch(retained){report.retained=retained.message;await save();}}
  if(paused&&clean){await write('app-runtime.json',original);await write('native-display-handoff.json',{...request,state:'release',cleanupKnown:true});}
  else if(paused){report.resourcesRetained=true;await save();}
 }
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
