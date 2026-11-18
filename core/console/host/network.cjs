const net=require('node:net');
const definitions=require('../abi/service-functions.json');
const receiveTimeouts=new WeakSet();
function isReceiveTimeout(error){return error!==null&&typeof error==='object'&&receiveTimeouts.has(error);}
function endpoint(value){
 if(!value||!net.isIPv4(value.address)||!Number.isInteger(value.port)||value.port<1||value.port>65535)throw Error('UDP endpoint requires an IPv4 address and port 1..65535');
 return {address:value.address,port:value.port};
}
async function create(rpc,directory){
 if(rpc.generationGuarded===true){
  if(typeof rpc.udp!=='function')throw Error('This guarded channel has no native UDP executor');let busy=false;
  return {async exchange(destination,payload,{timeout=1000}={}){
   destination=endpoint(destination);if(!Buffer.isBuffer(payload)||payload.length<1||payload.length>1024)throw Error('UDP payload must be 1..1024 bytes');if(!Number.isInteger(timeout)||timeout<1||timeout>5000)throw Error('UDP timeout must be 1..5000ms');if(busy)throw Error('UDP exchange already in flight');busy=true;
   try{const receipt=await rpc.udp(destination,payload,{timeout});
    if(receipt.result===4n){const error=Error('UDP receive timed out');error.code='ETIMEDOUT';receiveTimeouts.add(error);throw error;}
    if(receipt.result!==0n)throw Object.assign(Error('Native UDP exchange failed: '+receipt.nativeResult),{nativeResult:receipt.nativeResult});return receipt.data;
   }finally{busy=false;}
  }};
 }
 const names=['socket','sendto','recv','socketClose','errno','socketInitialized'];
 const api=await require('../native/client.cjs').create(rpc,Object.fromEntries(names.map(k=>[k,definitions[k]])));
 const worker=await require('../native/six.cjs').create(rpc,directory);let busy=false;
 return {async exchange(destination,payload,{timeout=1000}={}){
  destination=endpoint(destination);
  if(!Buffer.isBuffer(payload)||payload.length<1||payload.length>1024)throw Error('UDP payload must be 1..1024 bytes');
  if(!Number.isInteger(timeout)||timeout<1||timeout>5000)throw Error('UDP timeout must be 1..5000ms');
  if(busy)throw Error('UDP exchange already in flight');busy=true;let fd=-1;
  try{
   if(await api.result('socketInitialized')!==1)throw Error('Console sockets are not initialized');
   fd=Number(BigInt.asIntN(32,await api.call('socket',[2,2,17])));if(fd<0)throw Error('UDP socket failed');
   const address=Buffer.alloc(16);address[0]=16;address[1]=2;address.writeUInt16BE(destination.port,2);destination.address.split('.').forEach((n,i)=>address[4+i]=+n);
   await api.write(0,address);await api.write(256,payload);
   const sent=Number(BigInt.asIntN(64,await worker.call(api.functions.sendto,[fd,api.base+256,payload.length,0,api.base,16])));
   if(sent!==payload.length)throw Error('UDP send failed');
   const until=Date.now()+timeout;
   do{
    const n=Number(BigInt.asIntN(64,await api.call('recv',[fd,api.base+2048,1024,0x80])));
    if(n>=0){if(n>1024)throw Error('UDP response exceeds buffer');return api.read(2048,n);}
    const errno=await api.result('errno');if(errno!==35&&errno!==11)throw Error('UDP receive errno '+errno);
    await new Promise(r=>setTimeout(r,20));
   }while(Date.now()<until);
   const error=Error('UDP receive timed out');error.code='ETIMEDOUT';receiveTimeouts.add(error);throw error;
  }finally{try{if(fd>=0)await api.result('socketClose',[fd]);}finally{busy=false;}}
 }};
}
module.exports={create,endpoint,isReceiveTimeout};
