'use strict';
const dns=require('node:dns/promises'),net=require('node:net'),path=require('node:path'),fs=require('node:fs/promises');
const store=require('../../../core/services/device-pairings.cjs'),normalize=require('./settings.cjs').endpoint;
async function resolve(host,lookup=dns.lookup){if(net.isIPv4(host))return host;let timer;try{return (await Promise.race([lookup(host,{family:4}),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Name lookup timed out')),1500);})])).address;}catch{return null;}finally{clearTimeout(timer);}}
async function restore({root,profile={},fetchImpl=fetch,lookup=dns.lookup,registry}={}){
 let endpoint;try{endpoint=normalize(profile.cardputerEndpoint||'http://iz2s-adv.local');}catch{return {profile};}
 const host=new URL(endpoint).hostname,address=await resolve(host,lookup);
 const discovery={address,hostname:host,paired:false,networkReachable:false,pairingRequired:!profile.cardputerToken};
 const token=profile.cardputerToken,candidates=[];
 if(/^[a-f0-9]{64}$/.test(token||''))candidates.push({kind:'cardputer',endpoint,token,source:'profile'});
 else{
  try{const saved=JSON.parse(await fs.readFile(path.join(root,'data/state/cardputer-pairing.json'),'utf8'));if(/^[a-f0-9]{64}$/.test(saved.token||''))candidates.push({kind:'cardputer',endpoint,token:saved.token,source:'local-pairing'});}catch{}
  for(const row of await store.candidates('cardputer',{...registry,root})){
   let prior;try{prior=new URL(normalize(row.endpoint));}catch{continue;}
   if(prior.origin===endpoint||(address&&prior.port===new URL(endpoint).port&&(prior.hostname===address||row.address===address)))candidates.push(row);
  }
 }
 const unique=candidates.filter((r,i)=>candidates.findIndex(other=>other.token===r.token)===i);
 for(const candidate of unique){
  try{
   const response=await fetchImpl(endpoint+'/v1/status',{headers:{authorization:'Bearer '+candidate.token,connection:'close'},signal:AbortSignal.timeout(2500),redirect:'error'});
   discovery.networkReachable=true;if(!response.ok){discovery.pairingRequired=response.status===401||response.status===403;continue;}
   const text=await response.text();if(Buffer.byteLength(text)>16384)continue;const state=JSON.parse(text);
   if(state.device!=='iz2s-cardputer-adv'||state.apiVersion!==2||!/^[a-f0-9]{32}$/.test(state.bootId||'')||(candidate.identity&&candidate.identity!==state.usbSerial))continue;
   const ip=net.isIPv4(state.ip||'')?state.ip:address;
   const next={...profile,cardputerEndpoint:endpoint,cardputerToken:candidate.token,cardputerCapsLock:profile.cardputerCapsLock||'off'};
   await store.remember({kind:'cardputer',endpoint,token:candidate.token,address:ip,identity:state.usbSerial},registry).catch(()=>{});
   await require('../../../core/services/json-write.cjs').write(path.join(root,'data/state/cardputer-pairing.json'),{token:candidate.token},{directoryMode:0o700});
   return {profile:next,discovery:{...discovery,address:ip,paired:true,pairingRequired:false,automaticPairing:candidate.source!=='profile'}};
  }catch{}
 }
 if(!unique.length)try{const response=await fetchImpl(endpoint+'/v1/status',{signal:AbortSignal.timeout(1500),redirect:'error',headers:{connection:'close'}});discovery.networkReachable=response.status===401||response.ok;}catch{}
 return {profile,discovery};
}
module.exports={restore,resolve};
