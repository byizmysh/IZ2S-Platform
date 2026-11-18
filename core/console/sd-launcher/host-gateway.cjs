'use strict';
// A separate loopback channel in the original host. Loading this module sends
// no SDK command; guarded activation still needs the native binding proof.
const assert=require('node:assert/strict'),http=require('node:http'),crypto=require('node:crypto'),fs=require('node:fs/promises'),path=require('node:path');
const {invoke}=require('./gateway-rpc.cjs');
const FILE='native-app-gateway-private.json',FORMAT='iz2s.native-app-gateway/1',secret=()=>crypto.randomBytes(32).toString('hex');
function equal(a,b){return typeof a==='string'&&a.length===b.length&&crypto.timingSafeEqual(Buffer.from(a),Buffer.from(b));}
function refusal(message='This application gateway generation is retired'){return Object.assign(Error(message),{code:'IZ2S_RPC_GENERATION_REVOKED',nativeRequestSent:false,generationRevoked:true,requestReplayed:false});}
function encode(value){return JSON.stringify(value,(_,n)=>typeof n==='bigint'?n.toString():n);}
async function serve({native,dispatch,directory,verifyExtended}){
 assert(native&&dispatch&&dispatch.inspect().parentRetentionReady,'Gateway requires the original protected host');directory=path.resolve(directory);
 const instance=crypto.randomBytes(16).toString('hex'),controllerToken=secret(),file=path.join(directory,FILE),participants=new Map();let sequence=0,cancelSequence=0,current=null,busy=false,closed=false,pending=0,descriptor,closing;
 function inspect(){return {available:true,closed,pending,phase:dispatch.inspect().phase,epoch:dispatch.inspect().epoch,nativeOwnershipReleased:false};}
 function retire(){if(!current)return;current.retired=true;current.api?.revoke();if(current.epoch===dispatch.inspect().epoch&&['active','sealed','opening','claiming','mapping','mapped','adopting'].includes(dispatch.inspect().phase))dispatch.retire(current.epoch);}
 function retained(error){retire();dispatch.sealForShutdown();return Object.assign(error,{resourcesRetained:true,requestReplayed:false});}
 function capability(){assert(current?.api&&!current.retired&&dispatch.inspect().phase==='active');assert(participants.size<64,'Too many gateway participants');const token=secret();participants.set(token,{generation:current,sequence:0});return {format:FORMAT,instance,epoch:current.epoch,token,url:descriptor.url,a:{...native.addresses},output:native.output,nativeArgumentLimit:current.api.nativeArgumentLimit,graphicsMemoryProperties:current.graphicsMemoryProperties,serviceProfiles:current.serviceProfiles};}
 async function commit(row,nativeMapping=false){
  const generation=current;assert(!row.wideCalls||typeof verifyExtended==='function','A verified SDK worker is required for extended calls');
  const extendedProof=row.wideCalls?await dispatch.observe(()=>verifyExtended({native,directory})):null;
  const binding=await (nativeMapping?dispatch.mapNativeBinding(row.binding):dispatch.prepareBinding(row.binding)),m=require('./check.cjs').verifyRuntime(),image=await fs.readFile(path.join(__dirname,'native/build/launcher.bin')),R=require('./host-rpc.cjs'),s=row.binding.supervisor,thunk=R.packetThunks({owner:binding.entries.owner,epoch:current.epoch,fullWrite:s.entry+m.exports.iz_host_rpc_full_write,fullReceipt:s.entry+m.exports.iz_host_rpc_full_receipt}).bytes;
  const helper=(address,expected)=>({address,expected});
  generation.api=await dispatch[nativeMapping?'observe':'setup'](()=>require('./guarded-packets.cjs').create({native,owner:binding.entries.owner,epoch:generation.epoch,targets:binding.targets.map(t=>t.target),sdkProfile:binding.sdkProfile,packet:helper(binding.entries.packet,image.subarray(m.exports.iz_host_rpc_packet,m.exports.iz_host_rpc_packet+16)),receipt:helper(binding.entries.receipt,image.subarray(m.exports.iz_host_rpc_receipt,m.exports.iz_host_rpc_receipt+16)),fullWrite:helper(binding.entries.fullWrite,thunk.subarray(0,16)),fullReceipt:helper(binding.entries.fullReceipt,thunk.subarray(80,96)),execute:fn=>dispatch.run(generation.epoch,fn),...(row.wideCalls?{verifyExtended:async()=>extendedProof}:{})}));
  await (nativeMapping?dispatch.adoptPrepared(binding):dispatch.commitPrepared(binding));generation.graphicsMemoryProperties=binding.graphicsMemoryProperties;generation.serviceProfiles=binding.sdkProfile?['sdk']:[];return capability();
 }
 async function control(row){
  if(row.action==='status')return inspect();
  if(row.action==='participant'){assert(current&&row.epoch===current.epoch,'Wrong gateway participant epoch');return capability();}
  if(row.action==='retire'){assert(current&&row.epoch===current.epoch,'Wrong gateway retirement epoch');retire();return inspect();}
  if(row.action==='cancel-native'){assert(current&&row.epoch===current.epoch&&current.lease.guarded,'Wrong native cancellation epoch');try{return await dispatch.cancelNative(row.epoch,row.errorCode??0);}catch(error){if(error.resourcesRetained)throw retained(error);throw error;}}
  assert(!busy,'Another gateway control operation is pending');busy=true;
  try{
   if(row.action==='claim-native'){
    if(dispatch.inspect().phase==='open')await dispatch.prepare();const lease=await dispatch.claimNativeDisplay({supervisor:row.supervisor});current={epoch:lease.token,lease,api:null,retired:false};participants.clear();return inspect();
   }
   if(row.action==='begin'){
    assert(Number.isInteger(row.epoch)&&row.epoch>dispatch.inspect().epoch&&row.epoch<=0xffffffff&&['open','parked'].includes(dispatch.inspect().phase),'A fresh gateway display epoch is required');
    if(dispatch.inspect().phase==='open')await dispatch.prepare();
    const prove=()=>require('./host-binding.cjs').verifyDisplayLease({native,supervisor:row.supervisor,epoch:row.epoch});if(dispatch.inspect().phase==='open')await prove();else await dispatch.observe(prove);
    const lease={active:true,token:row.epoch};await dispatch.begin(lease);await dispatch.setup(prove);current={epoch:row.epoch,lease,api:null,retired:false};participants.clear();return inspect();
   }
   if(row.action==='observe'){assert(current&&row.epoch===current.epoch,'Wrong gateway observation epoch');assert(['read','read-many','query'].includes(row.request?.command),'Framework observation must be read-only');return await dispatch.observe(()=>invoke(native,row.request,{raw:true}));}
   if(row.action==='synchronize-native'){assert(current&&row.epoch===current.epoch&&current.lease.guarded,'Wrong native return epoch');const proof=await dispatch.confirmNativeReturn(row.epoch);if(proof.returned){current.retired=true;current.api?.revoke();participants.clear();}return {...proof,phase:dispatch.inspect().phase};}
   assert(current&&!current.retired&&row.epoch===current.epoch,'Wrong gateway setup epoch');
   if(row.action==='setup-rpc')return await dispatch.setup(()=>invoke(native,row.request,{raw:true}));
   if(row.action==='commit')return await commit(row);
   if(row.action==='commit-native')return await commit(row,true);
   if(row.action==='delegate-native-frames')return await dispatch.publishNativeFrames(row.epoch,row.frames);
   throw Object.assign(Error('Unsupported gateway control action'),{code:'IZ2S_GATEWAY_COMMAND',nativeRequestSent:false});
  }catch(error){if(error.nativeGenerationReturned&&current){current.retired=true;current.api?.revoke();participants.clear();}if(error.resourcesRetained||['sealing','opening','claiming','mapping','mapped','adopting','retained'].includes(dispatch.inspect().phase))throw retained(error);throw error;}finally{busy=false;}
 }
 const server=http.createServer(async(req,res)=>{
  let generation,started=false,timer,urgent=false;
  const disconnected=()=>{if(started&&!res.writableFinished){if(generation){generation.retired=true;generation.api?.revoke();if(current===generation&&dispatch.inspect().phase==='active')dispatch.retire(generation.epoch);}else if(busy&&!urgent)dispatch.sealForShutdown();}};
  try{
   assert(!closed&&req.method==='POST'&&req.headers['x-iz-gateway']===instance,'Invalid gateway channel');const token=String(req.headers.authorization||'').replace(/^Bearer /,'');
   urgent=req.url==='/cancel';const controller=(req.url==='/control'||urgent)&&equal(token,controllerToken),participant=req.url==='/rpc'?participants.get(token):null;if(req.url==='/rpc'&&!participant)throw refusal('Application gateway capability is retired');assert(controller||participant,'Invalid gateway credential');
   const parts=[];let length=0;for await(const part of req){length+=part.length;assert(length<=12*1024*1024,'Gateway request is too large');parts.push(part);}const row=JSON.parse(Buffer.concat(parts));
   if(controller&&!urgent&&row.action==='hello'){res.setHeader('content-type','application/json');res.end(encode({format:FORMAT,instance,sequence,cancelSequence,...inspect()}));return;}
   if(urgent)assert(row.action==='cancel-native','The urgent channel only accepts native cancellation');const owner=controller?{sequence:urgent?cancelSequence:sequence}:participant;assert(Number.isSafeInteger(row.sequence)&&row.sequence===owner.sequence+1,'Gateway sequence is stale or duplicated');if(controller){if(urgent)cancelSequence=row.sequence;else sequence=row.sequence;}else participant.sequence=row.sequence;
   if(participant){generation=participant.generation;if(generation!==current||generation.retired||row.epoch!==generation.epoch||dispatch.inspect().phase!=='active')throw refusal();}
   pending++;started=true;res.once('close',disconnected);timer=setInterval(()=>{if(!res.destroyed&&!res.writableFinished)res.writeProcessing();},30000);timer.unref();
   const result=controller?await control(row):await invoke(generation.api,row.request);
   res.setHeader('content-type','application/json');res.end(encode(result));
  }catch(error){if(generation&&error.resourcesRetained)retained(error);if(!res.destroyed){if(!started)Object.assign(error,{nativeRequestSent:false,requestReplayed:false});const fields=['code','resourcesRetained','nativeRequestSent','generationRevoked','requestReplayed','nativeStatus','menuBusy','nativeClaimRejected','framesNotAccepted','nativeGenerationReturned','nativeMappingRejected','nativeMappingError'];res.writeHead(400,{'content-type':'application/json'}).end(encode({error:error.message,...Object.fromEntries(fields.filter(k=>error[k]!==undefined).map(k=>[k,error[k]]))}));}}
  finally{clearInterval(timer);res.off('close',disconnected);if(started)pending--;}
 });
 server.requestTimeout=0;server.timeout=0;
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
 descriptor={format:FORMAT,instance,url:'http://127.0.0.1:'+server.address().port,token:controllerToken};
 try{await fs.writeFile(file,JSON.stringify(descriptor)+'\n',{flag:'wx',mode:0o600});}catch(error){server.closeAllConnections();await new Promise(resolve=>server.close(resolve));throw error;}
 function close(){if(!closing){closed=true;retire();dispatch.sealForShutdown();closing=(async()=>{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));try{const saved=JSON.parse(await fs.readFile(file));if(saved.instance===instance&&saved.token===controllerToken)await fs.unlink(file);}catch(error){if(error.code!=='ENOENT')throw error;}})();}return closing;}
 return Object.freeze({inspect,close,channel:Object.freeze({...descriptor})});
}
module.exports={serve,FILE,FORMAT};
