'use strict';
// Native-menu services shared by the Civ7 panel. Launch is owned exclusively by
// IzCivSwitchFS; this controller only supplies PC Link state and session reset.
const os=require('node:os');
const runtime=require('./runtime.cjs');
const DEFAULT_OUT=runtime.output();
function create({out=DEFAULT_OUT,manager,resetLink=()=>require('./tools/pc-link-service.cjs').reset(out)}={}){
 const state=async()=>({busy:false,owned:false,attempt:null,historicalAttempt:false,out,
  hosts:[...new Set(Object.values(os.networkInterfaces()).flat().filter(x=>x&&x.family==='IPv4'&&!x.internal).map(x=>x.address))]});
 return {state,running:()=>false,stop:async()=>({ok:true}),async reset(){
  if(manager?.preview)throw Error('Session reset is unavailable in Preview.');
  await resetLink();return {ok:true,reset:true};
 }};
}
module.exports={create,DEFAULT_OUT};
