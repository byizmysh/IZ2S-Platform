// Separate authenticated application telemetry. It never restores native RPC,
// accepts commands, reads remote memory or changes launcher execution state.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net'),crypto=require('node:crypto');
const {readJson,saveJson}=require('../ledger.cjs');
const passport=require('./passport.cjs'),wire=require('./wire.cjs');
const PRIVATE='app-recovery-private.json',PUBLIC='app-recovery-state.json';
async function create({directory,host='127.0.0.1',port,heartbeatMs=1000,timeoutMs=5000,onEvent=()=>{}}){
 if(!directory||!net.isIPv4(host)||!Number.isInteger(heartbeatMs)||heartbeatMs<10||!Number.isInteger(timeoutMs)||timeoutMs<heartbeatMs*2)throw Error('Invalid application recovery settings');
 await fs.mkdir(directory,{recursive:true,mode:0o700});
 const file=path.join(directory,PRIVATE),saved=await readJson(file);
 const credentials=saved||{version:1,session:crypto.randomBytes(16).toString('hex'),secret:crypto.randomBytes(32).toString('hex')};
 if(credentials.version!==1||!/^[a-f0-9]{32}$/.test(credentials.session)||!/^[a-f0-9]{64}$/.test(credentials.secret))throw Error('Invalid recovery credentials');
 const secret=Buffer.from(credentials.secret,'hex');
 const previous=saved?await readJson(path.join(directory,PUBLIC)):null;
 if(previous&&previous.session!==credentials.session)throw Error('Recovery ledger session mismatch');
 const state={version:1,channel:'application-status',session:credentials.session,state:'waiting-for-client',
  automaticReconnect:true,nativeControlAvailable:false,consoleIntegrationVerified:false,application:previous?.application||null,
  reconnects:previous?.reconnects||0,receivedAt:previous?.receivedAt||null,endpoint:null};
 let active=null,closed=false,serial=Promise.resolve(),failure=null;
 const peers=new Set();
 const publish=async()=>{state.updated=new Date().toISOString();await saveJson(path.join(directory,PUBLIC),state);};
 const enqueue=job=>{const task=serial.then(job);serial=task.catch(error=>{if(error.code!=='ERR_RECOVERY_PEER')failure=error;});return task;};
 const rejectPeer=message=>Object.assign(Error(message),{code:'ERR_RECOVERY_PEER'});
 const emit=(name)=>{try{onEvent({state:name,at:new Date().toISOString()});}catch{}};
 const server=net.createServer(socket=>{
  if(closed||peers.size>=4){socket.destroy();return;}
  const peer={socket,nonce:null,busy:false,timer:null};peers.add(peer);socket.setNoDelay(true);socket.setTimeout(timeoutMs,()=>socket.destroy());
  const challenge=()=>{if(socket.destroyed||closed)return;peer.nonce=crypto.randomBytes(32);socket.write(wire.frame(wire.CHALLENGE,peer.nonce));};
  socket.on('error',()=>{});
  socket.on('close',()=>{
   peers.delete(peer);clearTimeout(peer.timer);
   enqueue(async()=>{
    if(active!==peer)return;
    active=null;
    if(!closed){state.state='waiting-for-network';await publish();emit(state.state);}
   }).catch(()=>{});
  });
  const parse=wire.parser((type,payload)=>{
   if(type!==wire.PROOF||!peer.nonce||peer.busy)throw Error('Unexpected application recovery proof');
   const nonce=peer.nonce;peer.nonce=null;
   if(!wire.verifyProof(secret,nonce,payload))throw Error('Invalid application authentication');
   const application=passport.decode(payload.subarray(0,passport.BYTES));
   if(application.session!==credentials.session)throw Error('Wrong application session');
   peer.busy=true;
   enqueue(async()=>{
    if(closed||socket.destroyed)return;
    const old=state.application;
    if(old){
     if(old.process!==application.process)throw rejectPeer('Application process was restarted');
     if(application.sequence<old.sequence||application.heartbeat<old.heartbeat||application.completed<old.completed)throw rejectPeer('Application status moved backwards');
     if(application.sequence===old.sequence&&JSON.stringify(application)!==JSON.stringify(old))throw rejectPeer('Changed application status without a commit');
    }
    const attached=active!==peer;
    if(attached){
     if(old)state.reconnects++;
     const replaced=active;active=peer;replaced?.socket.destroy();
    }
    state.application=application;state.state='connected';state.clientAuthenticated=true;state.receivedAt=new Date().toISOString();
    // Persist before ACK: an acknowledgement never precedes its durable ledger.
    await publish();
    socket.write(wire.frame(wire.ACK,wire.ack(secret,nonce,application.sequence)));
    peer.busy=false;peer.timer=setTimeout(challenge,heartbeatMs);
    emit(attached?(old?'reconnected':'connected'):'status');
   }).catch(()=>socket.destroy());
  });
  socket.on('data',bytes=>{try{parse(bytes);}catch{socket.destroy();}});
  challenge();
 });
 const chosen=port??saved?.endpoint?.port??0;
 if(!Number.isInteger(chosen)||chosen<0||chosen>65535)throw Error('Invalid recovery port');
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(chosen,host,()=>{server.removeListener('error',reject);resolve();});});
 server.on('error',error=>{failure=error;});
 state.endpoint={address:host,port:server.address().port};
 try{
  await saveJson(file,{...credentials,endpoint:state.endpoint});
  await fs.chmod(file,0o600);
  await publish();
 }catch(error){for(const p of peers)p.socket.destroy();await new Promise(resolve=>server.close(resolve));throw error;}
 return {
  endpoint:state.endpoint,
  credentials:{session:credentials.session,secret:credentials.secret},
  snapshot:()=>structuredClone(state),
  async close(){
   if(closed)return;closed=true;
   for(const peer of peers){clearTimeout(peer.timer);peer.socket.destroy();}
   await new Promise(resolve=>server.close(resolve));await serial;
   state.state='stopped';await publish();
  },
  get error(){return failure;},
 };
}
function publicState(value,now=Date.now()){
 if(!value)return null;
 const stamp=Date.parse(value.receivedAt||'');
 const stale=!Number.isFinite(stamp)||now-stamp>6000||stamp>now+6000;
 return {...value,stale,nativeControlAvailable:false,state:value.state==='connected'&&stale?'waiting-for-network':value.state};
}
module.exports={create,publicState,PRIVATE,PUBLIC};
