'use strict';
// Only private, directly attached networks are searched. A protocol marker
// identifies the game loader; an open TCP port alone is not a match.
const net=require('node:net'),os=require('node:os'),crypto=require('node:crypto');
const {isWireless,isTunnel}=require('../../../core/platform/network.cjs');
const privateIP=ip=>net.isIPv4(ip)&&(/^(10\.|192\.168\.)/.test(ip)||/^172\.(1[6-9]|2\d|3[01])\./.test(ip));
const number=ip=>ip.split('.').reduce((n,b)=>(n*256+Number(b))>>>0,0);
const address=n=>[24,16,8,0].map(shift=>(n>>>shift)&255).join('.');
function probe(host,{timeout=1200,signal,connect=net.createConnection}={}){
 if(!privateIP(host)||signal?.aborted)return Promise.resolve(false);
 return new Promise(resolve=>{
  const marker='IZ2S_CIV_'+crypto.randomBytes(12).toString('hex');
  let socket,timer,finished=false,response='';
  const finish=ok=>{if(finished)return;finished=true;clearTimeout(timer);signal?.removeEventListener('abort',abort);socket?.destroy();resolve(ok);};
  const abort=()=>finish(false);
  signal?.addEventListener('abort',abort,{once:true});
  timer=setTimeout(abort,Math.max(1,timeout));
  try{socket=connect({host,port:9026});socket.once('connect',()=>socket.end("(async()=>{await log('"+marker+"')})()"));
   socket.on('data',chunk=>{response+=chunk;if(response.includes(marker)&&response.includes('Executed'))finish(true);else if(response.length>8192)finish(false);});
   socket.once('error',abort);socket.once('close',abort);
  }catch{finish(false);}
 });
}
async function candidates({mode='auto',known=[],interfaces=os.networkInterfaces,wireless=isWireless,tunnel=isTunnel}={}){
 const networks=[],own=new Set();
 for(const [name,values]of Object.entries(interfaces())){
  if(await tunnel(name))continue;
  const wifi=await wireless(name);
  if(mode==='wifi'&&!wifi||mode==='lan'&&wifi)continue;
  for(const row of values||[]){
   if(row.internal||!['IPv4',4].includes(row.family)||!privateIP(row.address)||!net.isIPv4(row.netmask))continue;
   const mask=number(row.netmask),ip=number(row.address);
   own.add(row.address);networks.push({ip,mask});
  }
 }
 const attached=host=>privateIP(host)&&!own.has(host)&&networks.some(row=>(number(host)&row.mask)===(row.ip&row.mask));
 const preferred=[...new Set(known)].filter(attached),targets=new Set(preferred);
 for(const row of networks){
  // Limit large corporate subnets to the PC's /24, rather than flooding them.
  const mask=(~row.mask>>>0)>511?0xffffff00:row.mask,base=(row.ip&mask)>>>0,count=(~mask>>>0)+1;
  for(let i=1;i<count-1&&targets.size<512;i++){const host=address((base+i)>>>0);if(!own.has(host))targets.add(host);}
 }
 return {preferred,targets:[...targets].filter(host=>!preferred.includes(host))};
}
async function discover(options={}){
 const {signal,timeout=6000,test=probe}=options,deadline=Date.now()+timeout;
 const {preferred,targets}=await candidates(options);
 // Previously paired addresses are a hint, never proof of a current connection.
 for(const host of preferred){if(signal?.aborted||Date.now()>=deadline)return [];if(await test(host,{signal,timeout:Math.min(900,deadline-Date.now())}))return [host];}
 let next=0;const found=[];
 await Promise.all(Array.from({length:Math.min(48,targets.length)},async()=>{
  while(next<targets.length&&!signal?.aborted&&Date.now()<deadline){const host=targets[next++];if(await test(host,{signal,timeout:Math.min(800,deadline-Date.now())}))found.push(host);}
 }));
 return signal?.aborted?[]:found;
}
function knownHosts(connections,saved=''){
 const peer=connections?.devices?.lan||{};
 return [saved,peer.address,...Object.values(peer.addresses||{}),...Object.values(peer.lastKnownAddresses||{}).map(row=>row?.address),...(peer.candidates||[]).map(row=>row.address)].filter(Boolean);
}
module.exports={probe,discover,candidates,knownHosts};
