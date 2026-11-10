'use strict';
// Route selection only. A successful TCP connection is not a resumed session:
// verify must authenticate the process and reconcile its command receipt.
const net=require('node:net'),assert=require('node:assert/strict');
const {privateIP}=require('../../session/network-discovery.cjs');
function dial(endpoint,{signal,timeoutMs}){return new Promise((resolve,reject)=>{
 const socket=net.createConnection({host:endpoint.address,port:endpoint.port});let settled=false;
 const finish=(error)=>{if(settled)return;settled=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);socket.off('error',failed);socket.off('connect',connected);if(error){socket.destroy();reject(error);}else{socket.on('error',()=>{});resolve(socket);}};
 const failed=e=>finish(e),connected=()=>finish(),abort=()=>finish(Error('Recovery route cancelled'));
 const timer=setTimeout(()=>finish(Error('Recovery route timed out')),timeoutMs);socket.once('error',failed);socket.once('connect',connected);signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();
});}
function endpoints(entries){
 const seen=new Set(),rows=[];for(const entry of entries){
  if(!entry||!privateIP(entry.address)||!Number.isInteger(entry.port)||entry.port<1024||entry.port>65535||entry.enabled===false)continue;
  const key=entry.address+':'+entry.port;if(seen.has(key))continue;seen.add(key);rows.push({...entry,key});if(rows.length===16)break;
 }return rows;
}
function create({candidates,verify,connect=dial,timeoutMs=2000,backoff=round=>Math.min(15000,1000*2**Math.min(round,4)),onState=()=>{}}){
 assert(typeof candidates==='function'&&typeof verify==='function');assert(Number.isInteger(timeoutMs)&&timeoutMs>=1&&timeoutMs<=30000);
 let pending=null,stopped=false,wakeWait=null,activeAbort=null,last=null,attempts=0,wakeGeneration=0;
 function emit(state,details={}){try{onState({state,attempts,...details});}catch{}}
 function wake(){wakeGeneration++;wakeWait?.();}
 async function wait(ms,generation){if(stopped||generation!==wakeGeneration)return;await new Promise(resolve=>{const done=()=>{clearTimeout(timer);if(wakeWait===done)wakeWait=null;resolve();},timer=setTimeout(done,Math.max(1,ms));wakeWait=done;});}
 async function run(){let round=0;while(!stopped){const generation=wakeGeneration;
  const rows=endpoints(await candidates());
  rows.sort((a,b)=>Number(b.key===last)-Number(a.key===last));
  for(const endpoint of rows){if(stopped)break;const abort=activeAbort=new AbortController();let transport,cancel,disposed=false;const dispose=()=>{if(!transport||disposed)return;disposed=true;if(transport.destroy)transport.destroy();else transport.close?.();};
   const aborted=new Promise((resolve,reject)=>{cancel=()=>{dispose();reject(Error(stopped?'Recovery stopped':'Recovery authentication timed out'));};abort.signal.addEventListener('abort',cancel,{once:true});});
   const timer=setTimeout(()=>abort.abort(),timeoutMs);
   try{attempts++;emit('connecting',{address:endpoint.address,mode:endpoint.mode});
    const work=(async()=>{transport=await connect(endpoint,{signal:abort.signal,timeoutMs});if(abort.signal.aborted){dispose();throw Error('Recovery route cancelled');}const session=await verify(transport,endpoint,{signal:abort.signal});if(abort.signal.aborted){dispose();throw Error('Recovery route cancelled');}return session;})();
    const session=await Promise.race([work,aborted]);if(stopped)throw Error('Recovery stopped');
    last=endpoint.key;activeAbort=null;emit('authenticated',{address:endpoint.address,mode:endpoint.mode});return {transport,session,endpoint};
   }catch(e){dispose();if(!stopped)emit('route-unavailable',{address:endpoint.address,mode:endpoint.mode,reason:e.message});}
   finally{clearTimeout(timer);abort.signal.removeEventListener('abort',cancel);if(activeAbort===abort)activeAbort=null;}
  }
  if(stopped)break;emit('waiting-for-network');await wait(rows.length?backoff(round++):2000,generation);
 }throw Error('Recovery stopped');}
 return {recover(){if(stopped)return Promise.reject(Error('Recovery stopped'));if(!pending)pending=run().finally(()=>{pending=null;});return pending;},wake,remember(endpoint){const [row]=endpoints([endpoint]);assert(row,'Invalid authenticated recovery endpoint');last=row.key;},stop(){stopped=true;activeAbort?.abort();wake();},get attempts(){return attempts;}};
}
module.exports={create,dial,endpoints};
