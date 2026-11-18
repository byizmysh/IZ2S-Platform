'use strict';
// ABI-1 PC-managed GPU application. RAM and SD use this same executor.
async function create({rpc,mapped,directory,videoStorage}, {createHost=options=>require('./application-host.cjs').create(options)}={}){
 if(!videoStorage||videoStorage.bytes<2097152)throw Error('Update IZ2S Platform: private video storage is unavailable');
 const platform=require('./lib/platform.cjs'),workspace=platform.require('core/session-workspace.cjs').create({root:platform.root,start:async()=>{throw Error('Open BlitBun from the console launcher');},stop:()=>rpc.write(mapped+1016,Buffer.from([1,0,0,0]))}),owned=callback=>workspace.run('blitbun','BlitBun',callback);
 let host,sequence=0,closed=false;
 try{
  host=await owned(()=>createHost({rpc,mapped,directory,persistRenderSettings:true}));host.setVideoStorage(videoStorage);
  const state=await rpc.read(mapped,1024);sequence=state.readFloatLE(972);await owned(()=>host.onState(state));
 }catch(error){await host?.close();await workspace.close();throw error;}
 return {pending(state){return !closed&&state.readFloatLE(972)!==sequence;},async afterBatch(state){if(closed)return;const next=state.readFloatLE(972);if(next!==sequence){sequence=next;await owned(()=>host.onState(state));}},async close(){if(closed)return;closed=true;try{await host.close();}finally{await workspace.close();}}};
}
module.exports={create};
