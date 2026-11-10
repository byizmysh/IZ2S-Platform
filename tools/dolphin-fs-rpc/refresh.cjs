'use strict';
// Upgrade an idle PC file broker after a module update without restarting Switch.
const fs=require('node:fs/promises'),path=require('node:path');
const pending=new Map(),VERSION=2;
async function refresh({root,backend,admit,readCommand=pid=>fs.readFile('/proc/'+pid+'/cmdline','utf8'),signal=(pid,name)=>process.kill(pid,name),waitForExit=async pid=>{
 for(let i=0;i<60;i++){try{process.kill(pid,0);}catch(error){if(error.code==='ESRCH')return;throw error;}await new Promise(resolve=>setTimeout(resolve,50));}
 throw Error('The previous file service is still closing. Reconnect SD storage shortly.');
}}){
 const state=await backend.request('status');
 if(state.protocolVersion>=VERSION||!Number.isSafeInteger(state.pid)||state.pid<=1)return state;
 if(state.downloads)return state;
 if(state.uploads){const jobs=await backend.requestRaw('sd-upload-list',{},2000);if(jobs.uploads.some(job=>!job.paused))return state;}
 const entry=path.join(path.resolve(root),'tools/dolphin-fs-rpc/service.cjs'),command=await readCommand(state.pid);
 if(!command.split('\0').includes(entry))throw Error('The previous file service could not be identified. Reconnect SD storage.');
 await admit();
 const stopped=await backend.requestRaw('stop',{lease:state.lease},15000);
 if(stopped.status!=='closed'&&stopped.status!=='idle')throw Error('File service cleanup has not completed. Reconnect SD storage.');
 if(await readCommand(state.pid)!==command)throw Error('The file service changed while closing. Reconnect SD storage.');
 signal(state.pid,'SIGTERM');await waitForExit(state.pid);
 return backend.request('status');
}
function ensure(options){const key=path.resolve(options.root);if(!pending.has(key))pending.set(key,refresh(options).finally(()=>pending.delete(key)));return pending.get(key);}
module.exports={ensure,VERSION};
