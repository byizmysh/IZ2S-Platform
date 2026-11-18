const fs=require('node:fs/promises'),path=require('node:path');
async function create(context,{createHost=options=>require('./application-host.cjs').create({...options,persistRenderSettings:true})}={}){
 let host=null,storage,available=true,last=0,work=Promise.resolve(),closed=false;
 return {setVideoStorage:s=>{storage=s;host?.setVideoStorage(s);},setAvailable:v=>{available=v;host?.setAvailable(v);},
 onState(state){const action=state.readFloatLE(968),seq=state.readFloatLE(972);if(closed||!available||![1,2,3,5,6,7,16,17,18,19,20,21,22,24,25,26,27,28,29,30,32,33,34,35,36,37,38].includes(action)||!Number.isInteger(seq)||seq<1||seq===last)return;last=seq;const packet=Buffer.from(state);work=work.catch(()=>{}).then(async()=>{if(closed||!available)return;if(!host){host=await createHost(context);host.setVideoStorage(storage);host.setAvailable(available);}await host.onState(packet);});work.catch(error=>console.error('BlitBun: '+error.message));return work;},
 drain:()=>work,
 async pollInterval(){if(!host)return 50;const row=await fs.readFile(path.join(context.directory,'pc-screen-receiver.json'),'utf8').then(JSON.parse,()=>null);return row?.session===context.rpc.a.session&&row?.running?200:50;},
 async suspend(){await work.catch(()=>{});await host?.close();host=null;},async close(){closed=true;await work.catch(()=>{});await host?.close();host=null;}};
}
module.exports={create};
