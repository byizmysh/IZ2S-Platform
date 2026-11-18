'use strict';
const assert=require('node:assert/strict'),http=require('node:http'),fs=require('node:fs/promises'),path=require('node:path');
const {FILE,FORMAT}=require('./host-gateway.cjs'),{MAX}=require('./gateway-rpc.cjs');
function validate(channel){const url=new URL(channel?.url);assert(channel.format===FORMAT&&url.protocol==='http:'&&url.hostname==='127.0.0.1'&&url.port&&url.pathname==='/'&&!url.search&&!url.hash&&!url.username&&!url.password&&/^[a-f0-9]{64}$/.test(channel.token)&&/^[a-f0-9]{32}$/.test(channel.instance),'Invalid local application gateway');return url;}
function uncertain(error){return Object.assign(Error('Local application request completion is unknown; resources retained',{cause:error}),{code:'IZ2S_GATEWAY_UNCERTAIN',resourcesRetained:true,requestReplayed:false});}
function binary(text,n){assert(typeof text==='string'&&text.length===Math.ceil(n/3)*4);const b=Buffer.from(text,'base64');assert(b.length===n&&b.toString('base64')===text);}
function integer(text){assert(typeof text==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(text)&&BigInt(text)<=0xffffffffffffffffn);}
function validateReply(request,row){
 assert(row&&typeof row==='object');
 if(request.command==='read')binary(row.data,request.length);
 else if(request.command==='read-many'){assert(Array.isArray(row.parts)&&row.parts.length===request.spans.length);row.parts.forEach((part,i)=>binary(part,request.spans[i].length));}
 else if(request.command==='query'){integer(row.address);integer(row.size);assert(Number.isInteger(row.permissions));}
 else if(request.command==='write')assert(row.written===true);
 else if(request.command==='verify-function')assert(row.verified===true);
 else if(request.command==='haptics'){assert(['0','3'].includes(row.result)&&Number.isInteger(row.nativeResult)&&row.nativeResult>=0&&row.nativeResult<=0xffffffff&&Number.isInteger(row.completedSteps)&&row.completedSteps>=0&&row.completedSteps<=request.steps.length);binary(row.actual,16);binary(row.afterStop,16);}
 else if(request.command==='udp'){assert(['0','3','4'].includes(row.result)&&Number.isInteger(row.nativeResult)&&row.nativeResult>=0&&row.nativeResult<=0xffffffff&&Number.isInteger(row.polls)&&row.polls>=0&&row.polls<=Math.ceil(request.timeout/5)+2&&typeof row.socketClosed==='boolean'&&typeof row.data==='string');const n=Buffer.from(row.data,'base64').length;assert(n<=1024&&(row.result==='0'||n===0)&&(row.result==='3'||row.socketClosed));binary(row.data,n);}
 else if(request.command==='read-file'){integer(row.size);integer(row.offset);assert(['0','3'].includes(row.result)&&Number.isInteger(row.nativeResult)&&row.nativeResult>=0&&row.nativeResult<=0xffffffff&&typeof row.fileClosed==='boolean'&&row.offset===String(request.offset)&&BigInt(row.size)<=0x7fffffffffffffffn&&typeof row.data==='string');const n=Buffer.from(row.data,'base64').length;assert(n<=request.length&&(row.result==='0'?row.fileClosed&&row.nativeResult===0&&BigInt(row.size)>=BigInt(row.offset)+BigInt(n):n===0&&row.nativeResult!==0));binary(row.data,n);}
 else if(request.command==='pair'){assert(Array.isArray(row.results)&&row.results.length===request.calls.length);row.results.forEach(integer);binary(row.output,32768);}
 else if(['call','call-address','call-extended','fill','copy','write-call'].includes(request.command)){integer(row.result);if(request.command==='write-call')assert(row.written===true);else binary(row.output,32768);}
 else assert.fail('Unexpected application gateway reply');
}
function send(channel,endpoint,row){
 const url=validate(channel),body=JSON.stringify(row,(_,n)=>typeof n==='bigint'?n.toString():n);assert(Buffer.byteLength(body)<=12*1024*1024,'Local application request is too large');
 return new Promise((resolve,reject)=>{
  let received=false;const req=http.request({hostname:url.hostname,port:url.port,path:endpoint,method:'POST',headers:{authorization:'Bearer '+channel.token,'x-iz-gateway':channel.instance,'content-type':'application/json','content-length':Buffer.byteLength(body)}},res=>{
   const parts=[];let length=0;res.on('data',part=>{length+=part.length;if(length>12*1024*1024){res.destroy();return;}parts.push(part);});
   res.on('error',e=>reject(uncertain(e)));res.on('end',()=>{received=true;try{const reply=JSON.parse(Buffer.concat(parts));if(res.statusCode!==200)reject(Object.assign(Error(reply.error||'Local application request refused'),Object.fromEntries(['code','resourcesRetained','nativeRequestSent','generationRevoked','requestReplayed','nativeStatus','menuBusy','nativeClaimRejected','framesNotAccepted','nativeGenerationReturned','nativeMappingRejected','nativeMappingError'].filter(k=>reply[k]!==undefined).map(k=>[k,reply[k]]))));else resolve(reply);}catch(error){reject(uncertain(error));}});
   res.on('close',()=>{if(!received)reject(uncertain(Error('Local application response closed')));});
  });req.on('error',e=>reject(uncertain(e)));req.end(body);
 });
}
function app(channel){
 validate(channel);assert(Number.isInteger(channel.epoch)&&channel.epoch>0&&channel.a?.sdk&&channel.a?.scratch&&[4,8].includes(channel.nativeArgumentLimit));assert(channel.serviceProfiles===undefined||Array.isArray(channel.serviceProfiles)&&channel.serviceProfiles.length<=1&&channel.serviceProfiles.every(name=>name==='sdk'),'Invalid SDK service profile');const cap={...channel,a:{...channel.a}};let sequence=0,tail=Promise.resolve(),closed=false,fault=null;
 function available(){if(fault)throw uncertain(fault);if(closed)throw Object.assign(Error('Application gateway view is retired'),{code:'IZ2S_RPC_GENERATION_REVOKED',nativeRequestSent:false,requestReplayed:false});}
 function request(row){const task=tail.then(async()=>{available();try{const result=await send(cap,'/rpc',{epoch:cap.epoch,sequence:++sequence,request:row});try{validateReply(row,result);}catch(error){throw uncertain(error);}return result;}catch(error){if(error.resourcesRetained)fault=error;if(error.code==='IZ2S_RPC_GENERATION_REVOKED')closed=true;throw error;}});tail=task.catch(()=>{});return task;}
 const reply=row=>({result:BigInt(row.result),output:Buffer.from(row.output||'','base64')}),api={a:Object.freeze({...cap.a}),output:cap.output,epoch:cap.epoch,serviceProfiles:Object.freeze([...(cap.serviceProfiles||[])]),generationGuarded:true,nativeArgumentLimit:cap.nativeArgumentLimit,request,
  async getGraphicsMemoryProperties(){available();binary(cap.graphicsMemoryProperties,520);const bytes=Buffer.from(cap.graphicsMemoryProperties,'base64');assert(bytes.readUInt32LE()>=1&&bytes.readUInt32LE()<=32,'The supervisor has no verified graphics memory properties');return bytes;},
  get uncertain(){return !!fault;},
  async read(address,n){available();assert(Number.isInteger(n)&&n>=0&&n<=64*1024*1024);const out=[];for(let at=0;at<n;at+=MAX){const length=Math.min(MAX,n-at),row=await request({command:'read',address:address+at,length}),bytes=Buffer.from(row.data,'base64');out.push(bytes);}return Buffer.concat(out);},
  async readMany(spans){return (await request({command:'read-many',spans})).parts.map(data=>Buffer.from(data,'base64'));},
  async query(address){const row=await request({command:'query',address});return {...row,address:BigInt(row.address),size:BigInt(row.size)};},
  async write(address,bytes){available();assert(Buffer.isBuffer(bytes)&&bytes.length<=64*1024*1024);for(let at=0;at<bytes.length;at+=MAX)assert((await request({command:'write',address:address+at,data:bytes.subarray(at,at+MAX).toString('base64')})).written);},
  async call(offset,args=[]){return reply(await request({command:'call',offset,args}));},async callAddress(address,args=[]){return reply(await request({command:'call-address',address,args}));},async callExtended(address,args=[]){return reply(await request({command:'call-extended',address,args}));},
  async haptics(options){const row=await request({command:'haptics',handles:options.handles,steps:options.steps});return {...row,result:BigInt(row.result),actual:Buffer.from(row.actual,'base64'),afterStop:Buffer.from(row.afterStop,'base64')};},
  async udp(destination,payload,{timeout=1000}={}){require('../host/network.cjs').endpoint(destination);assert(Buffer.isBuffer(payload)&&payload.length>=1&&payload.length<=1024&&Number.isInteger(timeout)&&timeout>=1&&timeout<=5000,'Invalid native UDP request');const row=await request({command:'udp',destination,data:payload.toString('base64'),timeout});return {...row,result:BigInt(row.result),data:Buffer.from(row.data,'base64')};},
  async readFile(path,offset=0,length=require('../host/files.cjs').MAX){require('../host/files.cjs').validate(path,offset,length);const row=await request({command:'read-file',path,offset,length});return {...row,result:BigInt(row.result),size:BigInt(row.size),offset:BigInt(row.offset),data:Buffer.from(row.data,'base64')};},
  async pair(calls){const row=await request({command:'pair',calls});return {results:row.results.map(BigInt),output:Buffer.from(row.output,'base64')};},
  async writeCall(address,bytes,functionAddress,args=[]){assert(Buffer.isBuffer(bytes)&&bytes.length<=32768);const row=await request({command:'write-call',address,data:bytes.toString('base64'),functionAddress,args});return {result:BigInt(row.result)};},
  async fill(address,value,length){return reply(await request({command:'fill',address,value,length}));},async copy(destination,source,length){return reply(await request({command:'copy',destination,source,length}));},
  revoke(){closed=true;},inspect(){return {epoch:cap.epoch,closed,uncertain:!!fault,generationGuarded:true,nativeArgumentLimit:cap.nativeArgumentLimit,nativeOwnershipReleased:false};}
 };return Object.freeze(api);
}
async function connect(channel){
 if(typeof channel==='string')channel=JSON.parse(await fs.readFile(path.join(path.resolve(channel),FILE)));validate(channel);const credentials={...channel},hello=await send(credentials,'/control',{action:'hello'});assert(hello.instance===channel.instance&&hello.format===FORMAT);let sequence=hello.sequence,cancelSequence=hello.cancelSequence??0,tail=Promise.resolve(),cancelTail=Promise.resolve(),fault;
 function control(action,fields={}){const task=tail.then(async()=>{if(fault)throw uncertain(fault);try{return await send(credentials,'/control',{...fields,action,sequence:++sequence});}catch(error){if(error.resourcesRetained)fault=error;throw error;}});tail=task.catch(()=>{});return task;}
 function cancel(epoch,errorCode=0){const task=cancelTail.then(async()=>{if(fault)throw uncertain(fault);try{return await send(credentials,'/cancel',{action:'cancel-native',epoch,errorCode,sequence:++cancelSequence});}catch(error){if(error.resourcesRetained&&error.code!=='IZ2S_GATEWAY_UNCERTAIN')fault=error;throw error;}});cancelTail=task.catch(()=>{});return task;}
 return Object.freeze({control,cancel,status:()=>send(credentials,'/control',{action:'hello'}),app});
}
module.exports={connect,app};
