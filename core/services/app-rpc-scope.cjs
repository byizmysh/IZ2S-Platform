'use strict';
// A lifetime boundary over the existing RPC interface. It adds no console
// protocol or polling. Closure drains dispatched mutations, never guesses
// completion from a timeout, and does not certify native-thread/resource exit.
const {AsyncLocalStorage}=require('node:async_hooks'),policy=require('../session/offline-policy.cjs');
const CHUNK=0x80000,MAX=0x4000000,READS=new Set(['read','readMany','query','getGraphicsMemoryProperties']);
const READ_COMMANDS=new Set(['read','read-many','query','verify-function']);
const backends=new WeakMap();
function isRPCFor(rpc,backend){return backends.has(rpc)&&backends.get(rpc)===backend;}
function create(backend,{id,execution}={}){
 if(!backend||typeof backend!=='object'||typeof id!=='string'||!id||!Number.isSafeInteger(execution)||execution<1)throw Error('Invalid application RPC scope');
 const cleanup=new AsyncLocalStorage(),jobs=new Set(),wrappers=new WeakMap();let phase='active',failure=null,closing=null,revocations=0,started=0,mutations=0,segments=0;
 const identity={id,execution};
 function unsafe(error){return policy.requiresRetention(error)||policy.isTransportLoss(error);}
 function revoked(){return Object.assign(Error('The application RPC scope is closed'),{code:'IZ2S_APP_RPC_REVOKED',appRPCScope:identity});}
 function retained(error){return Object.assign(Error('Application RPC completion is unverified: '+(error?.message||'native owner retained'),{cause:error}),{code:'IZ2S_APP_RPC_UNCERTAIN',appRPCScope:identity,resourcesRetained:true});}
 function permitted(){const token=cleanup.getStore();return !failure&&(phase==='active'||phase==='stopping'||phase==='closing'&&token?.active===true);}
 function observeBackend(){if(backend.uncertain===true&&!failure){failure=Object.assign(Error('The native RPC backend retained an unverified request'),{resourcesRetained:true});phase='retained';}}
 function check(){observeBackend();if(failure)throw retained(failure);if(!permitted())throw revoked();}
 function observation(name,args){return READS.has(name)||name==='request'&&READ_COMMANDS.has(args[0]?.command);}
 function recordFailure(error){if(unsafe(error)){failure||=error;phase='retained';}}
 function status(){const rows=[...jobs];return {format:'iz2s.app-rpc-scope/1',...identity,state:phase,started,mutations,segments,revocations,pendingMutations:rows.filter(row=>!row.read).length,pendingObservations:rows.filter(row=>row.read).length,resourcesRetained:!!failure,nativeThreadsProven:false};}
 async function transfer(source,name,args){
  const [address,value]=args,valid=Number.isSafeInteger(address)&&(name==='write'?Buffer.isBuffer(value)&&value.length<=MAX:Number.isInteger(value)&&value>=0&&value<=MAX),length=name==='write'?value?.length:value;
  // CivSwitch already performs native fills/streaming below this interface.
  // Splitting that call would turn one native fill into sixteen operations.
  // A generation-guarded backend checks every native packet itself; splitting
  // its upload here repeats mapping queries without strengthening revocation.
  if(source!==backend||backend.nativeABI==='civswitch'||backend.generationGuarded===true&&name==='write'||!['read','write'].includes(name)||!valid||length<=CHUNK){segments++;return Reflect.apply(source[name],source,args);}
  const parts=[];
  for(let at=0;at<length;at+=CHUNK){
   // The canonical client already transfers in 512 KiB chunks. Keeping that
   // boundary here stops its next chunk if the application has since closed.
   check();const n=Math.min(CHUNK,length-at);segments++;
   const result=await Reflect.apply(source[name],source,[address+at,name==='write'?value.subarray(at,at+n):n,...args.slice(2)]);if(name==='read')parts.push(result);
  }
  return name==='read'?Buffer.concat(parts):undefined;
 }
 function wrap(source){
  if(source===backend&&rpc)return rpc;let cached=wrappers.get(source);if(cached)return cached;
  const methods=new Map(),target=Object.create(null);
  const get=key=>{
   const value=Reflect.get(source,key,source);if(value===backend)return rpc;if(typeof value!=='function')return value;
   const cachedMethod=methods.get(key);if(cachedMethod?.value===value)return cachedMethod.wrapped;
   const wrapped=(...args)=>{
    try{check();}catch(error){const rejected=Promise.reject(error);rejected.catch(()=>{});return rejected;}
    const row={read:observation(key,args)};started++;if(!row.read)mutations++;jobs.add(row);
    const operation=(async()=>{try{const result=await transfer(source,key,args);observeBackend();if(failure)throw retained(failure);return key==='createNativeClient'&&result&&typeof result==='object'?wrap(result):result;}catch(error){observeBackend();if(!row.read||error.uncertain===true)recordFailure(error);throw failure?retained(failure):error;}finally{jobs.delete(row);}})();
    // Keep a handled completion for the close barrier even when the caller
    // catches/ignores its own result. Reads may finish after clean closure;
    // their next attempted command is rejected by this same scope.
    row.settled=operation.then(()=>{},()=>{});return operation;
   };
   methods.set(key,{value,wrapped});return wrapped;
  };
  cached=new Proxy(target,{get:(_target,key)=>get(key),has:(_target,key)=>Reflect.has(source,key),ownKeys:()=>Reflect.ownKeys(source),getOwnPropertyDescriptor(_target,key){const descriptor=Reflect.getOwnPropertyDescriptor(source,key);return descriptor?{configurable:true,enumerable:descriptor.enumerable,value:get(key),writable:false}:undefined;},set:()=>false,defineProperty:()=>false,deleteProperty:()=>false});wrappers.set(source,cached);return cached;
 }
 let rpc;rpc=wrap(backend);backends.set(rpc,backend);
 function revoke(reason){if(phase==='closed'||phase==='retained')return status();if(phase==='active'||phase==='stopping'){phase='closing';revocations++;}if(unsafe(reason)){failure||=reason;phase='retained';}return status();}
 function close(stop){
  if(closing)return closing;
  // Normal service shutdown may need its own existing background work to
  // finish releasing sockets, DSP buffers or threads. The menu still belongs
  // to this app throughout that stop handler. Explicit revoke() is immediate.
  if(phase==='active'&&typeof stop==='function')phase='stopping';else revoke();
  closing=(async()=>{
   const token={active:true};let error;
   try{if(!failure&&stop)await cleanup.run(token,stop);}catch(cause){error=cause;recordFailure(cause);}finally{token.active=false;revoke();}
   // Cleanup may have issued unawaited operations. Its capability is expired
   // now; no detached timer can add a new mutation during or after this drain.
   for(;;){const pending=[...jobs].filter(row=>!row.read);if(!pending.length)break;await Promise.all(pending.map(row=>row.settled));}
   observeBackend();if(failure||error){failure||=error;phase='retained';throw retained(failure);}
   phase='closed';return {...status(),mutationsDrained:true,lateRequestsRevoked:true,resourcesReleased:false};
  })();return closing;
 }
 return {rpc,revoke,close,status,assertUsable:check};
}
module.exports={create,CHUNK,isRPCFor};
