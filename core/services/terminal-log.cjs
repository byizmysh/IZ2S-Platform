// Complete each bounded logical line before redaction, so credentials split
// across chunks never enter the transcript. Overlong lines are omitted whole.
const path=require('node:path'),{StringDecoder}=require('node:string_decoder'),logs=require('./rotating-log.cjs');
function redact(text){return text.replace(/(https?:\/\/[^\s#]+\/#)[a-f0-9]{32,64}/gi,'$1[redacted]').replace(/(["'])[a-f0-9]{32,64}\1/gi,'"[redacted]"').replace(/((?:token|secret|pairing key)\s*[:=]\s*)[a-f0-9]{16,64}/gi,'$1[redacted]');}
function create(directory,{maxBytes=logs.MAX_BYTES,archives=logs.ARCHIVES,lineBytes=64*1024,queueBytes=256*1024,append=logs.append,warn=()=>process.stderr.write('Terminal transcript incomplete; the server remains active.\n')}={}){
 logs.policy({maxBytes,archives});
 if(!Number.isInteger(lineBytes)||lineBytes<16||!Number.isInteger(queueBytes)||queueBytes<128)throw new RangeError('Invalid terminal transcript limits');
 const file=path.join(directory,'server-console.log'),channels=new Map(),queue=[];
 const status={pendingBytes:0,droppedLines:0,droppedChannelChunks:0,oversizedLines:0,writeErrors:0};
 let closed=false,warned=false,running=null,closing=null,overflow=false;
 function notify(){if(!warned){warned=true;try{warn();}catch{}}}
 const format=(channel,text)=>new Date().toISOString()+' ['+channel+'] '+text+'\n';
 function start(){
  if(running)return;
  running=(async()=>{
   while(queue.length){
    const entry=queue.shift();
    try{await append(file,entry.text,{maxBytes,archives,mode:0o600});}catch{status.writeErrors++;notify();}
    status.pendingBytes-=entry.bytes;
    if(overflow&&status.pendingBytes<queueBytes/2){overflow=false;enqueue('terminal','[omitted: transcript queue capacity exceeded]');}
   }
  })().finally(()=>{running=null;if(queue.length)start();});
 }
 function enqueue(channel,text){
  const value=format(channel,text),bytes=Buffer.byteLength(value);
  if(bytes>maxBytes||status.pendingBytes+bytes>queueBytes){status.droppedLines++;overflow=true;notify();return;}
  queue.push({text:value,bytes});status.pendingBytes+=bytes;start();
 }
 function consume(channel,state,text){
  let at=0;
  while(at<text.length){
   const newline=text.indexOf('\n',at),end=newline<0?text.length:newline,part=text.slice(at,end);
   if(!state.discarding){
    const bytes=Buffer.byteLength(part);
    if(state.bytes+bytes>lineBytes){state.tail='';state.bytes=0;state.discarding=true;status.oversizedLines++;enqueue(channel,'[omitted: logical line exceeded transcript capacity]');}
    else{state.tail+=part;state.bytes+=bytes;}
   }
   if(newline<0)break;
   if(!state.discarding)enqueue(channel,redact(state.tail.replace(/\r$/,'')));
   state.tail='';state.bytes=0;state.discarding=false;at=newline+1;
  }
 }
 function write(channel,bytes){
  if(closed)return;
  channel=String(channel).slice(0,48);if(!channels.has(channel)&&channels.size>=16){status.droppedChannelChunks++;notify();return;}
  let state=channels.get(channel);if(!state){state={decoder:new StringDecoder('utf8'),tail:'',bytes:0,discarding:false};channels.set(channel,state);}
  const source=Buffer.isBuffer(bytes)?bytes:Buffer.from(String(bytes));
  for(let at=0;at<source.length;at+=16384)consume(channel,state,state.decoder.write(source.subarray(at,at+16384)));
 }
 async function drain(){while(running)await running;}
 function close(){
  if(closing)return closing;closed=true;
  for(const[channel,state]of channels){consume(channel,state,state.decoder.end());if(state.tail&&!state.discarding)enqueue(channel,redact(state.tail));state.tail='';state.bytes=0;}
  closing=drain();return closing;
 }
 return {write,close,drain,status,get bufferedBytes(){let bytes=0;for(const state of channels.values())bytes+=state.bytes;return bytes;}};
}
module.exports={create,redact};
