'use strict';
const net=require('node:net'),path=require('node:path'),fs=require('node:fs/promises');
const store=require('../../../core/services/device-pairings.cjs');
async function restore({root,profile={},registry,connect=require('../../transports/switch-wifi/index.cjs').connect,discover=require('./discovery.cjs').discover,now=Date.now}={}){
 const token=String(profile.switchToken||'').replace(/[\s-]/g,'').toLowerCase(),port=Number(profile.switchPort||24872),address=profile.switchAddress||'';
 const valid=value=>/^[a-f0-9]{32}$/.test(value||'');
 if(!Number.isInteger(port)||port<1024||port>65535)return {profile,discovery:{paired:false,pairingRequired:!valid(token)}};
 const credentials=[];
 if(valid(token))credentials.push({kind:'switch1',endpoint:address,port,token,source:'profile'});
 else{
  try{const saved=(await fs.readFile(path.join(root,'data/state/switch-keyboard/connection.cfg'),'utf8')).trim();if(valid(saved))credentials.push({kind:'switch1',endpoint:address,port,token:saved,source:'local-pairing'});}catch{}
  for(const row of await store.candidates('switch1',{...registry,root}))if(Number(row.port||24872)===port)credentials.push(row);
 }
 const keys=credentials.filter((r,i)=>credentials.findIndex(other=>other.token===r.token)===i);
 const verified=[],tried=new Set(),deadline=now()+12000;let incomplete=false;
 async function verify(candidate){
  if(!net.isIPv4(candidate.endpoint)||tried.has(candidate.token+'@'+candidate.endpoint))return;
  const remaining=deadline-now();if(remaining<=0){incomplete=true;return;}
  tried.add(candidate.token+'@'+candidate.endpoint);
  let client;try{
   client=await connect({switchAddress:candidate.endpoint,switchPort:port,switchToken:candidate.token},{timeout:Math.max(1,Math.min(1500,Math.floor(remaining/2)))});
   if(/^IZSWITCH 1 (READY|WAIT)$/.test(await client.exchange('STATUS')))verified.push(candidate);
  }catch{}finally{client?.close();}
 }
 // Preserve a reachable selected device. Re-discover only after it stops answering.
 if(net.isIPv4(address))for(const key of keys){await verify({...key,endpoint:address});if(verified.length)break;}
 if(!verified.length && !profile.switchManualAddress){
  for(const candidate of credentials)await verify(candidate);
  if(!verified.length&&keys.length){
   const endpoints=await discover({port,exclude:net.isIPv4(address)?[address]:[]}).catch(()=>[]);
   for(const endpoint of endpoints){
    for(const key of keys){await verify({...key,endpoint,source:'network'});if(verified.some(r=>r.endpoint===endpoint))break;}
   }
  }
 }
 const candidates=[...new Set(verified.map(r=>r.endpoint))].map(address=>({address}));
 if(incomplete||candidates.length!==1)return {profile,discovery:{address:address||null,paired:false,pairingRequired:keys.length===0,
   networkReachable:candidates.length>0,ambiguous:candidates.length>1,incomplete,candidates}};
 const chosen=verified[0],next={...profile,switchAddress:chosen.endpoint,switchPort:port,switchToken:chosen.token};
 await store.remember({...chosen,address:chosen.endpoint},registry).catch(()=>{});
 const file=path.join(root,'data/state/switch-keyboard/connection.cfg');await fs.mkdir(path.dirname(file),{recursive:true,mode:0o700});
 await fs.writeFile(file,chosen.token+'\n',{mode:0o600});
 return {profile:next,discovery:{address:chosen.endpoint,paired:true,networkReachable:true,pairingRequired:false,automaticPairing:chosen.source!=='profile'}};
}
module.exports={restore};
