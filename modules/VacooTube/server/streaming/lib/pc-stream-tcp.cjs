// Dedicated frame transport. Only qualified socket/thread/memcpy functions run
// on the console. GPU acknowledgement releases the next receive frame.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net');
const {build}=require('../run13/abi/frames.cjs');
const code=require('../run13/abi/thread-code.json').functions;
const defs=require('../run13/abi/ftp-functions.json');
const S=require('./pc-screen.cjs');
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),signed=n=>Number(BigInt.asIntN(32,n));
function programs({base,sdk,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},fd,width,height,format=1,functions,directToMapped=false}){
 if(directToMapped)throw Error('Direct GPU receive is disabled');
 const bytes=S.frameBytes(width,height,format);if(bytes>video.capacity)throw Error('Frame exceeds video storage');const packetBytes=bytes+32,selector=mapped+S.HEADER+32,result=mapped+S.HEADER+64;
 const addresses={boot:base+0x10000,zero:base+0x50000,one:base+0x90000,wait:base+0xd0000,exit:base+0x110000};
 if(new Set(Object.values(addresses).map(p=>Math.floor(p/2**32))).size!==1)throw Error('Receiver frames cross a pointer-word boundary');
 const constants=Buffer.alloc(40);constants.writeBigUInt64LE(2n);constants.writeBigUInt64LE(3n,8);constants.writeBigUInt64LE(BigInt(addresses.wait),16);constants.writeBigUInt64LE(2n,24);constants.writeBigUInt64LE(3n,32);
 const copy=sdk+0x586fb0,jump=sdk+code.longjmp.offset,qualified=new Set(Object.values(functions));
 function make(name,slot,ack){
  const at=addresses[name],snapshot=at+0x200000,calls=[];
  const add=(fn,args)=>calls.push({function:fn,args:[...args,...Array(4-args.length).fill(0)]});
  if(ack!==null){
   add(copy,[base+0x2100,base+0x2000+(ack===2?0:8),8]);
   add(copy,[base+0x2108,mapped+0x7f0280,320]);
   add(copy,[base+0x2248,mapped+0x700004,4]);
   add(copy,[base+0x224c,mapped+S.HEADER+96,8]);add(copy,[base+0x2254,mapped+128,56]);
   add(functions.send,[fd,base+0x2100,396,0]);
  }
  add(copy,[selector,base+0x2010,8]);
  const staging=base+0x320000;
  const receive=calls.length;add(functions.recv,[fd,directToMapped?video.base+slot*video.stride:staging,packetBytes,0x40]);
  const errno=calls.length;add(functions.errno,[]);
  if(!directToMapped){add(copy,[video.base+slot*video.stride,staging+32,bytes]);add(copy,[mapped+S.HEADER+104,staging,32]);}
  // The result is stored in mapped storage before the publication word.
  add(copy,[mapped+S.HEADER,base+0x2018+slot*8,4]);
  const restore={function:copy,args:[at,snapshot,0,0]};calls.push(restore,{function:jump,args:[{u64:selector},1,0,0]});
  let p=build(at,sdk,calls,code,false,qualified);restore.args[2]=p.frame.length;p=build(at,sdk,calls,code,false,qualified);
  const i=p.proof.logicalResultIndices[receive];p.frame.writeBigUInt64LE(BigInt(result),p.proof.stackOffset+i*0x140+0x118);
  const e=p.proof.logicalResultIndices[errno];p.frame.writeBigUInt64LE(BigInt(mapped+S.HEADER+88),p.proof.stackOffset+e*0x140+0x118);
  if(p.frame.length>0x40000)throw Error('Receiver frame exceeds owned slot');
  return {...p,at,snapshot,slot,ack,receive,calls};
 }
 const waiterCalls=[{function:sdk+code.sleep.offset,args:[500000,0,0,0]},{function:copy,args:[addresses.wait,addresses.wait+0x200000,0,0]},{function:jump,args:[{u64:selector},1,0,0]}];
 let wait=build(addresses.wait,sdk,waiterCalls,code,false,qualified);waiterCalls[1].args[2]=wait.frame.length;wait=build(addresses.wait,sdk,waiterCalls,code,false,qualified);
 const exit=Buffer.alloc(0xb0);exit.writeBigUInt64LE(BigInt(sdk+code.exit.offset),0x58);exit.writeBigUInt64LE(BigInt(base+0x180000),0x68);
 const header=Buffer.alloc(96);header.writeUInt32LE(width,4);header.writeUInt32LE(height,8);header.writeUInt32LE(format,24);
 header.writeBigUInt64LE(BigInt(addresses.wait),32);header.writeBigUInt64LE(BigInt(addresses.zero),40);header.writeBigUInt64LE(BigInt(addresses.one),48);header.writeBigUInt64LE(BigInt(addresses.exit),56);header.writeUInt32LE(packetBytes,72);header.writeUInt32LE(1,76);
 return {addresses,constants,header,exit,bytes,packetBytes,frames:[make('boot',0,null),make('zero',0,3),make('one',1,2),{...wait,at:addresses.wait,snapshot:addresses.wait+0x200000}]};
}
async function receiver({rpc,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},directory,directToMapped=false,onInput=()=>{},onPause=()=>{}}){
 const names=['socket','bind','listen','getsockname','accept','recv','send','socketClose','errno','netAddress'];
 const api=await require('../run13/abi/client.cjs').create(rpc,Object.fromEntries(names.map(n=>[n,defs[n]])),0x10000),sdk=rpc.a.sdk;
 for(const name of ['create','join','tryWait','exit','longjmp','loadTable','call4','saveResult64','sleep']){const f=code[name];await rpc.request({command:'verify-function',address:sdk+f.offset,hex:f.bytes.slice(0,512)});}
 await rpc.request({command:'verify-function',address:sdk+0x586fb0,hex:require('../run13/abi/ipc-functions.json').copy.hex});
 const allocation=await rpc.call(5732112,[rpc.output,4096,0x500000]);if(allocation.result!==0n)throw Error('Receiver allocation failed');const base=Number(allocation.output.readBigUInt64LE());
 if(!Number.isSafeInteger(base)||!base||base%4096)throw Error('Invalid receiver allocation');
 const report={session:rpc.a.session,base,mapped,qualifiedFunctionsOnly:true,staging:directToMapped?'GPU mapping':'ordinary heap',created:new Date().toISOString()};
 const save=()=>fs.writeFile(path.join(directory,'pc-screen-receiver.json'),JSON.stringify(report,null,2));
 let fd=-1,listener=-1,peer,thread=null,p,ackBytes=Buffer.alloc(0),waiting=null,width=0,height=0,currentFormat=-1,closing=false,retained=false,closingPromise=null;
 async function call(name,args){const n=signed(await api.call(name,args));if(n<0)throw Error(name+' errno '+await api.result('errno'));return n;}
 let transportError;
 function disconnect(error){
  transportError??=error;
  if(!closing){report.paused=true;report.transportError=error.message;onPause(true);save().catch(()=>{});}
  if(waiting){const w=waiting;waiting=null;w.deadline.clear();w.reject(error);}
 }
 async function doClose(){
  if(retained)throw Error('Receiver retained after incomplete shutdown; no further native cleanup');
  closing=true;if(thread!==null){const pointer=Buffer.alloc(8);pointer.writeBigUInt64LE(BigInt(p.addresses.exit));try{await rpc.write(mapped+S.HEADER+32,pointer);}catch(e){report.stopError=e.message;}}
  peer?.destroy();disconnect(Error('Frame transport closed'));
  if(thread!==null){let joined=false;try{for(let i=0;i<100;i++){const rc=signed((await rpc.callAddress(sdk+code.tryWait.offset,[thread])).result);if(rc===1){if((await rpc.callAddress(sdk+code.join.offset,[thread,base+8])).result!==0n)throw Error('Receiver join failed');joined=true;break;}if(rc!==0)throw Error('Receiver state invalid');await sleep(20);}}catch(e){report.stopError=e.message;}
   if(!joined){retained=true;report.resourcesRetained=true;await save();throw Error('Receiver did not join; buffers retained');}thread=null;
  }
  if(fd>=0){await api.call('socketClose',[fd]);fd=-1;}if(listener>=0){await api.call('socketClose',[listener]);listener=-1;}
  const ack=await rpc.read(mapped+S.HEADER+12,4);await rpc.write(mapped+S.HEADER,ack);await rpc.write(mapped+S.HEADER+76,Buffer.alloc(4));report.running=false;report.stopped=new Date().toISOString();await save();width=0;height=0;
 }
 function close(){if(closingPromise)return closingPromise;closingPromise=doClose().finally(()=>closingPromise=null);return closingPromise;}
 async function configure(w,h,format){
  if(retained)throw Error('Receiver resources retained; restart the session');if(thread!==null)await close();closing=false;transportError=null;width=w;height=h;
  if(await api.result('netAddress',[api.base+2048]))throw Error('No console IP');const host=[...await api.read(2048,4)].join('.');
  listener=await call('socket',[2,1,6]);const sa=Buffer.alloc(16);sa[0]=16;sa[1]=2;await api.write(2048,sa);
  await call('bind',[listener,api.base+2048,16]);await call('listen',[listener,1]);await api.write(2080,Buffer.from([16,0,0,0]));await call('getsockname',[listener,api.base+2048,api.base+2080]);const port=(await api.read(2048,16)).readUInt16BE(2);
  peer=net.createConnection({host,port});peer.setNoDelay(true);peer.on('error',e=>disconnect(e));peer.on('close',()=>disconnect(Object.assign(Error('Console frame socket closed'),{code:'FRAME_SOCKET_CLOSED'})));
  await new Promise((resolve,reject)=>{const timer=setTimeout(()=>{peer.destroy();reject(Error('Frame socket connect timeout'));},3000);peer.once('connect',()=>{clearTimeout(timer);resolve();});peer.once('error',e=>{clearTimeout(timer);reject(e);});});
  await api.write(2080,Buffer.from([16,0,0,0]));fd=await call('accept',[listener,api.base+2112,api.base+2080]);
  const accepted=await api.read(2112,16);if([...accepted.subarray(4,8)].join('.')!==peer.localAddress||accepted.readUInt16BE(2)!==peer.localPort)throw Error('Frame socket peer differs from this PC');
  await api.call('socketClose',[listener]);listener=-1;
  ackBytes=Buffer.alloc(0);peer.on('data',bytes=>{ackBytes=Buffer.concat([ackBytes,bytes]);while(ackBytes.length>=396){const packet=ackBytes.subarray(0,396),ack=Number(packet.readBigUInt64LE());ackBytes=ackBytes.subarray(396);onInput(packet);if(waiting){const w=waiting;waiting=null;w.deadline.clear();if(report.paused){report.paused=false;report.resumed=new Date().toISOString();onPause(false);save().catch(()=>{});}if(ack===w.expected)w.resolve();else w.reject(Error('Frame acknowledgement differs'));}}});
  p=programs({base,sdk,mapped,video,fd,width:w,height:h,format,functions:api.functions,directToMapped});currentFormat=format;
  await rpc.write(base+0x2000,p.constants);for(const f of p.frames){await rpc.write(f.at,f.frame);await rpc.write(f.snapshot,f.frame);}await rpc.write(p.addresses.exit,p.exit);await rpc.write(mapped+S.HEADER,p.header);
  if(!(await rpc.read(p.frames[0].snapshot,p.frames[0].frame.length)).equals(p.frames[0].frame))throw Error('Receiver snapshot readback differs');
  const created=await rpc.callAddress(sdk+code.create.offset,[base,0,sdk+code.longjmp.offset,p.addresses.boot]);if(created.result!==0n)throw Error('Receiver pthread create failed');thread=(await rpc.read(base,8)).readBigUInt64LE();
  report.running=true;report.width=w;report.height=h;report.format=S.formatName(format);report.bytes=p.bytes;report.thread=String(thread);report.consoleIP=host;report.port=port;report.started=new Date().toISOString();await save();
 }
 let next=2;
 return {async push(bytes,w,h,metrics,format=1){
  if(width!==w||height!==h||currentFormat!==format||thread===null){try{await configure(w,h,format);next=2;}catch(e){try{await close();}catch{}throw e;}}
  if(transportError)throw transportError;
  if(closing||waiting)throw Error('Receiver is not ready');const expected=next;
  const ack=new Promise((resolve,reject)=>{waiting={expected,resolve,reject,deadline:require('./ack-wait.cjs').deadline({onPause:()=>{report.paused=true;report.pauseStarted=new Date().toISOString();onPause(true);console.log('Console frame ACK paused; waiting up to 120 seconds for HOME return.');save().catch(()=>{});},onTimeout:()=>{disconnect(Error('GPU frame acknowledgement timed out after HOME resume grace'));peer.destroy();}})};});
  const written=new Promise((resolve,reject)=>peer.write(Buffer.concat([metrics||Buffer.alloc(32),bytes]),e=>{if(e){disconnect(e);reject(e);}else resolve();}));await Promise.all([ack,written]);next=next===2?3:2;return expected;
 },async snapshot(){const h=await rpc.read(mapped+S.HEADER,96);report.lastState={publication:h.readUInt32LE(),ack:h.readUInt32LE(12),received:String(h.readBigInt64LE(64)),expected:h.readUInt32LE(72),gpuError:h.readUInt32LE(80),socketErrno:h.readUInt32LE(88)};await save();return report.lastState;},close,report};
}
// Inspect only completed recv calls. A blocked MSG_WAITALL still exposes the
// previous return count, so stage 1/2 cannot prove a body/header truncation.
// Expected lengths come from this PC's encoded in-flight packet, never a
// partially received console header. A genuine full-packet CRC failure is fatal.
function frameDiagnosticError(state){
 const stage=Number(state?.stage);
 for(const [part,minimumStage]of [['header',2],['body',3]]){
  if(!Number.isInteger(stage)||stage<minimumStage)continue;
  const raw=state?.[part+'Received'],expected=state?.[part+'ExpectedBytes'];
  if(raw===undefined||raw===null||raw==='')continue;
  const received=Number(raw),bounded=Number.isSafeInteger(expected)&&expected>0&&expected<=0x100000;
  if(!Number.isSafeInteger(received))continue;
  if(received<0||(bounded&&received<expected)){
   const error=Object.assign(Error(`Delta frame ${part} input ended before completion (${received}${bounded?' / '+expected:''} bytes); waiting for a fresh frame`),{code:'FRAME_INPUT_TRUNCATED',inputFailure:{part,received,expected:bounded?expected:null,stage,sourceEOF:received===0,sourceError:received<0}});
   return error;
  }
  if(bounded&&received>expected)return Object.assign(Error('Delta frame receive length exceeds the encoded packet'),{code:'FRAME_INPUT_BOUNDS'});
 }
 if(state?.gpuError)return Object.assign(Error('Delta frame checksum rejected by GPU; no frame replay'),{code:'FRAME_CHECKSUM_REJECTED'});
 return null;
}
function uploader({rpc,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},directory,save=async()=>{},onInput=()=>{},onPause=()=>{},onFreshness=async()=>{},controlsEnabled=()=>false,consoleState=()=>null,receiverFactory=receiver}){
 let native,busy=false,last=null,failure=null,lastSave=0,closing=false;const attempts=[];
 let lastNetworkType,lastDisplayMode,configured=null,freshnessEpoch=0,gateVisible=null,gateBarrier=Promise.resolve();
 const stats={format:'BC1-opaque',transport:'dedicated TCP / GPU acknowledgement',frames:0,unchanged:0,uploadedBytes:0,started:new Date().toISOString()};
 function gate(visible,reason){
  if(gateVisible===visible)return gateBarrier;
  gateVisible=visible;gateBarrier=gateBarrier.then(async()=>{await onFreshness(visible,reason);stats.videoVisible=visible;stats.freshnessAt=new Date().toISOString();});return gateBarrier;
 }
 async function dark(reason){
  freshnessEpoch++;stats.freshnessEpoch=freshnessEpoch;stats.awaitingFreshFrame=true;stats.freshnessReason=reason;
  try{await gate(false,reason);}catch(e){failure=e.message;stats.freshnessError=e.message;stats.error=e.message;throw Object.assign(Error(e.message),{code:'FRAME_GATE_FAILED'});}
 }
 function observe(){const state=consoleState();return state&&!state.stale?{mode:['docked','handheld'].includes(state.mode)?state.mode:null,network:state.network?.connected?state.network:null}:{mode:null,network:null};}
 function transition({mode,network}){
  if(!native)return null;
  if(lastDisplayMode&&mode&&mode!==lastDisplayMode)return Object.assign(Error('Console display mode changed'),{code:'FRAME_DISPLAY_CHANGED'});
  if(network&&((net.isIPv4(network.ip)&&native.report?.consoleIP&&network.ip!==native.report.consoleIP)||(lastNetworkType&&network.type&&network.type!=='unknown'&&network.type!==lastNetworkType)))return Object.assign(Error('Console network adapter changed'),{code:'FRAME_NETWORK_CHANGED'});
  return null;
 }
 function shapeChanged(w,h,format){return !configured||configured.w!==w||configured.h!==h||configured.format!==format;}
 async function ensureNative(){native??=await receiverFactory({rpc,mapped,video,directory,onInput,onPause:value=>{if(value&&!stats.paused)dark('console-paused').catch(()=>{});stats.paused=value;onPause(value);save(stats).catch(()=>{});}});return native;}
 return {stats,invalidate:reason=>dark(reason||'source-interrupted'),consoleAddress:()=>native?.report?.consoleIP||null,async prepare(w,h,format=1){
  S.dimensions(w,h,format);if(S.frameBytes(w,h,format)>video.capacity)throw Error('Frame exceeds video storage');if(busy)throw Error('Frame upload already in progress');if(failure)throw Error(failure);if(closing)throw Error('Frame transport is closing');busy=true;
  try{let reconfigured=shapeChanged(w,h,format);if(reconfigured)await dark('preparing-video');
   if(stats.paused||stats.waitingForMainConnection){const status=await rpc.request({command:'status'});if(status.suspended)throw Object.assign(Error('Waiting for the console network adapter before preparation'),{code:'FRAME_RECONNECT_READY',recoverable:true,retryAfterMs:1000});if(!status.ready)throw Error('Main console connection is unavailable');
    if(stats.waitingForMainConnection){if(native?.reset)await native.reset();else if(native){await native.close();native=null;}configured=null;last=null;lastNetworkType=null;lastDisplayMode=null;reconfigured=true;stats.waitingForMainConnection=false;stats.reconnects=(stats.reconnects||0)+1;}
   }
   const receiver=await ensureNative();if(!receiver.prepare)return {prepared:false};await receiver.prepare(w,h,format);configured={w,h,format};if(reconfigured){const observed=observe();lastDisplayMode=observed.mode;if(observed.network?.type&&observed.network.type!=='unknown')lastNetworkType=observed.network.type;}stats.preparedAt=new Date().toISOString();await save(stats);return {prepared:true,width:w,height:h,format:S.formatName(format)};}
  catch(e){if(e.recoverable){await save(stats);throw e;}failure=e.message;stats.error=failure;if(e.code==='FRAME_GATE_FAILED')throw e;try{await native?.close();}catch(cleanup){stats.recoveryError=cleanup.message;}await save(stats);throw e;}finally{busy=false;}
 },async push(bytes,w,h,format=1,metrics){
  if(bytes.length>video.capacity)throw Error('Frame exceeds video storage');if(bytes.length!==S.frameBytes(w,h,format))throw Error('Direct transport needs a complete frame');if(busy)throw Error('Frame upload already in progress');if(failure)throw Error(failure);busy=true;
  try{
   stats.requests=(stats.requests||0)+1;stats.lastInputAt=new Date().toISOString();
   const observed=observe(),{network}=observed,changed=transition(observed);if(changed)throw changed;
   // A still image must continue through recovery. Otherwise its first failed
   // upload leaves every subsequent identical frame skipped forever.
   if(!stats.paused&&!stats.awaitingFreshFrame&&!stats.waitingForMainConnection&&!controlsEnabled()&&last&&last.w===w&&last.h===h&&last.format===format&&last.bytes.equals(bytes)){stats.unchanged++;if(Date.now()-lastSave>1000){await save(stats);lastSave=Date.now();}return {skipped:true,...stats};}
   if(closing)throw Error('Frame transport is closing');
   if(shapeChanged(w,h,format))await dark('configuring-video');await gateBarrier;
   await ensureNative();lastDisplayMode??=observed.mode;const ownEpoch=freshnessEpoch,start=performance.now(),ack=await native.push(bytes,w,h,metrics,format);
   configured={w,h,format};const after=transition(observe());if(after)throw after;
   // HOME may pause an already in-flight frame. Its late ACK must not expose
   // an old capture; the next freshly captured/verified frame releases black.
   await gateBarrier;
   if(!closing&&ownEpoch===freshnessEpoch){await gate(true,'fresh-frame-acknowledged');if(ownEpoch===freshnessEpoch){stats.awaitingFreshFrame=false;delete stats.freshnessReason;}}
   stats.frames++;stats.acknowledged=ack;stats.uploadedBytes+=bytes.length;stats.width=w;stats.height=h;stats.format=S.formatName(format);stats.lastBytes=bytes.length;stats.lastMs=+(performance.now()-start).toFixed(2);stats.lastAt=new Date().toISOString();last={w,h,format,bytes};
   if(network?.type&&network.type!=='unknown')lastNetworkType=network.type;if(observed.mode)lastDisplayMode=observed.mode;
   if(!stats.awaitingFreshFrame&&stats.paused){stats.paused=false;stats.resumed=new Date().toISOString();onPause(false);}
   if(stats.frames===1||Date.now()-lastSave>1000){await save(stats);lastSave=Date.now();}return {...stats};
  }catch(e){
   if(e.code==='FRAME_GATE_FAILED')throw e;await dark(e.message);
   let cleanupAttempted=false;
   // Rebuild only the video receiver, and only after its previous thread has
   // joined. A reset main/RPC session is never revived or replayed here.
   const recoverable=['ECONNRESET','ECONNABORTED','EPIPE','ETIMEDOUT','ENETUNREACH','EHOSTUNREACH','FRAME_SOCKET_CLOSED','FRAME_NETWORK_CHANGED','FRAME_DISPLAY_CHANGED','FRAME_INPUT_TRUNCATED'].includes(e.code);
   if(recoverable&&native&&!closing&&attempts.filter(t=>Date.now()-t<30000).length<3){
    stats.paused=true;onPause(true);
    try{
     const status=await rpc.request({command:'status'});
     if(status.suspended){stats.waitingForMainConnection=true;await save(stats);throw Object.assign(Error('Waiting for the console network adapter'),{code:'FRAME_RECONNECT_READY',recoverable:true,retryAfterMs:1000});}
     if(!status.ready){cleanupAttempted=true;throw Error('Main console connection is unavailable');}
     attempts.push(Date.now());stats.waitingForMainConnection=false;
     cleanupAttempted=true;
     if(native.reset)await native.reset();else {await native.close();native=null;} // Never repeat an incomplete join.
     last=null;lastNetworkType=null;lastDisplayMode=null;configured=null;stats.reconnects=(stats.reconnects||0)+1;stats.awaitingFreshFrame=true;stats.lastTransportError=e.message;if(e.inputFailure)stats.lastInputFailure=e.inputFailure;delete stats.error;await save(stats);
     throw Object.assign(Error('Video connection interrupted; waiting for a fresh frame'),{code:'FRAME_RECONNECT_READY',recoverable:true,retryAfterMs:750});
    }catch(recovery){
     if(recovery.code==='FRAME_RECONNECT_READY')throw recovery;
     stats.recoveryError=recovery.message;
    }
   }
   failure=e.message;stats.error=failure;await save(stats);if(!cleanupAttempted){try{await native?.snapshot();await native?.close();}catch{}}throw e;
  }finally{busy=false;}
 },async close(){closing=true;await dark('video-closed');await native?.close();native=null;last=null;configured=null;}};
}
module.exports={programs,receiver,uploader,frameDiagnosticError};
