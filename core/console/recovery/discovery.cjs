'use strict';
const dgram=require('node:dgram'),os=require('node:os'),W=require('./wire.cjs'),assert=require('node:assert/strict');
const {privateIP}=require('../../session/network-discovery.cjs');
const PORT=39419,GROUP='239.255.77.15';
// The final two discovery words extend legacy zero fields with a retained
// menu-wake counter and feature bit 0. Only authenticated new sequences may
// advance lastWake; the host keeps failed wake attempts pending until proof.
function index({credentials,onCandidate=()=>{},now=()=>Date.now()}){const auth={session:Buffer.from(credentials.session,'hex'),key:Buffer.from(credentials.key,'hex'),nonce:Buffer.alloc(32)};let sequence=0n,lastSeen=null,lastWake=null;const rows=new Map();return {
 receive(bytes,remote){try{if(!Buffer.isBuffer(bytes)||bytes.length!==160||!privateIP(remote.address))return false;const frame=W.decode(bytes,auth);if(frame.type!==W.TYPES.DISCOVERY||frame.sequence<=sequence||frame.sequence===0n)return false;const b=frame.payload,port=b.readUInt32LE(),address=[...b.subarray(4,8)].join('.'),type=b.readUInt32LE(16),networkStatus=b.readUInt32LE(20),wake=b.readUInt32LE(24),features=b.readUInt32LE(28);if(port<1024||port>65535||address!==remote.address||(features&~1)||(!features&&wake))return false;const timestamp=now(),mode=type===1?'wifi':type===2?'lan':'unknown',row={address,port,mode,networkStatus,source:'authenticated-console-announcement',sequence:frame.sequence.toString(),receivedAt:new Date(timestamp).toISOString(),afterGap:lastSeen!==null&&timestamp-lastSeen>5000,wakeSequence:wake,wakeRequested:features===1&&(lastWake!==null?wake!==lastWake:wake!==0)};sequence=frame.sequence;lastSeen=timestamp;lastWake=features===1?wake:null;rows.set(mode,row);if(rows.size>3)rows.delete(rows.keys().next().value);onCandidate({...row});return true;}catch{return false;}},
 candidates(){return [...rows.values()].map(row=>({...row}));},get sequence(){return sequence;}
};}
async function listen({credentials,onCandidate,port=PORT,interfaces=()=>os.networkInterfaces(),socketFactory=()=>dgram.createSocket({type:'udp4',reuseAddr:true})}){
 assert(Number.isInteger(port)&&port>=1024&&port<=65535);const records=index({credentials,onCandidate}),socket=socketFactory(),memberships=new Set(),errors=[];let closed=false;
 socket.on('message',(bytes,remote)=>records.receive(bytes,remote));socket.on('error',e=>errors.push(e.code||e.message));
 await new Promise((resolve,reject)=>{const failed=e=>{socket.off('listening',ready);try{socket.close();}catch{}reject(e);},ready=()=>{socket.off('error',failed);resolve();};socket.once('error',failed);socket.once('listening',ready);socket.bind(port,'0.0.0.0');});
 const refresh=()=>{if(closed)return;const current=new Set(Object.values(interfaces()).flat().filter(i=>i.family==='IPv4'&&!i.internal&&privateIP(i.address)).map(i=>i.address));for(const address of current)if(!memberships.has(address))try{socket.addMembership(GROUP,address);memberships.add(address);}catch(e){errors.push(e.code||e.message);}for(const address of memberships)if(!current.has(address)){try{socket.dropMembership(GROUP,address);}catch{}memberships.delete(address);}if(errors.length>16)errors.splice(0,errors.length-16);};refresh();const timer=setInterval(refresh,10000);timer.unref();
 return {candidates:records.candidates,errors,refresh,async close(){if(closed)return;closed=true;clearInterval(timer);await new Promise(resolve=>socket.close(resolve));}};
}
module.exports={index,listen,PORT,GROUP};
