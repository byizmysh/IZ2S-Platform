// Ordinary application client/reference implementation. It has no native RPC.
const crypto=require('node:crypto'),net=require('node:net');
const passport=require('./passport.cjs'),wire=require('./wire.cjs');
function create({host,port,session,secret,processId=crypto.randomBytes(16).toString('hex'),getStatus=()=>({}),
 retryMs=250,maxRetryMs=8000,timeoutMs=5000,onState=()=>{}}){
 if(!net.isIPv4(host)||!Number.isInteger(port)||port<1||port>65535||!/^[a-f0-9]{32}$/.test(session)||!/^[a-f0-9]{32}$/.test(processId)||!/^[a-f0-9]{64}$/.test(secret)||!Number.isInteger(retryMs)||retryMs<10||!Number.isInteger(maxRetryMs)||maxRetryMs<retryMs||!Number.isInteger(timeoutMs)||timeoutMs<20)throw Error('Invalid application recovery client');
 const key=Buffer.from(secret,'hex'),book=Buffer.alloc(passport.BYTES);
 let socket=null,timer=null,stopped=false,started=false,delay=retryMs,nonce=null,sentSequence=0,heartbeat=0;
 const emit=state=>{try{onState(state);}catch{}};
 function connect(){
  if(stopped)return;
  nonce=null;
  const owned=socket=net.createConnection({host,port});owned.setNoDelay(true);owned.setTimeout(timeoutMs,()=>owned.destroy());
  const parse=wire.parser((type,bytes)=>{
   if(type===wire.CHALLENGE){
    if(nonce)throw Error('Previous recovery status is unacknowledged');
    nonce=bytes;
    const status={...getStatus(),session,process:processId,heartbeat:++heartbeat};
    const row=passport.update(book,status);sentSequence=row.sequence;
    owned.write(wire.frame(wire.PROOF,wire.proof(key,nonce,book)));
   }else if(type===wire.ACK){
    if(!nonce||!wire.verifyAck(key,nonce,bytes)||Number(bytes.readBigUInt64LE())!==sentSequence)throw Error('Invalid recovery acknowledgement');
    nonce=null;delay=retryMs;emit('connected');
   }else throw Error('Unexpected recovery frame');
  });
  owned.on('data',bytes=>{if(socket!==owned)return;try{parse(bytes);}catch{owned.destroy();}});
  owned.on('error',()=>{});
  owned.on('close',()=>{
   if(socket!==owned)return;socket=null;
   if(stopped)return;
   emit('waiting-for-network');
   const wait=delay+Math.floor(Math.random()*Math.min(delay/4,250));
   delay=Math.min(maxRetryMs,delay*2);timer=setTimeout(connect,wait);
  });
 }
 return {
  start(){if(!started&&!stopped){started=true;connect();}},
  stop(){if(stopped)return;stopped=true;clearTimeout(timer);socket?.destroy();socket=null;emit('stopped');},
  passport:()=>Buffer.from(book),
  processId,
 };
}
module.exports={create};
