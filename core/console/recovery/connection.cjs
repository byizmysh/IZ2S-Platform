'use strict';
const {EventEmitter}=require('node:events'),assert=require('node:assert/strict');
const W=require('./wire.cjs'),J=require('./journal.cjs');
const Routes=require('./routes.cjs');
const {Connection}=require('../../../engines/izmysh/server/lib/connection.cjs');
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});promise.catch(()=>{});return {promise,resolve,reject};}
function receipt(frame,session){
 assert(frame.type===W.TYPES.RECEIPT&&frame.payload.length===64,'Expected console receipt');const b=frame.payload,sequence=Number(b.readBigUInt64LE()),state=['idle','executing','completed'][b.readUInt32LE(8)],responseBytes=b.readUInt32LE(12);
 assert(Number.isSafeInteger(sequence)&&frame.sequence===BigInt(sequence)&&state&&b.subarray(56).every(v=>v===0),'Invalid console receipt');assert(responseBytes===(state==='completed'?J.RESPONSE_BYTES:0),'Invalid cached response length');
 return {session,sequence,state,hash:b.subarray(16,48).toString('hex'),executions:b.readBigUInt64LE(48)};
}
async function peer(socket,credentials,timeoutMs,{beforeProof=()=>{}}={}){
 const connection=new Connection(socket,timeoutMs),raw=async()=>{const h=await connection.read(W.HEADER),n=W.size(h);return n===W.HEADER?h:Buffer.concat([h,await connection.read(n-W.HEADER)]);};
 try{const challenge=await raw();assert(challenge.length===W.HEADER+32);const auth={session:Buffer.from(credentials.session,'hex'),key:Buffer.from(credentials.key,'hex'),nonce:Buffer.from(challenge.subarray(W.HEADER))};const hello=W.decode(challenge,auth);assert(hello.type===W.TYPES.CHALLENGE&&hello.sequence===0n);beforeProof();await connection.write(W.encode({type:W.TYPES.PROOF,...auth}));const read=async()=>W.decode(await raw(),auth),send=(type,sequence=0,payload=Buffer.alloc(0))=>connection.write(W.encode({type,sequence,payload,...auth}));const first=receipt(await read(),credentials.session);return {connection,first,read,send,close:()=>connection.close()};}catch(e){connection.close();throw e;}
}
class RecoveryConnection extends EventEmitter{
 constructor({journal,credentials,candidates,connect,timeoutMs=120000,routeTimeoutMs=2000,backoff,onState=()=>{},pingMs=5000}){
  super();assert(/^[a-f0-9]{32}$/.test(credentials.session)&&/^[a-f0-9]{64}$/.test(credentials.key));this.socket=this;this.journal=journal;this.credentials=credentials;this.timeout=timeoutMs;this.routeTimeout=routeTimeoutMs;this.pingMs=pingMs;this.onState=onState;this.suspended=true;this.current=null;this.link=null;this.reading=false;this.ready=deferred();this.recovered=[];
  this.candidates=candidates;this.connect=connect||Routes.dial;this.probing=null;this.replacement=null;
  this.routes=Routes.create({candidates,connect,timeoutMs:routeTimeoutMs,backoff,onState:event=>this.state(event.state,event),verify:async(socket,endpoint)=>{const p=await peer(socket,credentials,routeTimeoutMs);try{const action=await journal.reconcile(p.first);assert(!p.connection.error&&!socket.destroyed);return {peer:p,action};}catch(e){p.close();throw e;}}});
 }
 state(state,details={}){try{this.onState({...details,state,at:new Date().toISOString()});}catch{}}
 start(){if(!this.running){this.running=this.drive().catch(e=>this.fail(e));this.running.catch(()=>{});}return this.ready.promise;}
 wake(){this.routes.wake();this.workWake?.();}
 probeRoute({preferred,force=false}={}){
  this.wake();if(this.probing)return this.probing;
  if(this.error||!this.link||this.replacement)return Promise.resolve(false);
  const sameEndpoint=preferred&&preferred.address===this.remoteAddress&&preferred.port===this.remotePort;
  if(!force&&(!preferred||sameEndpoint&&(preferred.mode===this.mode||this.mode==='unknown'))){
   // The first signed announcement can identify an already authenticated IP
   // as LAN/Wi-Fi. Learning its type alone must not replace a live socket.
   if(sameEndpoint&&this.mode==='unknown'&&['lan','wifi'].includes(preferred.mode))this.mode=preferred.mode;
   return Promise.resolve(false);
  }
  const previous=this.link;
  this.probing=this.probe(previous,{preferred,force}).catch(e=>{this.state('route-probe-failed',{reason:e.message});return false;}).finally(()=>{this.probing=null;this.workWake?.();});return this.probing;
 }
 async probe(previous,{preferred,force}){
  let rows=Routes.endpoints(await this.candidates());
  if(preferred)rows=rows.filter(e=>e.address===preferred.address&&e.port===preferred.port&&e.mode===preferred.mode);
  else rows.sort((a,b)=>Number(a.address===this.remoteAddress)-Number(b.address===this.remoteAddress));
  for(const endpoint of rows){
   if(this.error||this.link&&this.link!==previous)return false;
   const abort=this.probeAbort=new AbortController();let socket,p,cancel;
   const dispose=()=>{p?.close();socket?.destroy();};
   const cancelled=new Promise((resolve,reject)=>{cancel=()=>{dispose();reject(Error('Recovery route probe cancelled or timed out'));};abort.signal.addEventListener('abort',cancel,{once:true});});
   const timer=setTimeout(()=>abort.abort(),this.routeTimeout);
   try{
    this.state('probing-route',{address:endpoint.address,mode:endpoint.mode});
    const work=(async()=>{
     socket=await this.connect(endpoint,{signal:abort.signal,timeoutMs:this.routeTimeout});if(abort.signal.aborted){dispose();throw Error('Recovery route probe cancelled');}
     p=await peer(socket,this.credentials,this.routeTimeout,{beforeProof:()=>assert(!this.error&&!abort.signal.aborted&&(!this.link||this.link===previous),'Recovery route changed during authentication')});
     if(this.error||abort.signal.aborted){dispose();throw Error('Recovery route probe cancelled');}return p;
    })();
    const authenticated=await Promise.race([work,cancelled]);
    assert(!authenticated.connection.error&&!socket.destroyed,'Authenticated route already closed');
    // Receipt reconciliation belongs to the one serialized drive loop. The
    // probe sends no SDK command and cannot mutate the durable request journal.
    this.replacement={peer:authenticated,endpoint};this.state('route-probe-authenticated',{address:endpoint.address,mode:endpoint.mode});previous.close();this.workWake?.();return true;
   }catch(e){dispose();if(!this.error)this.state('route-probe-unavailable',{address:endpoint.address,mode:endpoint.mode,reason:e.message});}
   finally{clearTimeout(timer);abort.signal.removeEventListener('abort',cancel);if(this.probeAbort===abort)this.probeAbort=null;}
  }return false;
 }
 async workWait(ms){if(this.error)return;await new Promise(resolve=>{const finish=()=>{clearTimeout(timer);if(this.workWake===finish)this.workWake=null;resolve();},timer=setTimeout(finish,ms);this.workWake=finish;});}
 async complete(p,frame){assert(frame.type===W.TYPES.RESPONSE&&frame.sequence===BigInt(p.sequence),'Response belongs to a different command');const response=await this.journal.commit({...p,response:frame.payload});
  if(this.current?.pending?.sequence===p.sequence){this.current.output=response;this.current.written.resolve();this.current.result.resolve(response);}
  else{this.recovered.push({sequence:p.sequence,hash:p.hash,response});this.state('recovered-unclaimed-result',{sequence:p.sequence});}
 }
 async pending(p,initial={action:'send-proven-unexecuted-request'}){
  const link=this.link;link.connection.timeout=this.timeout;let action=initial;
  for(;;){
   if(action.action==='wait-for-completion'){
    this.state('waiting-for-command-completion',{sequence:p.sequence});await this.workWait(1000);if(this.error)throw this.error;
    await link.send(W.TYPES.PING);action=await this.journal.reconcile(receipt(await link.read(),this.credentials.session));continue;
   }
   assert(['send-proven-unexecuted-request','recover-cached-response'].includes(action.action),'Unexpected request receipt');
   await link.send(W.TYPES.REQUEST,p.sequence,Buffer.from(p.packet,'base64'));if(this.current?.pending?.sequence===p.sequence)this.current.written.resolve();const frame=await link.read();
   if(frame.type===W.TYPES.RESPONSE){await this.complete(p,frame);return;}
   action=await this.journal.reconcile(receipt(frame,this.credentials.session));
  }
 }
 async drive(){
  while(!this.error){
   if(!this.link){
    // A probe may have authenticated exactly as the old socket failed. Wait
    // for it before starting another handshake against the same console.
    if(this.probing)await this.probing;if(this.error)break;
    let connected;
    if(this.replacement){const {peer:p,endpoint}=this.replacement;this.replacement=null;try{const action=await this.journal.reconcile(p.first);assert(!p.connection.error);connected={session:{peer:p,action},endpoint};this.routes.remember(endpoint);}catch(e){p.close();throw e;}}
    else connected=await this.routes.recover();
    if(this.error){connected.session.peer.close();break;}this.link=connected.session.peer;this.initialAction=connected.session.action;this.remoteAddress=connected.endpoint.address;this.remotePort=connected.endpoint.port;this.mode=connected.endpoint.mode;this.suspended=true;this.state('synchronizing',{address:this.remoteAddress,mode:this.mode});
   }
   try{
    const saved=this.journal.snapshot().pending;
    if(saved){if(this.current&&!this.current.pending&&J.digest(this.current.packet)===saved.hash)this.current.pending=saved;await this.pending(saved,this.initialAction);}
    this.initialAction=null;this.suspended=false;this.ready.resolve(this);this.state('connected',{address:this.remoteAddress,mode:this.mode});
    if(this.current&&!this.current.output){if(!this.current.pending)this.current.pending=await this.journal.prepare(this.current.packet);await this.pending(this.current.pending);continue;}
    await this.workWait(this.pingMs);if(this.error)break;if(this.current&&!this.current.output)continue;
    this.link.connection.timeout=this.routeTimeout;await this.link.send(W.TYPES.PING);const action=await this.journal.reconcile(receipt(await this.link.read(),this.credentials.session));assert(action.action==='ready','Idle receipt differs from committed state');
   }catch(e){
    if(this.error)break;if(this.journal.uncertain)throw e;
    this.suspended=true;this.link?.close();this.link=null;await this.journal.suspend();this.state('waiting-for-network',{reason:e.message});
   }
  }
 }
 write(packet){
  if(this.error)return Promise.reject(this.error);if(!Buffer.isBuffer(packet)||packet.length!==J.REQUEST_BYTES)return Promise.reject(Error('Invalid recovery request'));
  // NativeClient.stop's zero packet closes its transport; it is not an SDK call.
  if(packet.subarray(0,80).every(v=>v===0))return Promise.resolve();
  if(this.current)return Promise.reject(Error('A native recovery request is already pending'));
  const row=this.current={packet:Buffer.from(packet),written:deferred(),result:deferred(),pending:null,output:null};this.start();this.wake();return row.written.promise;
 }
 read(size){if(this.error)return Promise.reject(this.error);if(size!==J.RESPONSE_BYTES||!this.current||this.reading)return Promise.reject(Error('Invalid recovery response boundary'));this.reading=true;const row=this.current;return row.result.promise.then(bytes=>{assert(this.current===row);this.current=null;return Buffer.from(bytes);}).finally(()=>{this.reading=false;this.wake();});}
 fail(error){if(this.error)return;this.error=error;this.destroyed=true;this.suspended=true;this.routes.stop();this.probeAbort?.abort();this.replacement?.peer.close();this.replacement=null;this.link?.close();this.link=null;this.current?.written.reject(error);this.current?.result.reject(error);this.ready.reject(error);this.workWake?.();this.state('closed',{reason:error.message});this.emit('close');}
 close(){this.fail(Error('Connection closed'));}
}
async function open(options){const journal=options.journal||await J.open({directory:options.directory,session:options.credentials.session});return new RecoveryConnection({...options,journal});}
module.exports={open,RecoveryConnection,receipt,peer};
