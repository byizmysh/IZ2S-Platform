'use strict';
// Live binding proof for the original host. JSON descriptors locate memory;
// only observations through that exact NativeClient establish a proof.
const assert=require('node:assert/strict'),fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const R=require('./host-rpc.cjs'),LIMIT=2**39,SHARED=8388608,prepared=new WeakMap(),proofs=new WeakMap();
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function span(base,bytes,alignment=4){assert(Number.isSafeInteger(base)&&base>=4096&&base%alignment===0&&Number.isSafeInteger(bytes)&&bytes>0&&bytes<=LIMIT-base,'Invalid native binding span');}
function disjoint(a,n,b,m){assert(a+n<=b||b+m<=a,'Native binding allocations overlap');}
function q(bytes,offset){const n=bytes.readBigUInt64LE(offset);assert(n<=BigInt(Number.MAX_SAFE_INTEGER),'Native binding address is not exact');return Number(n);}
async function mapping(native,base,bytes,permissions,regions){
 for(let at=base;at<base+bytes;){let region=regions.find(row=>Number(row.address)<=at&&Number(row.address)+Number(row.size)>at);if(!region){region=await native.query(at);regions.push(region);}const begin=Number(region.address),size=Number(region.size),end=begin+size;
  assert(Number.isSafeInteger(begin)&&Number.isSafeInteger(size)&&size>0&&Number.isSafeInteger(end)&&begin<=at&&end>at&&end<=LIMIT&&region.permissions===permissions&&!region.attributes&&!region.ipcReferences&&!region.deviceReferences&&!([0,1,16].includes(region.type)),'Native binding mapping differs');at=end;
 }
}
async function mappedRead(native,base,bytes){
 const parts=[];for(let at=0;at<bytes;at+=32768){const n=Math.min(32768,bytes-at),reply=await native.call(0x586fb0,native.output,base+at,n);assert(reply.result===BigInt(native.output)&&Buffer.isBuffer(reply.output)&&reply.output.length>=n,'Short native binding observation');parts.push(Buffer.from(reply.output.subarray(0,n)));}return Buffer.concat(parts);
}
async function mappedMany(native,spans){
 assert(spans.reduce((n,row)=>n+row.length,0)<=32768);const out=[];
 for(let at=0;at<spans.length;at+=2){const pair=spans.slice(at,at+2);let offset=0;const calls=pair.map(row=>{const call={offset:0x586fb0,args:[native.output+offset,row.address,row.length,0]};offset+=row.length;return call;}),reply=await native.exchange(calls);assert(Buffer.isBuffer(reply.output)&&reply.output.length>=offset,'Short native binding observation');offset=0;
  for(const [i,row]of pair.entries()){assert(reply.results?.[i]===BigInt(native.output+offset),'Native binding copy failed');out.push(Buffer.from(reply.output.subarray(offset,offset+row.length)));offset+=row.length;}
 }return out;
}
async function verifyDisplayLease({native,supervisor,epoch}){
 const m=require('./check.cjs').verifyRuntime(),L=m.layout,A=m.hostRpcABI,O=A.ownerLayout,s={...supervisor},context=s.base+0x1000,owner=s.base+A.ownerOffset,length=owner+A.ownerBytes-context;
 assert(s.bootstrapSha256===m.sha256&&Number.isInteger(epoch)&&epoch>0&&epoch<=0xffffffff,'Invalid native display proof');span(s.base,s.bytes,4096);span(s.entry,m.bytes,4096);disjoint(s.entry,m.bytes,s.base,s.bytes);assert(length<=32768&&owner+A.ownerBytes<=s.base+s.bytes);
 const regions=[];await mapping(native,s.entry,m.bytes,5,regions);await mapping(native,context,length,3,regions);const image=await fs.readFile(path.join(__dirname,'native/build/launcher.bin'));assert((await mappedRead(native,s.entry,m.bytes)).equals(image),'Live display supervisor image differs');
 const block=await mappedRead(native,context,length),u=name=>block.readUInt32LE(L[name]),n=name=>q(block,L[name]),g=block.subarray(owner-context),word=g.readBigUInt64LE(O.word);
 assert(u('magic')===0x44535a49&&u('version')===1&&u('state')===2&&!u('stop')&&!u('error')&&!u('poisoned')&&!u('active')&&u('host_mode')===1&&n('sdk')===native.addresses.sdk&&n('host_rpc_scratch')===native.addresses.scratch&&n('supervisor_base')===s.base&&n('supervisor_bytes')===s.bytes&&n('host_rpc_owner')===owner,'Native display identity differs');
 assert(u('host_display_request')===epoch&&u('host_display_ready')===epoch&&u('host_display_release')!==epoch&&(word&0xffffffffn)===0x80000000n&&Number(word>>32n)<epoch&&!g.readUInt32LE(O.opaque)&&!g.readUInt32LE(O.graphics_owner)&&!g.readUInt32LE(O.graphics_fault),'Native display lease is not granted and idle');
 return true;
}
async function prepareBinding({native,displayLease,supervisor,thunkBase,thunkBacking,targets,frameMode=1,sdkServices=false},mapped){
 try{
  assert(native?.addresses?.sdk&&native.addresses?.scratch&&native.output===native.addresses.scratch+0x4040&&['query','write','call','exchange'].every(name=>typeof native[name]==='function'),'Original native client is required');
  assert(displayLease?.active&&Number.isInteger(displayLease.token)&&displayLease.token>0&&displayLease.token<=0xffffffff,'A current display lease is required');
  const epoch=displayLease.token,a={...native.addresses},m=require('./check.cjs').verifyRuntime(),L={...m.layout},A={...m.hostRpcABI},O={...A.ownerLayout},s={base:supervisor?.base,bytes:supervisor?.bytes,entry:supervisor?.entry};
  assert(typeof sdkServices==='boolean');const SDK=require('../host/sdk-profile.cjs');if(sdkServices)assert.deepEqual(m.sdkServices,SDK.definitions,'The SDK service profile differs from the native build');const sdkProfile=mapped?.sdkProfile||(sdkServices?await SDK.verify(native,{epoch}):null);
  assert(supervisor?.bootstrapSha256===m.sha256&&A.planBytes===R.PLAN_BYTES&&A.bindingStride===R.BINDING_BYTES&&A.bindingOffset===40&&O.bytes===A.ownerBytes&&O.bindings===A.bindingOffset&&A.ownerBytes<=4096,'Supervisor build/layout differs');
  span(a.sdk,4);span(a.scratch,0x80000,16);span(s.base,s.bytes,4096);span(s.entry,m.bytes,4096);span(thunkBase,160,4096);assert(s.bytes>=0x10000&&s.bytes%4096===0);
  disjoint(s.entry,m.bytes,s.base,s.bytes);disjoint(thunkBase,160,s.base,s.bytes);disjoint(thunkBase,160,s.entry,m.bytes);disjoint(s.base,s.bytes,a.scratch,0x80000);disjoint(thunkBase,160,a.scratch,0x80000);disjoint(s.entry,m.bytes,a.scratch,0x80000);
  const context=s.base+0x1000,owner=s.base+A.ownerOffset,staging=s.base+A.stagingOffset,offer=context+L.host_rpc_offer,backing=thunkBacking,blockBytes=owner+A.ownerBytes-context;span(backing,4096,4096);
  assert(context+L.bytes<=s.base+0x4000&&owner+4096<=s.base+s.bytes&&staging+R.PLAN_BYTES<=s.base+s.bytes,'Supervisor configuration escapes its allocation');
  const image=await fs.readFile(path.join(__dirname,'native/build/launcher.bin'));assert(image.length===m.bytes&&sha(image)===m.sha256,'Supervisor image differs');
  const entries=Object.freeze({owner,packet:s.entry+m.exports.iz_host_rpc_packet,receipt:s.entry+m.exports.iz_host_rpc_receipt,fullWrite:thunkBase,fullReceipt:thunkBase+80});
  const thunk=R.packetThunks({owner,epoch,fullWrite:s.entry+m.exports.iz_host_rpc_full_write,fullReceipt:s.entry+m.exports.iz_host_rpc_full_receipt}).bytes;
  assert(blockBytes+thunk.length*2<=32768,'Retained binding snapshot exceeds SDK output');
  assert(Array.isArray(targets)&&targets.length<=R.MAX,'Invalid native binding targets');
  const copiedTargets=targets.map(row=>Object.freeze({target:row.target,effect:row.effect??0}));for(const row of copiedTargets){span(row.target,4);disjoint(row.target,4,s.entry,m.bytes);disjoint(row.target,4,thunkBase,thunk.length);}
  let expectedPlan,boundedTargets,platform,graphicsMemoryProperties,codeIndex,poolBase,poolBytes,attempted=false,published=false,verified=false;
  async function observe(open){
   assert(displayLease.active&&displayLease.token===epoch&&native.addresses.sdk===a.sdk&&native.addresses.scratch===a.scratch&&native.output===a.scratch+0x4040,'Binding client or display lease changed');
   // Mapping observations are reused only within this serialized snapshot.
   // The original client's raw read helpers would repeat the same queries.
   const regions=[];await mapping(native,s.entry,image.length,5,regions);await mapping(native,thunkBase,thunk.length,5,regions);await mapping(native,context,blockBytes,3,regions);if(!mapped)await mapping(native,backing,4096,3,regions);else await mapping(native,s.base+A.historyOffset,4096,3,regions);
   assert((await mappedRead(native,s.entry,image.length)).equals(image),'Live supervisor image differs');
   const [block,t,source]=await mappedMany(native,[{address:context,length:blockBytes},{address:thunkBase,length:thunk.length},{address:mapped?s.base+A.historyOffset+mapped.codeIndex*16:backing,length:mapped?16:thunk.length}]);assert(t.equals(thunk),'Live generation thunks differ');if(!mapped)assert(source.equals(thunk),'Retained generation thunk backing differs');
   const b=block.subarray(0,L.status_bytes),g=block.subarray(owner-context,owner-context+A.ownerBytes),p=block.subarray(staging-context,staging-context+R.PLAN_BYTES);
   const u=name=>b.readUInt32LE(L[name]),n=name=>q(b,L[name]),gu=name=>g.readUInt32LE(O[name]),gn=name=>q(g,O[name]);
   assert(u('magic')===0x44535a49&&u('version')===1&&u('state')===2&&!u('stop')&&!u('error')&&!u('poisoned')&&!u('active')&&!u('probe')&&!u('ram_only')&&u('host_mode')===1,'Supervisor is not a healthy host display owner');
   assert(n('sdk')===a.sdk&&n('host_rpc_scratch')===a.scratch&&n('supervisor_base')===s.base&&n('supervisor_bytes')===s.bytes&&n('host_rpc_owner')===owner&&n('host_rpc_staging')===staging,'Supervisor address binding differs');
   assert(u('host_display_request')===epoch&&u('host_display_ready')===epoch&&u('host_display_release')!==epoch&&u('host_display_rejected')!==epoch,'Exact native display lease is unavailable');
   assert(gu('magic')===0x495a5247&&gn('sdk_scratch')===a.scratch&&gn('protected_base')===s.base&&gn('protected_bytes')===s.bytes&&!gu('opaque')&&!gu('cpu_jobs')&&!gu('graphics_fault')&&!gu('graphics_waiting'),'Private RPC owner is unverified or retained');
   const table=Array.from({length:m.functions.length},(_,i)=>q(b,L.functions+i*8));table.forEach(fn=>span(fn,4));
   const properties=b.subarray(L.graphics_memory_properties,L.graphics_memory_properties+520),propertyTypes=properties.readUInt32LE();assert(propertyTypes<=32,'Supervisor memory property count differs');
   if(graphicsMemoryProperties===undefined)graphicsMemoryProperties=properties.toString('base64');else assert(properties.toString('base64')===graphicsMemoryProperties,'Supervisor memory properties changed');
   if(!platform){platform=Buffer.from(b.subarray(L.functions,L.functions+m.functions.length*8));boundedTargets=[...table.filter((fn,i)=>m.functions[i]!=='SEND'),...(sdkProfile?.targets.map(row=>row.target)||[])];expectedPlan=R.plan(copiedTargets,{boundedTargets,...(!mapped?{fullWrite:{entry:entries.fullWrite,receipt:entries.fullReceipt},backing:{base:backing,bytes:4096}}:{}),frameMode});codeIndex=mapped?mapped.codeIndex:gu('code_used');poolBase=n('host_rpc_code_base');poolBytes=n('host_rpc_code_bytes');}
   else assert(b.subarray(L.functions,L.functions+m.functions.length*8).equals(platform),'Supervisor platform function table changed');
   assert(p.equals(expectedPlan),'Staged native RPC plan differs');
   if(mapped){
    assert(frameMode===1&&platform.equals(mapped.platform)&&p.equals(mapped.seed)&&poolBase===mapped.poolBase&&poolBytes===mapped.poolBytes,'Native mapper preparation changed');
    assert(u('host_rpc_map_offer')===epoch&&u('host_rpc_map_ack')===epoch&&!u('host_rpc_map_error')&&u('host_rpc_map_state')===3&&u('host_rpc_map_index')===codeIndex&&n('host_rpc_map_rx')===thunkBase&&n('host_rpc_map_source')===backing,'Native mapping acknowledgement differs');
    assert(gu('map_epoch')===epoch&&gu('map_state')===3&&!gu('map_error')&&gu('map_index')===codeIndex&&gn('map_rx')===thunkBase&&gn('map_source')===backing&&((g.readUInt32LE(O.code_states+Math.floor(codeIndex/16)*4)>>>((codeIndex%16)*2))&3)===3,'Private native mapping proof differs');
    assert(source.readBigUInt64LE()===BigInt(thunkBase)&&source.readBigUInt64LE(8)===BigInt(epoch),'Private native mapping history differs');
   }
   const controls=n('controls'),snapshot=n('snapshot'),parent=n('parent_snapshot'),dataBegin=s.base+0x10000+Math.ceil(image.length/4096)*4096+65536;span(controls,SHARED,4096);assert(n('controls_bytes')===SHARED&&snapshot>=dataBegin&&snapshot+SHARED<=s.base+s.bytes&&parent>=dataBegin&&parent+SHARED<=s.base+s.bytes,'Resident menu storage differs');disjoint(controls,SHARED,s.base,s.bytes);disjoint(controls,SHARED,s.entry,image.length);disjoint(controls,SHARED,thunkBase,thunk.length);disjoint(snapshot,SHARED,parent,SHARED);
   if(frameMode)assert(b.readBigUInt64LE(L.queue)!==0n,'Native graphics queue is unavailable');
   assert(poolBytes===R.CODE_POOL_BYTES&&poolBase%4096===0&&poolBase>=n('stack')+n('stack_bytes')&&n('stack')>=snapshot+SHARED&&n('stack')>=parent+SHARED&&n('stack_bytes')>0&&poolBase>=s.base&&poolBase+poolBytes<=s.base+s.bytes&&n('host_rpc_code_base')===poolBase&&n('host_rpc_code_bytes')===poolBytes&&backing===poolBase+codeIndex*4096&&backing+4096<=poolBase+poolBytes,'Retained thunk source slot differs');
   assert(gu('code_used')===codeIndex+(open?1:0)&&(!codeIndex&&!open?(!gn('code_base')&&!gn('code_bytes')&&!gn('code_history')):(gn('code_base')===poolBase&&gn('code_bytes')===poolBytes&&gn('code_history')===s.base+A.historyOffset)),'Private source slot history differs');
   const word=g.readBigUInt64LE(O.word),graphicsOwner=gu('graphics_owner');
   if(!open){assert((word&0xffffffffn)===0x80000000n&&Number(word>>32n)<epoch&&!graphicsOwner&&u('host_rpc_ack')<epoch&&u('host_rpc_offer')!==epoch&&(!codeIndex?word===0x80000000n:true),'A previous native generation is not closed');}
   else{
    assert(word===(BigInt(epoch)<<32n)&&u('host_rpc_offer')===epoch&&u('host_rpc_ack')===epoch&&!u('host_rpc_error')&&u('host_rpc_epoch')===epoch&&u('host_rpc_count')===copiedTargets.length&&n('host_rpc_bindings')===owner+A.bindingOffset,'Native generation offer was not accepted');
    assert(gu('count')===copiedTargets.length&&gn('full_write_thunk')===entries.fullWrite&&gn('full_receipt_thunk')===entries.fullReceipt&&gu('graphics_mode')===frameMode&&(graphicsOwner===0||frameMode===1&&graphicsOwner===2),'Private generation configuration differs');
    assert(gn('service_sdk')===a.sdk&&gu('service_tick_hz')===u('tick_hz')&&g.readBigUInt64LE(O.graphics_queue)===b.readBigUInt64LE(L.queue)&&gn('graphics_sleep')===table[m.functions.indexOf('SLEEP')]&&gn('graphics_idle')===table[m.functions.indexOf('QUEUE_IDLE')],'Private graphics binding differs');
    assert(gn('service_send')===table[m.functions.indexOf('SEND')]&&gn('service_close')===table[m.functions.indexOf('CLOSE')]&&gu('service_filesystem')===u('filesystem')&&!gu('service_reserved'),'Private SD filesystem binding differs');
    for(let i=0;i<copiedTargets.length;i++){const at=A.bindingOffset+i*A.bindingStride,row=copiedTargets[i];assert(q(g,at)===owner&&q(g,at+8)===row.target&&g.readUInt32LE(at+16)===epoch&&g.readUInt32LE(at+20)===row.effect,'Private target/epoch binding differs');}
   }
  }
  await observe(Boolean(mapped));
  const identity={native,epoch,entries,offer,mode:mapped?'mapped':'manual',imageSha256:m.sha256,thunkSha256:sha(thunk),claimed:false};
  const binding=Object.freeze({entries,offer,graphicsMemoryProperties,sdkProfile,targets:Object.freeze(copiedTargets),async publish(){assert(!mapped,'A native-created mapping cannot be republished');assert(!attempted,'A native binding publication cannot be replayed');attempted=true;await observe(false);const bytes=Buffer.alloc(4);bytes.writeUInt32LE(epoch);await native.write(offer,bytes);published=true;},async verify(){assert((mapped||published)&&!verified,'A fresh native binding publication is required');await observe(true);verified=true;const proof=Object.freeze({owner,epoch,offerAccepted:true,nativeEpochOpen:true,imageSha256:identity.imageSha256,thunkSha256:identity.thunkSha256,frameMode,nativeOwnershipReleased:false});proofs.set(proof,{...identity,consumed:false});return proof;}});
  prepared.set(binding,identity);return binding;
 }catch(error){throw Object.assign(error,{code:'IZ2S_NATIVE_BINDING_UNVERIFIED',resourcesRetained:true,requestReplayed:false});}
}
function prepare(options){return prepareBinding(options,null);}
async function prepareClaim({native,supervisor,minimumEpoch}){
 const m=require('./check.cjs').verifyRuntime(),L=m.layout,A=m.hostRpcABI,O=A.ownerLayout,s={...supervisor},context=s.base+0x1000,owner=s.base+A.ownerOffset,length=owner+A.ownerBytes-context;
 assert(s.bootstrapSha256===m.sha256&&Number.isInteger(minimumEpoch)&&minimumEpoch>0&&minimumEpoch<=0xffffffff,'Invalid guarded display claim');span(s.base,s.bytes,4096);span(s.entry,m.bytes,4096);disjoint(s.entry,m.bytes,s.base,s.bytes);assert(length<=32768&&owner+A.ownerBytes<=s.base+s.bytes);
 const regions=[];await mapping(native,s.entry,m.bytes,5,regions);await mapping(native,context,length,3,regions);assert((await mappedRead(native,s.entry,m.bytes)).equals(await fs.readFile(path.join(__dirname,'native/build/launcher.bin'))),'Live display supervisor image differs');
 const block=await mappedRead(native,context,length),u=name=>block.readUInt32LE(L[name]),n=name=>q(block,L[name]),g=block.subarray(owner-context),gu=name=>g.readUInt32LE(O[name]),gn=name=>q(g,O[name]),word=g.readBigUInt64LE(O.word);
 assert(u('magic')===0x44535a49&&u('version')===1&&u('state')===2&&!u('stop')&&!u('error')&&!u('poisoned')&&u('host_mode')===1&&n('sdk')===native.addresses.sdk&&n('host_rpc_scratch')===native.addresses.scratch&&n('supervisor_base')===s.base&&n('supervisor_bytes')===s.bytes&&n('host_rpc_owner')===owner&&gu('magic')===0x495a5247&&gn('sdk_scratch')===native.addresses.scratch&&gn('protected_base')===s.base&&gn('protected_bytes')===s.bytes,'Native claim identity differs');
 if(u('active')||u('host_display_ready'))throw Object.assign(Error('The console already owns an active application or display loan'),{menuBusy:true,nativeClaimRejected:true,nativeRequestSent:false});
 assert((word&0xffffffffn)===0x80000000n&&!gu('opaque')&&!gu('graphics_owner')&&!gu('graphics_fault')&&!gu('graphics_waiting')&&gu('map_state')!==5,'Previous native ownership is not closed');
 let epoch;const request=u('host_display_request'),release=u('host_display_release'),previous=Number(word>>32n);
 if(request!==release){
  if(!(request&1)||request<minimumEpoch||request<=previous||request<=gu('claim_seen')||!((u('host_launch')>0)!==(u('host_remove')>0)))throw Object.assign(Error('A different native menu action owns the display request'),{menuBusy:true,nativeClaimRejected:true,nativeRequestSent:false});epoch=request;
 }else{epoch=Math.floor(Math.max(minimumEpoch-1,previous,gu('claim_seen'),...['host_display_request','host_display_release','host_display_claim','host_display_observed','host_display_rejected','host_rpc_claim_ack'].map(u))/2)*2+2;assert(epoch<=0xfffffffe,'Guarded display generation exhausted');}
 const offer=context+L.host_rpc_claim;let attempted=false;
 return Object.freeze({epoch,offer,cancel:context+L.host_frames_cancel,async publish(){assert(!attempted,'A guarded display claim cannot be replayed');attempted=true;const bytes=Buffer.alloc(4);bytes.writeUInt32LE(epoch);await native.write(offer,bytes);},async status(){
  assert(attempted);const b=await mappedRead(native,context,L.status_bytes),w=name=>b.readUInt32LE(L[name]);assert(w('magic')===0x44535a49&&w('version')===1&&w('state')===2&&!w('stop')&&!w('poisoned')&&!w('error'),'Native display claim stopped');
  if(w('host_rpc_returned')===epoch||w('host_rpc_map_ack')===epoch&&w('host_rpc_map_state')===6&&w('host_display_release')===epoch)return {done:true,cancelled:true};
  if(w('host_display_rejected')===epoch||w('host_display_observed')>epoch||w('host_display_request')>epoch||w('host_rpc_claim_ack')>epoch)return {done:true,rejected:true};
  if(w('host_rpc_claim_ack')===epoch&&w('host_display_request')===epoch&&w('host_display_ready')===epoch&&w('host_display_release')!==epoch)return {done:true,granted:true};return {done:false};
 },async verify(){await verifyDisplayLease({native,supervisor:s,epoch});const b=await mappedRead(native,context,length),g=b.subarray(owner-context);assert(b.readUInt32LE(L.host_rpc_claim)===epoch&&b.readUInt32LE(L.host_rpc_claim_ack)===epoch&&g.readUInt32LE(O.claim_epoch)===epoch&&g.readUInt32LE(O.claim_seen)===epoch,'Guarded display claim was not accepted by the native owner');}});
}
async function observeOwnership({native,supervisor,epoch,returned=false}){
 const m=require('./check.cjs').verifyRuntime(),L=m.layout,A=m.hostRpcABI,O=A.ownerLayout,s={...supervisor},context=s.base+0x1000,owner=s.base+A.ownerOffset,length=owner+A.ownerBytes-context;
 assert(s.bootstrapSha256===m.sha256&&Number.isInteger(epoch)&&epoch>0&&epoch<=0xffffffff,'Invalid native ownership observation');span(s.base,s.bytes,4096);span(s.entry,m.bytes,4096);disjoint(s.entry,m.bytes,s.base,s.bytes);assert(length<=32768&&owner+A.ownerBytes<=s.base+s.bytes);
 const regions=[];await mapping(native,s.entry,m.bytes,5,regions);await mapping(native,context,length,3,regions);assert((await mappedRead(native,s.entry,m.bytes)).equals(await fs.readFile(path.join(__dirname,'native/build/launcher.bin'))),'Live ownership supervisor image differs');
 const block=await mappedRead(native,context,length),u=name=>block.readUInt32LE(L[name]),n=name=>q(block,L[name]),g=block.subarray(owner-context),gu=name=>g.readUInt32LE(O[name]),gn=name=>q(g,O[name]),word=g.readBigUInt64LE(O.word);
 assert(u('magic')===0x44535a49&&u('version')===1&&u('state')===2&&!u('stop')&&!u('error')&&!u('poisoned')&&u('host_mode')===1&&n('sdk')===native.addresses.sdk&&n('host_rpc_scratch')===native.addresses.scratch&&n('supervisor_base')===s.base&&n('supervisor_bytes')===s.bytes&&n('host_rpc_owner')===owner&&gu('magic')===0x495a5247&&gn('sdk_scratch')===native.addresses.scratch&&gn('protected_base')===s.base&&gn('protected_bytes')===s.bytes&&gu('claim_epoch')===epoch&&u('host_rpc_claim_ack')===epoch,'Native ownership identity differs');
 if(returned){
  const complete=word===((BigInt(epoch)<<32n)|0x80000000n)&&!gu('opaque')&&!gu('cpu_jobs')&&!gu('graphics_owner')&&!gu('graphics_fault')&&!gu('graphics_waiting')&&u('host_rpc_returned')===epoch&&u('host_display_ready')===0&&u('host_display_release')===epoch&&!u('active')&&!u('host_frames_active')&&!u('host_frames_paused');
  return Object.freeze({returned:complete,epoch,graphicsIdle:complete,nativeCallsDrained:complete,nativeOwnershipReleased:false,resourcesReleased:false});
 }
 assert(word===(BigInt(epoch)<<32n)&&!gu('opaque')&&!gu('graphics_fault')&&gu('graphics_mode')===1&&u('host_display_request')===epoch&&u('host_display_ready')===epoch&&u('host_display_release')!==epoch&&!u('active'),'The guarded native generation is not open');
 const imageCount=n('image_count'),controls=n('controls');assert(imageCount>=1&&imageCount<=8&&n('controls_bytes')===SHARED);span(controls,SHARED,4096);
 return Object.freeze({epoch,context,layout:L,imageCount,controls,menuCommands:Array.from({length:imageCount},(_,i)=>q(block,L.menu_commands+i*8)),framesOffer:u('host_frames_offer'),framesAck:u('host_frames_ack'),nativeOwnershipReleased:false});
}
async function prepareMapping({native,displayLease,supervisor,targets,frameMode=1,sdkServices=false}){
 assert(frameMode===1&&displayLease?.active,'Console mapping requires coordinated native frames');
 const epoch=displayLease.token,m=require('./check.cjs').verifyRuntime(),L=m.layout,A=m.hostRpcABI,O=A.ownerLayout,s={...supervisor},context=s.base+0x1000,owner=s.base+A.ownerOffset,staging=s.base+A.stagingOffset,offer=context+L.host_rpc_map_offer;
 assert(typeof sdkServices==='boolean');const SDK=require('../host/sdk-profile.cjs');if(sdkServices)assert.deepEqual(m.sdkServices,SDK.definitions,'The SDK service profile differs from the native build');
 await verifyDisplayLease({native,supervisor:s,epoch});
 const block=await mappedRead(native,context,owner+A.ownerBytes-context),g=block.subarray(owner-context),u=name=>block.readUInt32LE(L[name]),n=name=>q(block,L[name]),gu=name=>g.readUInt32LE(O[name]),gn=name=>q(g,O[name]);
 assert(displayLease.active&&displayLease.token===epoch&&u('state')===2&&!u('stop')&&!u('error')&&!u('poisoned')&&u('host_display_request')===epoch&&u('host_display_ready')===epoch&&u('host_display_release')!==epoch&&g.readBigUInt64LE(O.word)%0x100000000n===0x80000000n&&Number(g.readBigUInt64LE(O.word)>>32n)<epoch&&!gu('opaque')&&!gu('graphics_owner')&&!gu('graphics_fault'),'Native mapping lease changed');
 assert(n('host_rpc_staging')===staging&&u('host_rpc_map_offer')<epoch&&u('host_rpc_map_ack')<epoch&&gu('map_state')!==5&&n('pid')&&n('ro_pid')&&u('ro_handle'),'Native mapper is unavailable');
 const sdkProfile=sdkServices?await SDK.verify(native,{epoch}):null,platform=Buffer.from(block.subarray(L.functions,L.functions+m.functions.length*8)),table=m.functions.map((_,i)=>q(platform,i*8)),bounded=[...table.filter((_,i)=>m.functions[i]!=='SEND'),...(sdkProfile?.targets.map(row=>row.target)||[])],copied=(targets??Array.from(new Set(bounded),target=>({target,effect:0}))).map(t=>Object.freeze({target:t.target,effect:t.effect??0})),seed=R.plan(copied,{boundedTargets:bounded,frameMode});
 const codeIndex=gu('code_used'),poolBase=n('host_rpc_code_base'),poolBytes=n('host_rpc_code_bytes');
 assert(poolBytes===R.CODE_POOL_BYTES&&poolBase%4096===0&&codeIndex<poolBytes/4096&&poolBase>=n('stack')+n('stack_bytes')&&poolBase>=s.base&&poolBase+poolBytes<=s.base+s.bytes&&(!codeIndex||gn('code_base')===poolBase&&gn('code_bytes')===poolBytes&&gn('code_history')===s.base+A.historyOffset),'Native mapper source pool differs');
 let published=false,attempted=false,bound=false;
 return Object.freeze({staging,offer,get seed(){return Buffer.from(seed);},async publish(){assert(!attempted&&displayLease.active&&displayLease.token===epoch,'Native map publication cannot be replayed');attempted=true;await native.write(staging,seed);const bytes=Buffer.alloc(4);bytes.writeUInt32LE(epoch);await native.write(offer,bytes);published=true;},async status(){assert(attempted);const b=await mappedRead(native,context+L.host_rpc_map_ack,4);return b.readUInt32LE();},async binding(){assert(published&&!bound,'A fresh native mapping publication is required');bound=true;const b=await mappedRead(native,context,L.status_bytes);assert(b.readUInt32LE(L.host_rpc_map_ack)===epoch,'Native mapping acknowledgement differs');if(b.readUInt32LE(L.host_rpc_map_error)||b.readUInt32LE(L.host_rpc_map_state)!==3)throw Object.assign(Error('Native mapping was not accepted'),{nativeMappingRejected:true,nativeMappingError:b.readUInt32LE(L.host_rpc_map_error),requestReplayed:false});return prepareBinding({native,displayLease,supervisor:s,targets:copied,frameMode,sdkServices,thunkBase:q(b,L.host_rpc_map_rx),thunkBacking:q(b,L.host_rpc_map_source)},{codeIndex,poolBase,poolBytes,platform,seed,sdkProfile});}});
}
function claim(binding,{native,epoch,mode='manual'}){const identity=prepared.get(binding);assert(identity&&identity.native===native&&identity.epoch===epoch&&identity.mode===mode&&!identity.claimed,'A fresh live binding from this original host is required');identity.claimed=true;return binding;}
function consume(proof,{native,epoch,entries,offer}){const identity=proofs.get(proof);if(!identity||identity.consumed||identity.native!==native||identity.epoch!==epoch||identity.offer!==offer||Object.keys(identity.entries).some(name=>identity.entries[name]!==entries[name]))return false;identity.consumed=true;return true;}
module.exports={prepare,prepareClaim,prepareMapping,observeOwnership,claim,consume,verifyDisplayLease};
