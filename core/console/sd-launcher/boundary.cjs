'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const reservation=require('../native/display-reservation.cjs');
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const leases=new Map();
const leaseKey=(directory,token)=>path.resolve(directory)+':'+token;
function current(directory,token){return leases.get(leaseKey(directory,token));}
async function acquire(directory,{id='sd-storage-experiment',timeoutMs=20000,returnOnAppExit=true}={}){
 assert.equal(typeof returnOnAppExit,'boolean');
 const session=path.basename(directory),read=async name=>JSON.parse(await fs.readFile(path.join(directory,name))),write=(name,value)=>require('../../session/ledger.cjs').saveJson(path.join(directory,name),value);
 assert(/^launcher-[A-Za-z0-9]+$/.test(session));const lock=path.join(directory,'native-display-claim.lock');
 try{await fs.mkdir(lock);}catch(error){if(error.code==='EEXIST')throw Error('Another native application is still returning display ownership');throw error;}
 let reserved=false,original,request;
 try{
 original=await read('app-runtime.json');assert(!original.active,'Return the active module to the launcher before an SD experiment');assert(!await fs.access(path.join(directory,'native-owner.json')).then(()=>true,()=>false),'A native module still owns resources');
 let previous;try{previous=await read('native-display-handoff.json');}catch(error){if(error.code!=='ENOENT')throw error;}
 assert(!previous||previous.state==='resumed','The previous application has not returned display ownership');
 const token=crypto.randomBytes(16).toString('hex');request={version:1,session,token,ownerPid:process.pid,state:'requested',reservation:1};await reservation.write(directory,request,id);await write('app-runtime.json',{...original,active:{id,ownerPid:process.pid}});reserved=true;await write('native-display-handoff.json',request);
 const wait=async state=>{const deadline=Date.now()+timeoutMs;for(;;){const row=await read('native-display-handoff.json');if(row.token===token&&row.state==='rejected'&&row.notStarted===true)throw Object.assign(Error(row.reason||'The console application is still active'),{handoffRejected:true,activeApplication:row.activeApplication});if(row.token===token&&row.state===state)return row;assert(Date.now()<deadline,'Joined renderer boundary was not acknowledged; no SD test was started');await pause(100);}};
 const paused=await wait('paused');assert(paused.workerJoined===true);
 let released=false,releasing;
 const lease={token,paused,returnOnAppExit,get released(){return released;},release(){
  if(releasing)return releasing;
  releasing=(async()=>{const row=await read('native-display-handoff.json');assert(row.token===token&&row.ownerPid===process.pid&&row.session===session&&row.state==='paused','Display ownership changed before native cleanup completed');await write('app-runtime.json',original);await write('native-display-handoff.json',{...request,state:'release',cleanupKnown:true});await wait('resumed');await reservation.remove(directory);await fs.rmdir(lock);released=true;leases.delete(leaseKey(directory,token));})();return releasing;
 }};leases.set(leaseKey(directory,token),lease);return lease;
 }catch(error){
  if(reserved&&error.handoffRejected){const row=await read('native-display-handoff.json');assert(row.token===request.token&&row.ownerPid===process.pid&&row.state==='rejected'&&row.notStarted===true);const active=await read('app-runtime.json');if(active.active?.ownerPid===process.pid)await write('app-runtime.json',{...original,active:error.activeApplication||original.active});await write('native-display-handoff.json',{...request,state:'resumed',notStarted:true,rejected:true,reason:error.message});await reservation.remove(directory);await fs.rmdir(lock);}
  else if(!reserved){await reservation.remove(directory);await fs.rmdir(lock);}throw error;
 }
}
async function verify(directory,token){
 // This check happens before attachGame, allocation, services or native calls.
 assert(!await fs.access(path.join(directory,'native-owner.json')).then(()=>true,()=>false),'A native owner is active; SD experiments require the free launcher');
 let loop;try{loop=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto-loop.json')));}catch(e){if(e.code!=='ENOENT')throw e;}
 assert(!loop?.nativeMenuStarting||loop.nativeMenuStartingPid===process.pid,'The ordinary native renderer is still starting');
 if(token||loop?.thread||loop?.nativeMenuRetained){const row=JSON.parse(await fs.readFile(path.join(directory,'native-display-handoff.json')));assert(token&&row.token===token&&row.ownerPid===process.pid&&row.session===path.basename(directory)&&row.state==='paused'&&row.workerJoined===true,'Join the renderer before filesystem/mapping experiments');const runtime=row.reservation===1?null:JSON.parse(await fs.readFile(path.join(directory,'app-runtime.json')));assert(await reservation.matches(directory,row,runtime),'SD experiment lost its application reservation');assert.equal(loop?.threadRunning,false,'The original renderer is still running');if(loop?.nativeMenuRetained)assert(Number.isInteger(loop.nativeMenuParked)&&loop.nativeMenuParked>0,'The mixed native renderer has not acknowledged display ownership');return row;}
 assert(!loop?.threadRunning,'Join the renderer before filesystem/mapping experiments');
 return {session:path.basename(directory),state:'startup-before-render-worker'};
}
module.exports={acquire,verify,current};
