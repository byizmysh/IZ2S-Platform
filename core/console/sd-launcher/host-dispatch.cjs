'use strict';
// Original-host wire admission. This never attests native worker/resource exit.
// It is dormant until the trusted launcher loader begins a display lease.
const assert=require('node:assert/strict'),path=require('node:path'),{AsyncLocalStorage}=require('node:async_hooks');
const clients=new WeakMap(),directories=new Map(),LIMIT=2**39,FRAME_BYTES=0x8080;
function address(n){assert(Number.isSafeInteger(n)&&n>=4096&&n<LIMIT&&n%4===0,'Invalid host dispatch address');return n;}
function lookup(directory){return directories.get(path.resolve(directory));}
async function activity(native){
 const sdk=native.addresses.sdk,offset=0x1b18a0;
 assert((await native.read(sdk+offset,12)).toString('hex')==='0020009121088052e6401014','SDK activity entry differs');
 const controller=Number((await native.call(0x1aba00)).result);address(controller);assert(controller%8===0);
 const table=Number((await native.read(controller,8)).readBigUInt64LE());assert(table===sdk+0xccca90);assert((await native.read(table+0x120,8)).readBigUInt64LE()===BigInt(sdk+offset));
 return [{address:sdk+offset,args:[controller,0,0,0]}];
}
function create({native,directory,prepareMaintenance=activity,protectedParent=process.env.IZ2S_NATIVE_PARENT_RETENTION==='1',mappingClock=Date.now,mappingPause=ms=>new Promise(resolve=>setTimeout(resolve,ms)),mappingTimeoutMs=30000}){
 assert(native&&native.addresses?.sdk&&native.addresses?.scratch&&typeof protectedParent==='boolean');directory=path.resolve(directory);
 if(clients.has(native)){const old=clients.get(native);assert(old.directory===directory,'This native client belongs to another host directory');return old;}
 assert(!directories.has(directory),'Another native client owns this host dispatch directory');
 const sdk=address(native.addresses.sdk),scratch=address(native.addresses.scratch),output=native.output??scratch+0x4040,scope=new AsyncLocalStorage();
 let phase='open',epoch=0,lease=null,configuration=null,ready=false,preparing,used=false,shuttingDown=false,tail=Promise.resolve(),pending=0,denied=0,accepted=0,maintenance=[];
 const epochStates=new Map(),retainedEntryPages=new Set();
 let guardedSupervisor=null,cancelOffer=0,cancelOperation=null,framesPreparing=false,framesPublished=false;
 function reject(message='Native request has no current launcher dispatch permission',retired=shuttingDown||epochStates.get(epoch)?.retired===true||phase==='retained'){denied++;return Object.assign(Error(message),{code:'IZ2S_HOST_DISPATCH_REVOKED',requestReplayed:false,nativeRequestSent:false,generationRevoked:retired});}
 function uncertain(error){phase='retained';used=true;return Object.assign(Error('Host dispatch completion is unverified; resources retained',{cause:error}),{code:'IZ2S_HOST_DISPATCH_UNCERTAIN',resourcesRetained:true,requestReplayed:false});}
 function enqueue(action){pending++;const job=tail.then(action);tail=job.catch(()=>{});return job.finally(()=>pending--);}
 async function permitted(kind,fn,extra={}){
  const permit={kind,epoch,active:true,...extra};try{return await scope.run(permit,fn);}finally{permit.active=false;}
 }
 async function failed(error){
  if(lease?.guarded&&guardedSupervisor&&!shuttingDown&&!native.connection?.awaitingResponse&&!native.connection?.inflight){
   try{const started=mappingClock();for(;;){const proof=await permitted('observer',()=>require('./host-binding.cjs').observeOwnership({native,supervisor:guardedSupervisor,epoch,returned:true}));if(proof.returned){retire(epoch);return Object.assign(Error('The console returned to its menu',{cause:error}),{nativeGenerationReturned:true,requestReplayed:false,resourcesReleased:false,...(error.nativeMappingRejected?{nativeMappingRejected:true,nativeMappingError:error.nativeMappingError}:{})});}if(!error.nativeMappingRejected||mappingClock()-started>=mappingTimeoutMs)break;await mappingPause(10);}}catch{}
  }
  return uncertain(error);
 }
 function record(bytes,index){const at=index*40;return {fn:bytes.readBigUInt64LE(at),args:[0,1,2,3].map(i=>bytes.readBigUInt64LE(at+8+i*8))};}
 function same(row,fn,args){return row.fn===BigInt(fn)&&row.args.every((n,i)=>n===BigInt(args[i]??0));}
 function noop(row){return same(row,sdk+0x5872f0,[output,output,0,0]);}
 function observation(row){
  const a=row.args;
  if(row.fn===BigInt(sdk+0x158314))return a[0]===BigInt(output)&&a[1]===BigInt(output+64)&&a[2]>=4096n&&a[2]<BigInt(LIMIT)&&a[3]===0n;
  if(![sdk+0x586fb0,sdk+0x5872f0].some(fn=>row.fn===BigInt(fn)))return false;
  return a[0]>=BigInt(output)&&a[0]<=BigInt(output+32768)&&a[2]<=32768n&&a[0]+a[2]<=BigInt(output+32768)&&a[1]>=4096n&&a[1]+a[2]<BigInt(LIMIT)&&a[3]===0n;
 }
 function guarded(rows,bytes){
  const c=configuration,[first,second]=rows;if(!c)return false;
  if(first.fn===BigInt(c.packet)){
   const n=Number(first.args[3]),id=bytes.readBigUInt64LE(144);
   return first.args[0]===BigInt(c.owner)&&first.args[1]===BigInt(epoch)&&first.args[2]===BigInt(scratch+0x40080)&&n>=192&&n<=32768&&bytes.readUInt32LE(128)===0x495a5250&&bytes.readUInt32LE(132)===1&&bytes.readUInt32LE(136)===n&&id>0n&&same(second,c.receipt,[c.owner,epoch,id,0]);
  }
  if(first.fn===BigInt(c.fullWrite))return first.args[0]>=4096n&&first.args[0]+first.args[2]<BigInt(LIMIT)&&first.args[1]===BigInt(scratch+0x40080)&&first.args[2]<=32768n&&(first.args[3]&0x00ffffffffffffffn)>0n&&second.fn===BigInt(c.fullReceipt);
  return first.fn===BigInt(c.receipt)&&first.args[0]===BigInt(c.owner)&&first.args[1]===BigInt(epoch)&&first.args[2]>0n&&first.args[3]===1n&&noop(second);
 }
 function beforeWrite(bytes){
  const permit=scope.getStore();if(permit&&!permit.active)throw reject('An expired host callback cannot send native requests');
  if(phase==='open'||phase==='legacy-shutdown')return;
  if(!Buffer.isBuffer(bytes)||bytes.length!==FRAME_BYTES)throw reject('Invalid guarded native frame');
  const rows=[record(bytes,0),record(bytes,1)];
  const system=rows.every(row=>noop(row)||maintenance.some(call=>same(row,call.address,call.args)));
  if(system){accepted++;return;}
  if(!permit||permit.epoch!==epoch||shuttingDown)throw reject(undefined,permit?.epoch!==undefined&&permit.epoch!==epoch||shuttingDown||epochStates.get(epoch)?.retired===true);
  if(permit.kind==='app'&&(phase!=='active'||epochStates.get(epoch)?.retired)||permit.kind==='setup'&&phase!=='sealed'||permit.kind==='commit'&&phase!=='opening'||permit.kind==='mapping'&&phase!=='mapping'||permit.kind==='claim'&&phase!=='claiming')throw reject('The queued launcher permission has expired');
  if(rows.every(observation)){accepted++;return;}
  if(permit.kind==='setup'&&phase==='sealed'&&lease?.active&&!lease.guarded){used=true;accepted++;return;}
  if(permit.kind==='claim'&&phase==='claiming'&&lease?.active&&!permit.published&&noop(rows[1])&&same(rows[0],sdk+0x5872f0,[permit.offer,scratch+0x40080,4,0])&&bytes.readUInt32LE(128)===epoch){permit.published=true;used=true;accepted++;return;}
  if(permit.kind==='cancel'&&lease?.guarded&&['sealed','mapping','mapped','active','parked'].includes(phase)&&!permit.published&&noop(rows[1])&&(!permit.errorCode?same(rows[0],sdk+0x5872f0,[cancelOffer,scratch+0x40080,4,0])&&bytes.readUInt32LE(128)===epoch:same(rows[0],sdk+0x5872f0,[cancelOffer-4,scratch+0x40080,8,0])&&bytes.readUInt32LE(128)===permit.errorCode&&bytes.readUInt32LE(132)===epoch)){permit.published=true;used=true;accepted++;return;}
  if(permit.kind==='frames'&&phase==='active'&&lease?.guarded&&lease.active&&noop(rows[1])){
   if(!permit.staged&&same(rows[0],sdk+0x5872f0,[permit.staging,scratch+0x40080,permit.plan.length,0])&&bytes.subarray(128,128+permit.plan.length).equals(permit.plan)){permit.staged=true;used=true;accepted++;return;}
   if(permit.staged&&!permit.published&&same(rows[0],sdk+0x5872f0,[permit.offer,scratch+0x40080,4,0])&&bytes.readUInt32LE(128)===epoch){permit.published=true;used=true;accepted++;return;}
  }
  if(permit.kind==='commit'&&phase==='opening'&&lease?.active&&!permit.published&&noop(rows[1])&&same(rows[0],sdk+0x5872f0,[permit.offer,scratch+0x40080,4,0])&&bytes.readUInt32LE(128)===epoch){permit.published=true;used=true;accepted++;return;}
  if(permit.kind==='mapping'&&phase==='mapping'&&lease?.active&&noop(rows[1])){
   if(!permit.staged&&same(rows[0],sdk+0x5872f0,[permit.staging,scratch+0x40080,permit.seed.length,0])&&bytes.subarray(128,128+permit.seed.length).equals(permit.seed)){permit.staged=true;used=true;accepted++;return;}
   if(permit.staged&&!permit.published&&same(rows[0],sdk+0x5872f0,[permit.offer,scratch+0x40080,4,0])&&bytes.readUInt32LE(128)===epoch){permit.published=true;used=true;accepted++;return;}
  }
  if(permit.kind==='app'&&phase==='active'&&!epochStates.get(epoch)?.retired&&guarded(rows,bytes)){used=true;accepted++;return;}
  throw reject();
 }
 async function prepare(){
  assert(typeof native.call==='function','Guarded SDK dispatch requires the original callable native client');
  assert(!shuttingDown&&phase==='open','Prepare the host dispatch before opening a generation');if(ready)return;
  preparing??=Promise.resolve().then(()=>prepareMaintenance(native)).then(rows=>{assert(Array.isArray(rows)&&rows.length<=2);maintenance=rows.map(row=>({address:address(row.address),args:[...row.args]}));for(const row of maintenance)assert(row.args.length===4&&row.args.every(n=>Number.isSafeInteger(n)&&n>=0));ready=true;}).catch(error=>{throw uncertain(error);});return preparing;
 }
 function begin(displayLease){
  assert(protectedParent,'Guarded dispatch requires the original runner and panel parent retention policy');
  assert(!shuttingDown&&ready&&['open','parked'].includes(phase)&&!pending,'Host dispatch cannot begin another display lease');
  assert(displayLease?.active&&Number.isInteger(displayLease.token)&&displayLease.token>epoch&&displayLease.token<=0xffffffff,'A fresh exact display lease is required');
  lease=displayLease;epoch=displayLease.token;phase='sealing';configuration=null;guardedSupervisor=null;cancelOffer=0;cancelOperation=null;framesPublished=framesPreparing=false;epochStates.set(epoch,{retired:false});
  // A harmless ordered packet proves completion of pre-seal SDK dispatch.
  // Later raw requests are refused at the actual write, not when enqueued.
  return enqueue(async()=>{try{await native.call(0x5872f0,output,output,0);assert(!shuttingDown&&phase==='sealing'&&lease.active,'Display lease changed while draining legacy requests');phase='sealed';return inspect();}catch(error){throw uncertain(error);}});
 }
 function setup(fn){if(lease?.guarded)throw reject('Guarded preparation only accepts native mapping or observation');assert(!shuttingDown&&phase==='sealed'&&lease?.active&&typeof fn==='function','Setup needs the exact sealed legacy display lease');return enqueue(()=>permitted('setup',fn));}
 function claimNativeDisplay(options){
  assert(protectedParent&&!shuttingDown&&ready&&['open','parked'].includes(phase)&&!pending,'The original protected host must be idle before a native claim');phase='claiming';configuration=null;
  return enqueue(async()=>{try{
   await native.call(0x5872f0,output,output,0);assert(!shuttingDown&&phase==='claiming','Host claim changed while draining legacy requests');
   const claim=await permitted('observer',()=>require('./host-binding.cjs').prepareClaim({...options,native,minimumEpoch:epoch+1}));epoch=claim.epoch;const token=epoch;epochStates.set(token,{retired:false});lease=Object.freeze({token,guarded:true,get active(){return !shuttingDown&&epoch===token&&!epochStates.get(token)?.retired;}});guardedSupervisor=Object.freeze({...options.supervisor});cancelOffer=claim.cancel;cancelOperation=null;framesPublished=framesPreparing=false;
   await permitted('claim',async()=>{const permit=scope.getStore();await claim.publish();assert(permit.published,'Guarded native display claim was not published');},{offer:claim.offer,published:false});
   const started=mappingClock();let polls=0;
   for(;;){const state=await permitted('observer',()=>claim.status());assert(!shuttingDown&&phase==='claiming'&&lease.active,'Native display claim changed');if(state.done){if(!state.granted)throw Object.assign(Error(state.cancelled?'The console cancelled application preparation':'The console retained its display'),{menuBusy:true,nativeClaimRejected:true,nativeRequestSent:true});break;}assert(mappingClock()-started<mappingTimeoutMs,'Native display claim result is unconfirmed');await mappingPause(Math.min(100,10*(++polls)));}
   await permitted('observer',()=>claim.verify());assert(!shuttingDown&&phase==='claiming'&&lease.active,'Native display claim changed during verification');phase='sealed';return lease;
  }catch(error){if(error.nativeClaimRejected&&!shuttingDown&&phase==='claiming'){const state=epochStates.get(epoch);if(state)state.retired=true;lease=null;phase='parked';throw error;}throw uncertain(error);}});
 }
 function commit(entries,offer,publish,verify){
  assert(!shuttingDown&&phase==='sealed'&&lease?.active&&!pending,'Finish every setup request before opening native admission');
  assert(typeof publish==='function'&&typeof verify==='function');const c=Object.fromEntries(['owner','packet','receipt','fullWrite','fullReceipt'].map(name=>[name,address(entries[name])]));assert(c.owner%4096===0&&new Set(Object.values(c)).size===5);configuration=Object.freeze(c);offer=address(offer);phase='opening';
  return enqueue(async()=>{try{
   await permitted('commit',async()=>{const permit=scope.getStore();await publish();assert(permit.published,'The native generation offer was not published');},{offer,published:false});
   const proof=await permitted('observer',verify);assert(proof?.owner===c.owner&&proof?.epoch===epoch&&proof?.offerAccepted===true&&proof?.nativeEpochOpen===true,'Native generation binding was not verified by the loader');
   assert(!shuttingDown&&phase==='opening'&&lease.active,'Display lease changed while opening native admission');phase='active';return inspect();
  }catch(error){throw uncertain(error);}});
 }
 function run(generation,fn){
  if(shuttingDown||phase!=='active'||generation!==epoch||epochStates.get(generation)?.retired)return Promise.reject(reject('The application dispatch generation is retired',true));
  return enqueue(()=>{if(shuttingDown||phase!=='active'||generation!==epoch||epochStates.get(generation)?.retired)throw reject('The queued application generation is retired',true);return permitted('app',fn);});
 }
 function commitPrepared(binding){
  assert(!shuttingDown&&phase==='sealed'&&lease?.active&&!pending,'Finish setup before binding the original host');
  const B=require('./host-binding.cjs'),verified=B.claim(binding,{native,epoch});
  assert(!retainedEntryPages.has(verified.entries.fullWrite),'A previous generation still owns this RX entry page');retainedEntryPages.add(verified.entries.fullWrite);
  return commit(verified.entries,verified.offer,verified.publish,async()=>{const proof=await verified.verify();assert(B.consume(proof,{native,epoch,entries:verified.entries,offer:verified.offer}),'The original-host live binding proof differs');return proof;});
 }
 function prepareBinding(options){return setup(()=>require('./host-binding.cjs').prepare({...options,native,displayLease:lease}));}
 function mapNativeBinding(options){
  assert(!shuttingDown&&phase==='sealed'&&lease?.active&&!pending,'Finish setup before console-owned mapping');phase='mapping';
  return enqueue(async()=>{try{
   const B=require('./host-binding.cjs'),mapping=await permitted('observer',()=>B.prepareMapping({...options,native,displayLease:lease}));
   await permitted('mapping',async()=>{const permit=scope.getStore();await mapping.publish();assert(permit.staged&&permit.published,'Native mapping offer was not published');},{staging:mapping.staging,offer:mapping.offer,seed:mapping.seed,staged:false,published:false});
   const started=mappingClock();let polls=0;
   for(;;){const ack=await permitted('observer',()=>mapping.status());if(ack===epoch)break;assert(ack<epoch&&lease.active&&!shuttingDown&&phase==='mapping','Native mapping epoch changed');assert(mappingClock()-started<mappingTimeoutMs,'Native mapping result is unconfirmed');await mappingPause(Math.min(100,10*(++polls)));}
   const binding=await permitted('observer',()=>mapping.binding());assert(lease.active&&!shuttingDown&&phase==='mapping','Display lease changed during native mapping');phase='mapped';return binding;
  }catch(error){throw await failed(error);}});
 }
 function adoptPrepared(binding){
  assert(!shuttingDown&&phase==='mapped'&&lease?.active&&!pending,'A console-created mapping is required for adoption');
  const B=require('./host-binding.cjs'),verified=B.claim(binding,{native,epoch,mode:'mapped'}),c=Object.fromEntries(['owner','packet','receipt','fullWrite','fullReceipt'].map(name=>[name,address(verified.entries[name])]));
  assert(c.owner%4096===0&&new Set(Object.values(c)).size===5&&!retainedEntryPages.has(c.fullWrite),'A previous generation still owns this RX entry page');retainedEntryPages.add(c.fullWrite);configuration=Object.freeze(c);phase='adopting';
  return enqueue(async()=>{try{const proof=await permitted('observer',()=>verified.verify());assert(B.consume(proof,{native,epoch,entries:verified.entries,offer:verified.offer}),'The original-host native mapping proof differs');assert(lease.active&&!shuttingDown&&phase==='adopting','Display lease changed during native adoption');phase='active';return inspect();}catch(error){throw await failed(error);}});
 }
 function publishNativeFrames(generation,frames){
  assert(!shuttingDown&&phase==='active'&&generation===epoch&&lease?.guarded&&lease.active&&guardedSupervisor&&!framesPreparing&&!framesPublished,'A fresh guarded frame publication is required');framesPreparing=true;
  return enqueue(async()=>{let observed=false,attempted=false;try{
   const view=await permitted('observer',()=>require('./host-binding.cjs').observeOwnership({native,supervisor:guardedSupervisor,epoch}));observed=true;assert(view.framesOffer<epoch&&view.framesAck<epoch,'Native frames have already been offered for this generation');
   const H=require('./host-frames.cjs'),plan=H.plan({...frames,imageCount:view.imageCount,menuControls:view.controls,supervisorRange:guardedSupervisor,menuCommands:view.menuCommands});await permitted('observer',()=>H.verifyControls(native,frames.controls));
   const staging=view.context+view.layout.host_frames_plan,offer=view.context+view.layout.host_frames_offer;framesPublished=true;attempted=true;
   await permitted('frames',async()=>{await native.write(staging,plan);const bytes=Buffer.alloc(4);bytes.writeUInt32LE(epoch);await native.write(offer,bytes);assert(scope.getStore().published,'Native frame offer was not published');},{staging,offer,plan,staged:false,published:false});
   const started=mappingClock();let polls=0;
   for(;;){const b=await permitted('observer',async()=>{const reply=await native.call(0x586fb0,output,view.context,view.layout.status_bytes);assert(reply.result===BigInt(output)&&reply.output.length>=view.layout.status_bytes);return reply.output;});const u=name=>b.readUInt32LE(view.layout[name]);assert(u('magic')===0x44535a49&&u('state')===2&&!u('stop')&&!u('poisoned')&&!u('error'),'Native frame owner stopped');
    if(u('host_rpc_returned')===epoch)throw Object.assign(Error('The console returned before frame publication completed'),{nativeGenerationReturned:true,requestReplayed:false});
    if(u('host_frames_ack')===epoch){if(u('host_frames_error'))throw Object.assign(Error('The console rejected the frame plan'),{framesNotAccepted:true,requestReplayed:false});break;}
    assert(phase==='active'&&lease.active&&!shuttingDown&&mappingClock()-started<mappingTimeoutMs,'Native frame acknowledgement is unconfirmed');await mappingPause(Math.min(100,10*(++polls)));
   }
   return {ticket:epoch,id:frames.id,controls:frames.controls.mapped,nativeOwnershipReleased:false,resourcesReleased:false};
  }catch(error){if(error.nativeRequestSent===false&&!error.resourcesRetained)throw error;if(error.nativeGenerationReturned||!observed||attempted&&!error.framesNotAccepted||error.resourcesRetained||native.connection?.awaitingResponse||native.connection?.inflight)throw await failed(error);throw error;}finally{framesPreparing=false;}});
 }
 function cancelNative(generation,errorCode=0){
  assert(Number.isInteger(errorCode)&&errorCode>=0&&errorCode<=0xffffffff,'Invalid native application error code');
  assert(!shuttingDown&&generation===epoch&&lease?.guarded&&guardedSupervisor&&['sealed','mapping','mapped','active','parked'].includes(phase),'There is no current guarded generation to cancel');if(cancelOperation)return cancelOperation;
  pending++;cancelOperation=permitted('cancel',async()=>{try{const bytes=Buffer.alloc(errorCode?8:4);if(errorCode)bytes.writeUInt32LE(errorCode);bytes.writeUInt32LE(generation,errorCode?4:0);await native.write(cancelOffer-(errorCode?4:0),bytes);assert(scope.getStore().published,'Native cancellation was not published');return {epoch:generation,cancellationRequested:true,errorCode,nativeOwnershipReleased:false,resourcesReleased:false};}catch(error){if(error.nativeRequestSent===false&&!error.resourcesRetained)throw error;throw uncertain(error);}},{published:false,errorCode}).finally(()=>pending--);return cancelOperation;
 }
 function confirmNativeReturn(generation){
  assert(!shuttingDown&&generation===epoch&&lease?.guarded&&guardedSupervisor&&!['open','legacy-shutdown','retained'].includes(phase),'There is no verifiable guarded return');
  return enqueue(async()=>{try{const proof=await permitted('observer',()=>require('./host-binding.cjs').observeOwnership({native,supervisor:guardedSupervisor,epoch,returned:true}));if(proof.returned)retire(generation);return proof;}catch(error){throw uncertain(error);}});
 }
 function observe(fn){assert(typeof fn==='function');if(phase==='open'||phase==='legacy-shutdown')return Promise.reject(reject('Scoped observation is available after the launcher seals dispatch'));return enqueue(()=>permitted('observer',fn));}
 function retire(generation){assert(generation===epoch&&epochStates.has(epoch));epochStates.get(epoch).retired=true;if(!shuttingDown&&phase!=='retained')phase='parked';return inspect();}
 function sealForShutdown(){shuttingDown=true;for(const state of epochStates.values())state.retired=true;phase=used||phase!=='open'&&phase!=='legacy-shutdown'?'retained':'legacy-shutdown';return {retained:phase==='retained',nativeOwnershipReleased:false,...inspect()};}
 function inspect(){return {format:'iz2s.host-dispatch/1',phase,epoch,parentRetentionReady:protectedParent,maintenanceReady:ready,pending,accepted,denied,resourcesRetained:used||phase==='retained',nativeOwnershipReleased:false};}
 const api=Object.freeze({directory,beforeWrite,prepare,begin,claimNativeDisplay,setup,prepareBinding,mapNativeBinding,adoptPrepared,publishNativeFrames,cancelNative,confirmNativeReturn,commit,commitPrepared,run,observe,retire,sealForShutdown,inspect});clients.set(native,api);directories.set(directory,api);return api;
}
module.exports={create,lookup};
