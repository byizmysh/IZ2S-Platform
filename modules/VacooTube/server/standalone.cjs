'use strict';
async function create(context,{createHost=options=>require('./session-hook.cjs').create(options)}={}){
 if(!context.videoStorage||context.videoStorage.bytes<2097152)throw Error('Update IZ2S Platform: private video storage is unavailable');
 const platform=require('./platform.cjs'),workspace=require(require('node:path').join(platform.root,'core/session-workspace.cjs')).create({root:platform.root,start:async()=>{throw Error('Open VacooTube from the console launcher');},stop:()=>context.rpc.write(context.mapped+1016,Buffer.from([1,0,0,0]))}),owned=callback=>workspace.run('vacootube','VacooTube',callback);
 let host,sequence=0,closed=false;
 try{host=await owned(()=>createHost(context));host.setVideoStorage(context.videoStorage);const state=await context.rpc.read(context.mapped,1024);sequence=state.readFloatLE(972);await owned(()=>host.onState(state));await host.drain?.();}catch(error){await host?.close();await workspace.close();throw error;}
 return {pending:state=>!closed&&state.readFloatLE(972)!==sequence,async afterBatch(state){if(closed)return;const next=state.readFloatLE(972);if(next!==sequence){sequence=next;await owned(()=>host.onState(state));await host.drain?.();}},async close(){if(closed)return;closed=true;try{await host.close();}finally{await workspace.close();}}};
}
module.exports={create};
