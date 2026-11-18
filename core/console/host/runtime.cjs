const L=require('./layout.cjs');
function validateManifest(value){
 if(value===undefined)return {capabilities:[],endpoints:[]};
 if(!value||value.version!==1||!Array.isArray(value.capabilities)||value.capabilities.some(x=>!['vibration','nfc','udp'].includes(x)))throw Error('Invalid SDK manifest');
 const services=value.services??[];if(!Array.isArray(services)||services.some(s=>s!=='ftp'))throw Error('Invalid SDK background service');
 if(value.ftpPort!==undefined&&(!Number.isInteger(value.ftpPort)||value.ftpPort<1024||value.ftpPort>65535))throw Error('Invalid FTP port');
 const endpoints=value.endpoints??[];if(!Array.isArray(endpoints)||endpoints.length>8)throw Error('SDK supports at most eight UDP endpoints');
 return {capabilities:[...new Set(value.capabilities)],endpoints:endpoints.map(require('./network.cjs').endpoint)};
}
async function create({rpc,mapped,directory,hardware:providedHardware}){
 const sessionRPC=rpc,context=require('./rpc-context.cjs').create(rpc);rpc=context.rpc;
 const hardware=providedHardware??await require('./hardware.cjs').create(rpc);const owners=new Map(),channelOwners=new WeakMap(),failures=new Map(),pending=new Set(),channels=require('../sd-launcher/application-channel.cjs');let closed=false;
 function unsafe(error){const policy=require('../../session/offline-policy.cjs');return policy.requiresRetention(error)||policy.isTransportLoss(error)&&!require('./network.cjs').isReceiveTimeout(error);}
 const firstFailure=()=>failures.values().next().value?.error;
 const sharedPending=guardedOnly=>{for(const boundary of pending)if(boundary.shared&&(!guardedOnly||boundary.owner.channel))return true;return false;};
 const pendingError=()=>Object.assign(Error('An SDK hardware operation has not completed'),{code:'IZ2S_SDK_SHARED_PENDING',resourcesRetained:true,nativeRequestSent:false,requestReplayed:false});
 function rememberFailure(owner,error,contained){
  if(!unsafe(error)||contained&&owner.returned)return;
  const previous=failures.get(owner);if(previous){previous.contained&&=contained;}else failures.set(owner,{error,contained});
 }
 function observeNativeReturn(controls,{channel,proof}={}){
  if(closed)throw Error('SDK runtime closed');
  if(!channels.isReturned(channel,proof))throw Error('SDK recovery requires an attested native application return');
  if(!Number.isSafeInteger(controls?.mapped)||!Number.isSafeInteger(controls?.execution)||controls.execution<1)throw Error('Invalid SDK return controls');
  let retired=false;
  for(const owner of channelOwners.get(channel)||[]){
   if(owner.target!==controls.mapped||owner.execution!==controls.execution)continue;
   owner.returned=true;retired=true;if(owners.get(owner.target)===owner)owners.delete(owner.target);
   if(failures.get(owner)?.contained)failures.delete(owner);
  }
  return retired;
 }
 try{await hardware.startSensors?.(mapped);}catch(error){
  // Creation has not returned ownership to the frame loop yet.
  if(unsafe(error))throw error;
  try{await hardware.close();}catch(cleanup){error.cleanupError=cleanup.message;}
  throw error;
 }
 return {hardware,observeNativeReturn,retireControls(controls){const target=controls?.mapped??mapped,owner=owners.get(target);if(controls?.execution===undefined||owner?.execution===controls.execution)owners.delete(target);},async atBoundary(app=null,controls,{rpc:route=sessionRPC,nativeInput,channel}={}){
  if(closed)throw Error('SDK runtime closed');
  const failure=firstFailure();
  if(failure)throw failure;
  if(sharedPending(true))throw pendingError();
  if(channel!==undefined&&!channels.ownsRPC(channel,route))throw Error('Invalid SDK application channel');
  if(nativeInput!==undefined&&(!nativeInput||route?.generationGuarded!==true||!Number.isInteger(nativeInput.epoch)||nativeInput.epoch<1||nativeInput.epoch>0xffffffff||!Number.isInteger(nativeInput.frame)||nativeInput.frame<0||nativeInput.frame>0xffffffff))throw Error('Invalid native SDK input stamp');
  const target=controls?.mapped??mapped,execution=controls?.execution??0;if(!Number.isSafeInteger(target)||target<4096||target>2**39-8388608||!Number.isSafeInteger(execution)||execution<0)throw Error('Invalid SDK control allocation');
  const base=target+L.OFFSET,identity=(app?.fingerprint??'launcher')+':'+execution;let owner=owners.get(target),fresh=false;
  if(!owner||owner.identity!==identity||owner.rpc!==route||channel&&owner.channel!==channel){owner={identity,target,execution,sequence:0,rpc:route,channel,returned:false};owners.set(target,owner);fresh=true;if(channel){let rows=channelOwners.get(channel);if(!rows)channelOwners.set(channel,rows=new Set());rows.add(owner);}}
  const current=()=>!closed&&owners.get(target)===owner;
  // Only isolated mailbox operations and contained typed services can be
  // reconciled by the native epoch proof. Shared sensor discovery and NFC
  // lifetimes still require their own completion evidence.
  let contained=!!owner.channel&&target!==mapped;
  const boundary={owner,shared:false};pending.add(boundary);
  return context.run(route,current,async()=>{
  async function write(address,bytes){if(!current())return false;await rpc.write(address,bytes);return current();}
  if(fresh&&!await write(base+L.FIELD.request,Buffer.alloc(6144)))return;
  if(fresh&&target!==mapped){const sweep=require('./pad-sweep.cjs');if(!await write(base+sweep.HEADER,sweep.header()))return;}
  contained=false;boundary.shared=true;const state=await hardware.poll();contained=!!owner.channel&&target!==mapped;boundary.shared=false;
  if(!current())return;
  state.writeBigUInt64LE(0n,L.FIELD.nativeAuxTick);state.writeUInt32LE(nativeInput?.epoch??0,L.FIELD.nativeInputEpoch);state.writeUInt32LE(nativeInput?.frame??0,L.FIELD.hostInputFrame);state.writeUInt32LE(0,L.FIELD.nativeInputFrame);
  // The GPU owns the gesture latch across PC observations. Its 32-byte region
  // follows the independent status snapshot and precedes live pad sources.
  const gestureEnd=L.FIELD.menuGesture+32,sample=require('./console-sensors.cjs'),independent=hardware.consoleSensors?.supported;
  if(sample.OFFSET+sample.BYTES>L.FIELD.menuGesture||gestureEnd>L.FIELD.padSources)throw Error('SDK status overlaps the menu gesture');
  if(!await write(base,state.subarray(0,independent?sample.OFFSET:L.FIELD.menuGesture)))return;
  if(!await write(base+gestureEnd,state.subarray(gestureEnd)))return;
  if(independent&&target!==mapped){const snapshot=await rpc.read(mapped+L.OFFSET+sample.OFFSET,sample.BYTES);if(!await write(base+sample.OFFSET,snapshot))return;}
  const request=await rpc.read(base+L.FIELD.request,1056),sequence=request.readUInt32LE();
  if(!current()||!sequence||sequence===owner.sequence)return;owner.sequence=sequence;
  const op=request.readUInt32LE(4),reply=Buffer.alloc(1056);reply.writeUInt32LE(sequence);
  const config=app?validateManifest(app.manifest.sdk):{capabilities:['vibration','nfc'],endpoints:[]};
  const capability=({1:'vibration',2:'nfc',3:'udp',4:'vibration'})[op];let status=L.STATUS.ok,payload=Buffer.alloc(0);
  try{
   if(!capability||!config.capabilities.includes(capability)){status=L.STATUS.unsupported;}
   else if(op===L.OP.vibrate){
    const result=await hardware.pulse({side:request.readUInt32LE(8),milliseconds:request.readUInt32LE(12),amplitude:request.readUInt32LE(16)/1000});payload=result.actual;
   }else if(op===L.OP.haptics){
    const count=request.readUInt32LE(12);if(count<1||count>32){status=L.STATUS.invalid;}else{
     const steps=Array.from({length:count},(_,i)=>{const o=32+i*20;return {milliseconds:request.readUInt32LE(o),lowAmplitude:request.readFloatLE(o+4),lowFrequency:request.readFloatLE(o+8),highAmplitude:request.readFloatLE(o+12),highFrequency:request.readFloatLE(o+16)};});
     payload=(await hardware.pattern({side:request.readUInt32LE(8),steps})).actual;
    }
   }else if(op===L.OP.nfc){
    contained=false;boundary.shared=true;
    owner.nfc??=await require('./nfc.cjs').create(rpc);const devices=await owner.nfc.scan({milliseconds:500});
    const tag=devices.find(d=>d.tag);payload=Buffer.alloc(16+(tag?88:0));payload.writeUInt32LE(devices.length);payload.writeUInt32LE(tag?1:0,4);payload.writeUInt32LE(devices[0]?.tagResult??0xffffffff,8);payload.writeUInt32LE(devices[0]?.start??0xffffffff,12);tag?.tag.copy(payload,16);
   }else if(op===L.OP.udp){
    const index=request.readUInt32LE(8),length=request.readUInt32LE(12);if(index>=config.endpoints.length||length<1||length>1024){status=L.STATUS.invalid;}
    else{owner.network??=await require('./network.cjs').create(rpc,directory);payload=await owner.network.exchange(config.endpoints[index],request.subarray(32,32+length));}
   }
  }catch(error){
   // No mailbox reply or cleanup call may follow unknown native completion.
   const receiveTimeout=require('./network.cjs').isReceiveTimeout(error);
   if(unsafe(error))throw error;
   if(['IZ2S_RPC_GENERATION_REVOKED','IZ2S_APP_RPC_REVOKED','IZ2S_SDK_RPC_REVOKED','IZ2S_HOST_DISPATCH_REVOKED'].includes(error.code))throw error;
   if(!current())return;
   status=receiveTimeout?L.STATUS.timeout:L.STATUS.native;payload=Buffer.from(error.message).subarray(0,1024);
  }
  reply.writeUInt32LE(status,4);reply.writeUInt32LE(payload.length,8);payload.copy(reply,32);await write(base+L.FIELD.response,reply);
  }).catch(error=>{rememberFailure(owner,error,contained);throw error;}).finally(()=>pending.delete(boundary));
 },async close(){closed=true;owners.clear();const failure=firstFailure();if(failure)throw failure;if(pending.size)throw pendingError();await hardware.close();}};
}
module.exports={create,validateManifest};
