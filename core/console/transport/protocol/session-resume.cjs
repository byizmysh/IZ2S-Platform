// Optional protocol v2: reconnect the SAME process, with a console receipt for
// the last executed request. An ambiguous native operation is never replayed.
const {EventEmitter}=require('node:events');
const {REQUEST_BYTES,RESPONSE_BYTES}=require('../server/lib/native.cjs');
const RESUME_MAGIC=0x32534d52;
class SessionConnection extends EventEmitter{
 constructor({graceMs=120000,onState=()=>{}}={}){
  super();this.socket=this;this.timeout=graceMs;this.graceMs=graceMs;this.onState=onState;this.sequence=0;this.generation=0;this.suspended=false;
 }
 state(name,extra={}){try{this.onState({state:name,at:new Date().toISOString(),sequence:this.sequence,...extra});}catch{}}
 async attach(transport,{sequence,cached}){
  if(this.error)throw this.error;
  if(this.transport)throw Error('A console transport is already attached');
  const current=this.current;
  if(current&&Date.now()>=current.expires){const e=Error('Native request reconnect budget expired; it was not replayed');this.fail(e);throw e;}
  let action=1;
  if(current&&sequence===current.sequence){if(!cached)throw Error('Console receipt has no saved result');action=2;}
  else if(sequence!==this.sequence)throw Error('Console receipt differs from the pending session');
  this.transport=transport;const generation=++this.generation;
  transport.socket.once('close',()=>this.suspend(transport,transport.error||Error('Console transport closed')));
  try{
   const approval=Buffer.alloc(8);approval.writeBigUInt64LE(BigInt(action));await transport.write(approval);
   if(action===1&&current)await transport.write(current.packet);
  }catch(e){this.suspend(transport,e);return;}
  if(generation!==this.generation||this.transport!==transport)return;
  clearTimeout(this.resumeTimer);this.resumeTimer=null;this.suspended=false;this.state('connected',{receipt:sequence,cachedResult:action===2});
  if(this.reader&&this.current){clearTimeout(this.readTimer);this.readTimer=setTimeout(()=>this.fail(Error('Console response timed out; native request was not replayed')),Math.max(1,Math.min(this.timeout,this.current.expires-Date.now())));}
  current?.resolveWrite();this.pump();
 }
 suspend(transport,error){
  if(this.error||this.transport!==transport)return;
  this.transport=null;this.generation++;this.suspended=true;clearTimeout(this.readTimer);this.state('waiting-for-dock',{reason:error.message});
  this.resumeTimer??=setTimeout(()=>this.fail(Error('Console reconnect grace expired; native request was not replayed')),Math.max(1,Math.min(this.graceMs,this.current?this.current.expires-Date.now():this.graceMs)));
  transport.close();
 }
 write(bytes){
  if(this.error)return Promise.reject(this.error);
  if(!Buffer.isBuffer(bytes)||bytes.length!==REQUEST_BYTES)return Promise.reject(Error('Invalid resumable request size'));
  if(bytes.subarray(0,40).equals(Buffer.alloc(40))){const t=this.transport;return t?t.write(bytes):Promise.resolve();}
  if(this.current)return Promise.reject(Error('A resumable native request is already pending'));
  const packet=Buffer.from(bytes),sequence=this.sequence+1;packet.writeBigUInt64LE(BigInt(sequence),80);
  return new Promise((resolve,reject)=>{
   let resolved=false;const resolveWrite=()=>{if(!resolved){resolved=true;resolve();}};
   const current=this.current={sequence,packet,expires:Date.now()+this.timeout+this.graceMs,resolveWrite,rejectWrite:reject};
   const t=this.transport;if(!t)return;
   t.write(packet).then(()=>{if(this.current===current)resolveWrite();},e=>this.suspend(t,e));
  });
 }
 read(size){
  if(this.error)return Promise.reject(this.error);
  if(size!==RESPONSE_BYTES||!this.current||this.reader)return Promise.reject(Error('Invalid resumable response boundary'));
  return new Promise((resolve,reject)=>{
   this.reader={resolve,reject};this.readTimer=setTimeout(()=>this.fail(Error('Console response timed out; native request was not replayed')),this.timeout);this.pump();
  });
 }
 pump(){
  const t=this.transport,reader=this.reader,current=this.current,generation=this.generation;
  if(!t||!reader||!current||this.pumping===generation)return;
  this.pumping=generation;t.timeout=this.timeout;
  t.read(RESPONSE_BYTES).then(bytes=>{
   if(this.error||this.transport!==t||this.generation!==generation||this.reader!==reader)return;
   clearTimeout(this.readTimer);this.pumping=null;this.current=null;this.reader=null;this.sequence=current.sequence;reader.resolve(bytes);
  },error=>this.suspend(t,error));
 }
 fail(error){
  if(this.error)return;this.error=error;clearTimeout(this.readTimer);clearTimeout(this.resumeTimer);
  const t=this.transport;this.transport=null;t?.close();this.current?.rejectWrite(error);this.reader?.reject(error);this.reader=null;this.state('closed',{reason:error.message});this.emit('close');
 }
 close(){this.fail(Error('Connection closed'));}
}
function receipt(bytes){
 if(bytes.length!==32||bytes.readBigUInt64LE(16)!==BigInt(RESUME_MAGIC)||bytes.readBigUInt64LE(24)!==0n)throw Error('Invalid reconnect receipt');
 const sequence=bytes.readBigUInt64LE(),cached=bytes.readBigUInt64LE(8);
 if(sequence>BigInt(Number.MAX_SAFE_INTEGER)||cached>1n||cached!==BigInt(sequence>0n))throw Error('Invalid reconnect sequence');
 return {sequence:Number(sequence),cached:cached===1n};
}
module.exports={SessionConnection,receipt,RESUME_MAGIC};
