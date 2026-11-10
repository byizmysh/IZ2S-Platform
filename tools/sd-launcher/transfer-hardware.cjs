'use strict';
// Small, uniquely named SD diagnostic. Borrows the free menu through the same
// joined display lease as app trials, and never starts an app or presses Plus.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..'),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.join(root,'data/sessions',session),nonce=crypto.randomBytes(8).toString('hex'),file=path.join(directory,'sd-transfer-hardware-'+nonce+'.json');
 const report={format:'iz-sd-transfer-hardware-v1',session,started:new Date().toISOString(),passed:false,temporaryOnly:true,resourcesRetained:false};const save=()=>require('../../core/session/ledger.cjs').saveJson(file,report);
 let lease,attachment,storage,opened,target,known=true;await save();
 try{
  lease=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory,{id:'sd-transfer-measurement',returnOnAppExit:false});report.joinedRenderer=true;await save();
  attachment=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));storage=await require('../../core/console/sd-launcher/storage.cjs').create(attachment);
  const {MAX_CHUNK}=require('../../core/console/sd-launcher/storage.cjs'),bytes=Buffer.alloc(MAX_CHUNK*2+17);bytes.fill(71,0,MAX_CHUNK);bytes[MAX_CHUNK+13]=33;bytes[bytes.length-1]=91;report.bytes=bytes.length;report.sha256=hash(bytes);
  await require('../../core/console/sd-launcher/install.cjs').ensure(storage,'iz-sd-launcher');target='/sd/IZ2S/Homebrew/Modules/iz-sd-launcher/.transfer-'+nonce+'.tmp';report.temporaryPath=target;
  let at=performance.now();opened=await storage.openWrite(target,bytes.length);for(let offset=0;offset<bytes.length;offset+=MAX_CHUNK)await storage.write(opened,offset,bytes.subarray(offset,offset+MAX_CHUNK));await storage.flush(opened);await storage.close(opened);opened=null;report.writeMilliseconds=performance.now()-at;report.writeMetrics=storage.metrics;await save();
  at=performance.now();opened=await storage.openRead(target);assert.equal(opened.size,bytes.length);const digest=crypto.createHash('sha256');for(let offset=0;offset<bytes.length;offset+=MAX_CHUNK)digest.update(await storage.read(opened,offset,Math.min(MAX_CHUNK,bytes.length-offset)));assert.equal(digest.digest('hex'),report.sha256);await storage.close(opened);opened=null;report.fullReadMilliseconds=performance.now()-at;report.fullReadMetrics=storage.metrics;await save();
  at=performance.now();opened=await storage.openRead(target);assert.equal(opened.size,bytes.length);let checked=0;for(let offset=0;offset<bytes.length;offset+=MAX_CHUNK){const count=Math.min(MAX_CHUNK,bytes.length-offset);assert.equal(await storage.readHash(opened,offset,count),hash(bytes.subarray(offset,offset+count)));checked+=count;}assert.equal(checked,bytes.length);await storage.close(opened);opened=null;report.digestReadMilliseconds=performance.now()-at;report.digestReadMetrics=storage.metrics;
  await storage.remove(target);await storage.commit();target=null;report.temporaryRemoved=true;report.passed=true;
 }catch(error){report.error=error.message;known=!error.resourcesRetained&&!storage?.uncertain;report.resourcesRetained=!known;}
 finally{
  if(known&&storage&&!storage.uncertain)try{if(opened)await storage.close(opened);if(target){await storage.remove(target);await storage.commit();report.temporaryRemoved=true;}}catch(error){known=false;report.resourcesRetained=true;report.cleanupError=error.message;}
  if(known&&attachment)try{await attachment.close();assert(!attachment.s.base&&!attachment.s.handles.size&&!attachment.s.threads.size,'SD diagnostic allocation cleanup was not verified');}catch(error){known=false;report.resourcesRetained=true;report.cleanupError=error.message;}
  if(known&&lease)try{await lease.release();report.menuResumed=true;}catch(error){known=false;report.resourcesRetained=true;report.cleanupError=error.message;}
  report.finished=new Date().toISOString();report.knownClean=known;await save();console.log(JSON.stringify({...report,reportFile:file}));
 }
 return report;
}
module.exports={main};if(require.main===module)main(process.argv[2]).then(report=>{if(!report.passed)process.exitCode=1;}).catch(error=>{console.error(error);process.exitCode=1;});
