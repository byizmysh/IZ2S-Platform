// Installed lazily after the original menu. The console runs qualified SDK
// socket/zlib/memcpy routines; no compiled ARM program or new GPU pipeline.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net'),{Worker}=require('node:worker_threads');
const D=require('./block-delta.cjs'),S=require('./pc-screen.cjs'),{programs}=require('./delta-programs.cjs'),code=require('../run13/abi/thread-code.json').functions,defs=require('../run13/abi/ftp-functions.json');
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),signed=x=>Number(BigInt.asIntN(32,x));
function compressionLevelFor({level=1,explicit=false,state,now=Date.now()}){
 if(![0,1,3,6].includes(level))throw Error('Invalid compression level');
 const age=now-Date.parse(state?.at);
 // TV mode alone says nothing about the active network. Only a fresh SDK
 // Ethernet result permits exchanging compression CPU time for cable bandwidth.
 return !explicit&&level===1&&state&&!state.stale&&age>=-1000&&age<6000&&state.network?.connected&&state.network.type==='ethernet'?0:level;
}
function compressor(){let id=0,current,closed=false;const worker=new Worker(path.join(__dirname,'block-delta-worker.cjs'));
 function fail(e){if(current){clearTimeout(current.timer);current.reject(e);current=null;}}
 worker.on('error',fail);worker.on('exit',n=>{closed=true;fail(Error('Delta encoder exited: '+n));});
 worker.on('message',m=>{if(!current||current.id!==m.id)return;const c=current;current=null;clearTimeout(c.timer);if(m.error)c.reject(Error(m.error));else c.resolve({...m,packet:Buffer.from(m.packet.buffer,m.packet.byteOffset,m.packet.byteLength)});});
 return {reset(bytes){if(closed||current)throw Error('Delta encoder unavailable or busy');D.layout(bytes);worker.postMessage({command:'reset',bytes});},commit(slot){if(closed||current)throw Error('Delta encoder unavailable or busy');worker.postMessage({command:'commit',slot});},encode(data){if(closed||current)return Promise.reject(Error('Delta encoder unavailable or busy'));return new Promise((resolve,reject)=>{const frame=Uint8Array.from(data.frame),job={...data,frame,blockBytes:D.BLOCK,id:++id};current={id,resolve,reject,timer:setTimeout(()=>{fail(Error('Delta compression timed out'));worker.terminate();},5000)};worker.postMessage(job,[frame.buffer]);});},async close(){closed=true;fail(Error('Delta encoder closed'));await worker.terminate();}};
}
async function receiver({rpc,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},directory,onInput=()=>{},onPause=()=>{},onEncoded=()=>{},programFactory=programs,consoleState=()=>null,compressionLevel:requestedLevel}){
 const compressionLevel=Number(requestedLevel??process.env.IZ12_STREAM_COMPRESSION_LEVEL??1),explicitCompression=requestedLevel!==undefined||process.env.IZ12_STREAM_COMPRESSION_LEVEL!==undefined;
 if(![0,1,3,6].includes(compressionLevel))throw Error('IZ12_STREAM_COMPRESSION_LEVEL must be 0, 1, 3 or 6');
 console.log('Run10: preparing persistent receiver v2 after the original menu.');
 const phase=await require('./post-menu.cjs').assertReady(rpc,directory),codec=await require('./zlib-sdk.cjs').create({rpc,directory}),sdk=rpc.a.sdk;
 // Decoder inspection is an explicit offline tool; no concurrent probe at stream start.
 let api,base,server,thread=null,p,waiting=null,closing=false,retained=false,closePromise=null,width=0,height=0,currentFormat=-1,next=2,slots,sequence=0,transportError,peer;const sockets=new Set(),queue=[];
 const report={...phase,transport:'SDK DEFLATE dictionary / persistent TCP v2',qualifiedFunctionsOnly:true,compiledArmUpload:false,privateVideoStorage:video.base!==mapped+S.SLOT,protocolRevision:2,maximumSize:[1920,1080],created:new Date().toISOString(),running:false,ready:false};
 const guard=Buffer.alloc(64,0xa7);
 const save=()=>fs.writeFile(path.join(directory,'pc-screen-receiver.json'),JSON.stringify(report,null,2));const zip=compressor();
 function disconnect(error){transportError??=error;report.error=error.message;if(waiting){const w=waiting;waiting=null;w.deadline.clear();w.reject(error);}save().catch(()=>{});}
 async function stopWorker(){
  closing=true;
  if(retained)throw Error('Delta receiver resources retained after incomplete shutdown');
  if(thread!==null){await rpc.write(mapped+S.HEADER+4,Buffer.alloc(4));await rpc.write(mapped+S.HEADER+76,Buffer.alloc(4));const out=Buffer.alloc(8);out.writeBigUInt64LE(BigInt(p.addresses.exit));await rpc.write(mapped+S.HEADER+32,out);}
  for(const socket of sockets)socket.destroy();queue.length=0;
  if(server){server.closeAllConnections?.();await new Promise(resolve=>server.close(resolve));server=null;}
  disconnect(Error('Delta receiver closed'));
  if(thread!==null){let joined=false;try{for(let i=0;i<150;i++){const n=signed((await rpc.callAddress(sdk+code.tryWait.offset,[thread])).result);if(n===1){if((await rpc.callAddress(sdk+code.join.offset,[thread,base+8])).result!==0n)throw Error('Delta receiver join failed');joined=true;break;}if(n!==0)throw Error('Invalid delta thread state');await sleep(20);}}catch(e){report.stopError=e.message;}
   if(!joined){retained=true;report.resourcesRetained=true;await save();throw Error('Delta receiver did not join; allocations retained');}thread=null;
   const fd=(await rpc.read(base+0x2300,4)).readInt32LE();if(fd>=0)await api.call('socketClose',[fd]);
   if(!(await rpc.read(base+0x500000-64,64)).equals(guard)||!(await rpc.read(base+0x500000+D.BLOCK,64)).equals(guard)){retained=true;report.guardViolation=true;await save();throw Error('Delta output guard changed; no further codec calls');}
  }
  report.running=false;report.stopped=new Date().toISOString();await save();width=0;height=0;peer=null;
 }
 async function close(){if(closePromise)return closePromise;closePromise=(async()=>{try{await stopWorker();await codec.close();}finally{await zip.close();}})();return closePromise;}
 async function snapshot(){
  const h=await rpc.read(mapped+S.HEADER,96),t=await rpc.read(p.telemetry,64);
  if(p.timingAddress){const raw=await rpc.read(p.timingAddress+(h.readUInt32LE()&1)*64,40),ticks=Array.from({length:5},(_,i)=>Number(raw.readBigUInt64LE(i*8)));report.decodeTimings={ticks,receiveAndHostWaitMs:(ticks[1]-ticks[0])/31250,inflateMs:(ticks[2]-ticks[1])/31250,crcAndPublicationMs:(ticks[3]-ticks[2])/31250,gpuWaitMs:(ticks[4]-ticks[3])/31250};}
  report.lastState={at:new Date().toISOString(),publication:h.readUInt32LE(),ack:h.readUInt32LE(12),crc:h.readUInt32LE(64),expectedCRC:h.readUInt32LE(72),gpuError:h.readUInt32LE(80),headerExpectedBytes:p.layout.header,bodyExpectedBytes:report.inFlight?.bodyBytes??null,stage:Number(t.readBigUInt64LE()),stageName:['not started','reading header','reading compressed data','inflating blocks','calculating CRC','publishing frame','sending acknowledgement'][Number(t.readBigUInt64LE())]||'unknown',headerReceived:String(t.readBigInt64LE(8)),bodyReceived:String(t.readBigInt64LE(16)),inflateResult:String(t.readBigInt64LE(24)),connectResult:String(t.readBigInt64LE(32)),ackSent:String(t.readBigInt64LE(40)),resetResult:String(t.readBigInt64LE(48)),dictionaryResult:String(t.readBigInt64LE(56))};await save();return report.lastState;
 }
 async function configure(w,h,format){
  if(thread!==null)await stopWorker();if(retained)throw Error('Delta receiver retained');closing=false;transportError=null;
  if(await api.result('netAddress',[api.base+2048]))throw Error('No console IP');const consoleIP=[...await api.read(2048,4)].join('.');report.consoleIP=consoleIP;
  const {routeTo}=require('./platform.cjs').require('core/platform/network.cjs');
  const host=process.env.HB_STREAM_HOST||(await routeTo(consoleIP)).address;
  if(!net.isIPv4(host)||host==='0.0.0.0')throw Error('Invalid PC stream host');
  server=net.createServer({allowHalfOpen:true},socket=>{
   socket.on('error',e=>{if(waiting?.peer===socket)disconnect(e);});
   if(closing||socket.remoteAddress!==consoleIP||sockets.size>=1){socket.destroy();return;}
   socket.setNoDelay(true);sockets.add(socket);queue.push(socket);let received=Buffer.alloc(0);
   socket.on('data',bytes=>{received=Buffer.concat([received,bytes]);if(received.length>396){disconnect(Error('Oversized delta acknowledgement'));socket.destroy();return;}if(received.length===396){const own=waiting;if(!own||own.peer!==socket){disconnect(Error('Unexpected delta acknowledgement'));return;}waiting=null;own.deadline.clear();const ack=Number(received.readBigUInt64LE());const input=received;received=Buffer.alloc(0);if(ack!==own.expected){own.reject(Error('Delta acknowledgement differs'));return;}onInput(input);onPause(false);own.resolve(ack);}});
   socket.on('close',()=>{sockets.delete(socket);const i=queue.indexOf(socket);if(i>=0)queue.splice(i,1);if(!closing)disconnect(Object.assign(Error('Delta persistent socket closed before the next frame'),{code:'FRAME_SOCKET_CLOSED'}));});
  });
  server.on('error',disconnect);await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,host,resolve);});const port=server.address().port;
  const sa=Buffer.alloc(16);sa[0]=16;sa[1]=2;sa.writeUInt16BE(port,2);host.split('.').forEach((n,i)=>sa[4+i]=Number(n));await rpc.write(base+0x2400,sa);
  p=programFactory({base,sdk,mapped,video,width:w,height:h,format,functions:{...api.functions,reset:codec.functions.nnutilZlib_inflateReset,dictionary:codec.functions.nnutilZlib_inflateSetDictionary,inflate:codec.functions.nnutilZlib_inflate,crc32:codec.functions.nnutilZlib_crc32},strm:codec.strm});
  await rpc.write(base+D.STAGING,D.seed(base,p.bytes));await rpc.write(base+0x500000-64,Buffer.concat([guard,Buffer.alloc(D.BLOCK),guard]));await rpc.write(base+0x2000,p.constants);await rpc.write(base+0x2600,p.output);await rpc.write(base+0x2800,p.stages);await rpc.write(p.telemetry,Buffer.alloc(64));if(p.timingAddress)await rpc.write(p.timingAddress,Buffer.alloc(128));await rpc.write(base+0x2300,Buffer.from([255,255,255,255,0,0,0,0]));
  // Each host dictionary belongs to one alternating inactive GPU slot.
  zip.reset(p.bytes);slots=[Buffer.alloc(p.bytes),Buffer.alloc(p.bytes)];for(let i=0;i<2;i++)await rpc.write(video.base+i*video.stride,slots[i]);
  for(const f of p.frames){await rpc.write(f.at,f.frame);await rpc.write(f.snapshot,f.frame);}await rpc.write(p.addresses.exit,p.exit);await rpc.write(mapped+S.HEADER,p.header);
  if(!(await rpc.read(p.frames[0].snapshot,p.frames[0].frame.length)).equals(p.frames[0].frame))throw Error('Delta snapshot readback differs');
  if((await rpc.callAddress(sdk+code.create.offset,[base,0,sdk+code.longjmp.offset,p.addresses.boot])).result!==0n)throw Error('Delta pthread create failed');thread=(await rpc.read(base,8)).readBigUInt64LE();
  width=w;height=h;currentFormat=format;next=2;sequence=0;report.running=true;report.ready=true;report.base=base;report.width=w;report.height=h;report.format=S.formatName(format);report.bytes=p.bytes;report.port=port;report.blocks=p.layout.count;report.thread=String(thread);delete report.error;delete report.inputFailure;await save();
 }
 try{
  api=await require('../run13/abi/client.cjs').create(rpc,{...Object.fromEntries(['socket','recv','send','socketClose','netAddress'].map(n=>[n,defs[n]])),connect:require('../run13/abi/connect-function.json')},0x10000);
  if(process.env.IZ12_STREAM_PROFILE==='1'){const tick=require('../run13/abi/timing-code.json').functions.tick;await rpc.request({command:'verify-function',address:sdk+tick.offset,hex:tick.bytes.slice(0,512)});}
  for(const name of ['create','join','tryWait','exit','longjmp','loadTable','call4','saveResult64','sleep']){const f=code[name];await rpc.request({command:'verify-function',address:sdk+f.offset,hex:f.bytes.slice(0,512)});}
  await rpc.request({command:'verify-function',address:sdk+0x586fb0,hex:require('../run13/abi/ipc-functions.json').copy.hex});
  const allocation=await rpc.call(5732112,[rpc.output,4096,D.ALLOCATION]);if(allocation.result!==0n)throw Error('Delta allocation failed');base=Number(allocation.output.readBigUInt64LE());D.validateBase(base,8);
 }catch(e){await zip.close();await codec.close();throw e;}
 return {report,async reset(){
  // Reuse this owned allocation and encoder only after the old native worker
  // joined. Configure resets both dictionaries before accepting a fresh frame.
  if(closePromise)throw Error('Delta receiver is closing');
  await stopWorker();closing=false;transportError=null;report.ready=false;delete report.consoleIP;
 },async prepare(w,h,format=1){if(closing||transportError)throw transportError||Error('Delta receiver is closing');if(thread===null||w!==width||h!==height||format!==currentFormat)await configure(w,h,format);report.warmedAt=new Date().toISOString();await save();},async push(bytes,w,h,metrics=Buffer.alloc(32),format=1){
  if(thread===null||w!==width||h!==height||format!==currentFormat)await configure(w,h,format);
  if(closing||waiting||transportError)throw transportError||Error('Delta receiver is not ready');
  const selectedLevel=compressionLevelFor({level:compressionLevel,explicit:explicitCompression,state:consoleState()});
  const expected=next,slot=expected&1,t=performance.now(),encoded=await zip.encode({frame:bytes,slot,base,metrics,sequence:sequence+1,keyframe:sequence<2,level:selectedLevel});
  const compressionMs=performance.now()-t;
  const until=Date.now()+120000;while(!peer&&!queue.length){if(closing||transportError)throw transportError||Error('Delta transport closed');if(Date.now()>until)throw Error('Console did not connect to post-menu receiver');await sleep(2);}if(!peer)peer=queue.shift();
  const ack=new Promise((resolve,reject)=>{waiting={peer,expected,resolve,reject,deadline:require('./ack-wait.cjs').deadline({onPause:()=>{onPause(true);const pending=waiting;snapshot().then(state=>{if(!pending||waiting!==pending)return;const error=require('./pc-stream-tcp.cjs').frameDiagnosticError(state);if(error){if(error.inputFailure)report.inputFailure=error.inputFailure;disconnect(error);}}).catch(e=>{report.diagnosticError=e.message;save().catch(()=>{});});},onTimeout:()=>disconnect(Error('Delta acknowledgement timeout'))})};});
  report.inFlight={sequence:sequence+1,packetBytes:encoded.packet.length,bodyBytes:encoded.packet.readUInt32LE(20),started:new Date().toISOString()};
  const written=new Promise((resolve,reject)=>peer.write(encoded.packet,e=>e?reject(e):resolve()));
  await Promise.all([ack,written]);zip.commit(slot);sequence++;next=expected===2?3:2;
  onEncoded({payloadBytes:encoded.packet.length,rawFrameBytes:bytes.length,changedBlocks:encoded.changedBlocks,literalBlocks:encoded.literalBlocks,blocks:encoded.blocks,compressionMs:+compressionMs.toFixed(2),workerEncodingMs:+encoded.workerEncodingMs.toFixed(2),compressionLevel:encoded.compressionLevel,compressionMode:encoded.compressionMode,keyframe:encoded.keyframe});return expected;
 },snapshot,close};
}
function uploader(options){
 const metrics={transport:'SDK DEFLATE dictionary / persistent TCP v2',compressedPayloadBytes:0,rawPayloadBytes:0};let stream;
 stream=require('./pc-stream-tcp.cjs').uploader({...options,save:async s=>options.save?.(Object.assign(s,metrics,{uploadedBytes:metrics.compressedPayloadBytes})),receiverFactory:args=>receiver({...args,consoleState:options.consoleState,onEncoded:r=>{metrics.compressedPayloadBytes+=r.payloadBytes;metrics.rawPayloadBytes+=r.rawFrameBytes;Object.assign(metrics,{lastWireBytes:r.payloadBytes,compressionRatio:+(r.rawFrameBytes/r.payloadBytes).toFixed(2),compressionMs:r.compressionMs,workerEncodingMs:r.workerEncodingMs,compressionQueueAndTransferMs:+Math.max(0,r.compressionMs-r.workerEncodingMs).toFixed(2),compressionLevel:r.compressionLevel,compressionMode:r.compressionMode,changedBlocks:r.changedBlocks,literalBlocks:r.literalBlocks,blocks:r.blocks,keyframe:r.keyframe});Object.assign(stream.stats,metrics);}})});
 Object.assign(stream.stats,metrics);const push=stream.push.bind(stream);stream.push=async(...args)=>{const result=await push(...args);stream.stats.uploadedBytes=metrics.compressedPayloadBytes;return {...result,...metrics,uploadedBytes:metrics.compressedPayloadBytes};};return stream;
}
module.exports={receiver,uploader,compressor,compressionLevelFor};
