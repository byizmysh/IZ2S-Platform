'use strict';
// One startup observation. Thereafter the existing video ACK carries mode/route
// telemetry from the SDK's native sampler: no host timer, getter or heartbeat RPC.
async function create({rpc,directory,preview=false,createReader=options=>require('./platform.cjs').require('core/services/console-state.cjs').create(options)}){
 let state=null,revision=0;
 if(!preview&&rpc.a?.sdkCrc32===3925528160){const reader=await createReader({rpc,directory});try{state=await reader.refresh();}finally{reader.close();}}
 return {get state(){return state;},get revision(){return revision;},observe(packet,address){
  if(!Buffer.isBuffer(packet)||packet.length<336)return;const flags=packet.readUInt32LE(332);
  if(!(flags&0x10000000))return;
  if(!(flags&0x02000000)){if(state)state={...state,at:new Date().toISOString(),source:'video-ack/last-known-route'};return;}
  const mode=flags&16?'docked':'handheld',type=(flags>>>5)&3,connected=!!(flags&128);
  if(![1,2].includes(type))return;
  const network=type===2?'ethernet':'wifi';
  if(state&&(state.mode!==mode||state.network?.type!==network||state.network?.connected!==connected))revision++;
  state={at:new Date().toISOString(),stale:false,mode,operationMode:mode==='docked'?1:0,network:{type:network,connected,status:connected?4:0,ip:address||state?.network?.ip||null},source:'video-ack'};
 },close(){}};
}
module.exports={create};
