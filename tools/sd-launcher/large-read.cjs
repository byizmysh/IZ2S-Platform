'use strict';
// Read-only staged qualification: bounded SD reads and CRC; --gpu adds only
// a private coherent Vulkan mapping. No app entry, draw or AudioOut operation.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session,id='izdoom-prime18',gpu=false){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.resolve(__dirname,'../../data/sessions',session),report={format:'iz2s-sd-large-read-v1',session,id,gpuMapping:gpu,started:new Date().toISOString(),passed:false,resourcesRetained:true};
 const save=()=>require('../../core/session/ledger.cjs').saveJson(path.join(directory,gpu?'sd-large-gpu-read.json':'sd-large-read.json'),report),boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'sd-read-only-qualification'});let worker,deploying=false,clean=false;const rpc=require(path.join(directory,'shared/rpc.cjs'));let menu;
 if(gpu){const p=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto.json'))),loop=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto-loop.json'))),b=await rpc.read(loop.base+0x2060,p.resources.images.length*8);menu={...p,commands:[Array.from({length:p.resources.images.length},(_,i)=>b.readBigUInt64LE(i*8).toString())]};}
 try{
  await save();deploying=true;worker=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,probe:gpu?4:3,menu,probeAppId:id,probeNonce:Date.now()>>>0,displayToken:boundary.token});deploying=false;report.bootstrapSha256=worker.report.bootstrapSha256;
  let last=-1;const until=Date.now()+120000;for(;;){const s=await worker.status();report.last=s;if(s.state!==1&&s.state!==0)break;assert(Date.now()<until,'Read probe deadline expired');const megabytes=Math.floor(s.read_bytes/1048576/8)*8;if(last!==megabytes){last=megabytes;await save();console.log(JSON.stringify({readMiB:megabytes}));}await pause(200);}
  assert.equal(report.last.state,3);assert.equal(report.last.probe_result,0);assert.equal(report.last.opened,report.last.closed);assert.equal(report.last.native_launches,0);assert(report.last.read_bytes>48*1048576);
  await worker.close();clean=true;await boundary.release();report.passed=true;report.resourcesRetained=false;report.finished=new Date().toISOString();await save();console.log(JSON.stringify({passed:true,...report.last}));
 }catch(e){report.error=e.message;if(!require('../../core/session/offline-policy.cjs').isTransportLoss(e)&&!e.resourcesRetained&&worker){try{await worker.close();clean=true;}catch(cleanup){report.cleanupError=cleanup.message;}}
  if(clean||!worker&&!deploying)await boundary.release();report.resourcesRetained=!clean;await save();throw e;
 }
}
if(require.main===module)main(process.argv[2],process.argv[3],process.argv.includes('--gpu')).then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1);});module.exports={main};
