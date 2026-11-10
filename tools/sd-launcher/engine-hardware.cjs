'use strict';
// Qualify the existing inspect-only ABI 4 controller at a joined display boundary.
// The engine entry is never called. A failed transport retains all ownership.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session,file){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));
 file=path.resolve(root,file||'data/build-cache/sd-engine-check/sd/launcher.izapp');assert(file.startsWith(root+path.sep));
 const bytes=await fs.readFile(file),pkg=require('../../core/console/sd-launcher/package.cjs').decode(bytes);
 assert(pkg.nativeAbi===4&&pkg.sections.nativeData.readUInt32LE(80)===1,'An inspect-only ABI 4 package is required');
 const assets=await require('../../core/console/sd-launcher/assets.cjs').loadBundle(file,bytes);
 const expected=await fs.readFile(path.join(root,'data/build-cache/sd-cooperative/arm64/native.bin'));
 assert(pkg.sections.nativeCode.equals(expected),'Use the locally checked inspect-only controller');
 const directory=path.join(root,'data/sessions',session),rpc=require(path.join(directory,'shared/rpc.cjs'));
 assert.equal(rpc.a.session,session);
 const boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'sd-engine-image-qualification',returnOnAppExit:false});
 const report={format:'iz-sd-engine-hardware-v1',session,id:pkg.id,started:new Date().toISOString(),passed:false,engineEntered:false,resourcesRetained:true,observations:[]};
 const save=()=>require('../../core/session/ledger.cjs').saveJson(path.join(directory,pkg.id+'-hardware.json'),report);
 let a,storage,supervisor,deploying=false,clean=false;await save();
 async function wait(test,ms=120000){const until=Date.now()+ms;for(;;){const value=await test();if(value)return value;assert(Date.now()<until,'Engine image qualification deadline expired');await pause(100);}}
 async function state(){const s=await supervisor.status();assert.equal(s.state,2,'SD supervisor stopped');assert.equal(s.poisoned,0,'Resource ownership is uncertain');assert.equal(s.last_app_error,0,'Image app error 0x'+s.last_app_error.toString(16));return s;}
 try{
  a=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));storage=await require('../../core/console/sd-launcher/storage.cjs').create(a);
  report.install=await require('../../core/console/sd-launcher/install.cjs').installBytes(storage,bytes,{assets});await a.close();a=null;await save();
  const prepared=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto.json'))),loop=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto-loop.json')));
  const commands=await rpc.read(loop.base+0x2060,prepared.resources.images.length*8),menu={...prepared,commands:[Array.from({length:prepared.resources.images.length},(_,i)=>commands.readBigUInt64LE(i*8).toString())]};
  deploying=true;supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,menu,displayToken:boundary.token});deploying=false;report.bootstrapSha256=supervisor.report.bootstrapSha256;
  const initial=await wait(async()=>{const s=await supervisor.status();assert(s.state!==4,'SD supervisor failed');return s.state===2&&s.catalogStable?s:null;});
  const slot=initial.applications.findIndex(x=>x.id===pkg.id)+1;assert(slot>0,'Image app was rejected by the SD catalog');report.initial=initial;await save();
  for(let attempt=1;attempt<=2;attempt++){
   await supervisor.command(slot);let appFrames=0,stopFrames=0,progressSamples=0;const stages=new Set();
   const returned=await wait(async()=>{const s=await state();
    // Shared controls outlive app STOP. Never chase app data after a snapshot:
    // it can be freed before the host's next read reaches the console.
    if(s.active===slot&&s.last_id===pkg.id){const b=await rpc.read(prepared.controls.mapped+784,16);appFrames=Math.max(appFrames,b.readUInt32LE(0));stopFrames=Math.max(stopFrames,b.readUInt32LE(12));stages.add(b.readUInt32LE(8));progressSamples++;}
    return s.native_launches===attempt&&s.active===0&&s.transitions===attempt*2?s:null;
   });
   assert.equal(returned.native_status,0);assert.equal(returned.opened,returned.closed);
   report.observations.push({attempt,appFrames,stopFrames,stages:[...stages],progressSamples,...returned});await save();console.log(JSON.stringify({attempt,frames:returned.frames,transitions:returned.transitions,appFrames,stopFrames,stages:[...stages],progressSamples,error:returned.last_app_error}));
  }
  await supervisor.close();clean=true;await boundary.release();report.passed=true;report.resourcesRetained=false;report.finished=new Date().toISOString();await save();console.log(JSON.stringify({passed:true,engineEntered:false,report:path.join(directory,pkg.id+'-hardware.json')}));
 }catch(e){report.error=e.message;
  if(!require('../../core/session/offline-policy.cjs').isTransportLoss(e)&&!e.resourcesRetained&&supervisor){try{await supervisor.close();clean=true;}catch(close){report.cleanupError=close.message;}}
  if(a&&!storage?.uncertain&&!e.resourcesRetained)await a.close();
  if(clean||!supervisor&&!deploying&&!storage?.uncertain&&!e.resourcesRetained){await boundary.release();report.resourcesRetained=false;}
  await save();throw e;
 }
}
if(require.main===module)main(process.argv[2],process.argv[3]).then(()=>process.exit(0)).catch(e=>{console.error(e);process.exit(1);});
module.exports={main};
