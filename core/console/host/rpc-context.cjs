'use strict';
// Session hardware is created once. Each asynchronous SDK boundary carries
// its own immutable RPC route; detached work cannot borrow a later boundary.
const {AsyncLocalStorage}=require('node:async_hooks');
function create(session){
 if(!session||typeof session!=='object')throw Error('SDK RPC context requires a session backend');
 const storage=new AsyncLocalStorage(),dynamicMethods=new Map();
 function revoked(){return Object.assign(Error('The SDK RPC boundary has ended'),{code:'IZ2S_SDK_RPC_REVOKED',nativeRequestSent:false,requestReplayed:false});}
 function select(fixed){
  const token=fixed??storage.getStore();
  if(token){if(!token.active||!token.current())throw revoked();return {backend:token.backend,token};}
  return {backend:session,token:null};
 }
 function clientView(source,backend,token){
  // Platform native-client objects may otherwise keep a private raw route.
  // Such an object stays with the backend which actually created it.
  const methods=new Map();
  function check(){const selected=select(token);if(selected.backend!==backend)throw revoked();}
  return new Proxy(Object.create(null),{get(_target,key){
   const value=Reflect.get(source,key,source);
   if(value===backend||value===session)return facade(token);
   if(typeof value!=='function'){check();return value;}
   if(!methods.has(key))methods.set(key,(...args)=>{try{check();return Reflect.apply(value,source,args);}catch(error){return Promise.reject(error);}});
   return methods.get(key);
  },has:(_target,key)=>Reflect.has(source,key),ownKeys:()=>Reflect.ownKeys(source),getOwnPropertyDescriptor(_target,key){const descriptor=Reflect.getOwnPropertyDescriptor(source,key);return descriptor?{configurable:true,enumerable:descriptor.enumerable}:undefined;},set:()=>false,defineProperty:()=>false,deleteProperty:()=>false});
 }
 function facade(fixed){
  const methods=fixed?new Map():dynamicMethods;
  return new Proxy(Object.create(null),{get(_target,key){
   const {backend,token}=select(fixed),value=Reflect.get(backend,key,backend);
   if(typeof value!=='function')return value;
   // Access inside a boundary yields a fixed closure as well. Keeping that
   // function and invoking it outside AsyncLocalStorage never exposes session.
   const captured=fixed??token,cache=captured?captured.methods:methods;
   if(!cache.has(key))cache.set(key,(...args)=>{
    try{
     const selected=select(captured),method=Reflect.get(selected.backend,key,selected.backend);
     if(typeof method!=='function')throw Error('SDK RPC method unavailable: '+String(key));
     const result=Reflect.apply(method,selected.backend,args);
     return key==='createNativeClient'?Promise.resolve(result).then(client=>clientView(client,selected.backend,selected.token)):result;
    }catch(error){return Promise.reject(error);}
   });
   return cache.get(key);
  },has:(_target,key)=>Reflect.has(select(fixed).backend,key),set:()=>false,defineProperty:()=>false,deleteProperty:()=>false});
 }
 const rpc=facade();
 async function run(backend,current,action){
  if(!backend||typeof backend!=='object'||backend===rpc||typeof current!=='function'||typeof action!=='function')throw Error('Invalid SDK RPC boundary');
  const parent=storage.getStore();if(parent)throw Error('SDK RPC boundaries cannot be replaced inside a handler');
  const token={backend,current,active:true,methods:new Map()};
  return storage.run(token,async()=>{try{select(token);return await action();}finally{token.active=false;}});
 }
 return Object.freeze({rpc,run});
}
module.exports={create};
