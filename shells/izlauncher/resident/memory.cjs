'use strict';
const L=require('./layout.cjs');
const BASE_BYTES=L.INPUT_BYTES+(L.TABLE+L.MAX_SLOTS*L.STRIDE)*4+L.STATE_BYTES*2+8;
function trimmedBytes(bytes){let n=bytes.length;while(n>4&&bytes[n-1]===0)n--;return Math.min(bytes.length,Math.ceil(n/4)*4);}
function estimate(app,{menu=false}={}){
 const state=Buffer.concat([app.files.state.bytes,app.files.data.bytes]);
 const initialStateBytes=menu?L.STATE_BYTES:trimmedBytes(state),privateBytes=app.files.memory?.allocationBytes||4;
 const initialPrivateBytes=trimmedBytes(app.files.memory?.bytes||Buffer.alloc(4));
 return {controlsBytes:L.STATE_BYTES,privateBytes,initialStateBytes,initialPrivateBytes,
  bufferBytes:L.STATE_BYTES+privateBytes+initialStateBytes+(initialPrivateBytes>32*1048576?Math.ceil(initialPrivateBytes/4096)*4096:initialPrivateBytes)};
}
function create({budgetBytes=256*1048576,baseBytes=0,maxApps=15}={}){
 if(!Number.isSafeInteger(budgetBytes)||budgetBytes<=0||!Number.isSafeInteger(baseBytes)||baseBytes<0||baseBytes>budgetBytes)throw Error('Invalid resident RAM budget');
 const entries=new Map();let usedBytes=baseBytes;
 function admit(app){
  const cached=entries.get(app.id);
  if(cached){if(cached.fingerprint!==app.fingerprint)throw Error('A different version of '+app.id+' is retained in RAM; start a fresh session to replace it');return {...cached,cached:true};}
  if(entries.size>=maxApps)throw Error('Resident RAM slot limit reached; start a fresh session');
  const cost=estimate(app);if(usedBytes+cost.bufferBytes>budgetBytes)throw Error('Resident RAM budget exceeded before allocation: '+(usedBytes+cost.bufferBytes)+' > '+budgetBytes);
  return {cached:false,id:app.id,fingerprint:app.fingerprint,...cost};
 }
 return {
  admit,
  plan(apps){const unique=new Set(),additions=[];for(const app of apps){if(unique.has(app.id))throw Error('Duplicate resident app identity');unique.add(app.id);const entry=admit(app);if(!entry.cached)additions.push(entry);}
   if(entries.size+additions.length>maxApps)throw Error('Resident RAM slot limit reached; start a fresh session');
   const nextBytes=usedBytes+additions.reduce((n,a)=>n+a.bufferBytes,0);if(nextBytes>budgetBytes)throw Error('Resident RAM budget exceeded before allocation: '+nextBytes+' > '+budgetBytes);return {nextBytes,additions};},
  retain(app,resources){const admission=admit(app);if(admission.cached)return entries.get(app.id).resources;entries.set(app.id,{...admission,resources});usedBytes+=admission.bufferBytes;return resources;},
  get:app=>{const admission=admit(app);return admission.cached?admission.resources:null;},
  status:()=>({owner:'launcher',lifetime:'session',requiresAppOptIn:false,reentry:'restart',automaticEviction:false,budgetBytes,usedBytes,remainingBytes:budgetBytes-usedBytes,
   apps:[...entries.values()].map(({resources,cached,...entry})=>({...entry,resident:true,applicationMemoryDeclared:!!resources.app?.manifest.memory}))})
 };
}
module.exports={create,estimate,trimmedBytes,BASE_BYTES};
