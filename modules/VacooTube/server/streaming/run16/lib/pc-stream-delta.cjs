// Installed lazily after the original menu. The console runs qualified SDK
// socket/zlib/memcpy routines; no compiled ARM program or new GPU pipeline.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net');
const D=require('./block-delta.cjs'),S=require('./pc-screen.cjs'),{programs}=require('./delta-programs.cjs'),code=require('./platform.cjs').require('core/console/abi/thread-code.json').functions,defs=require('./platform.cjs').require('core/console/abi/ftp-functions.json');
const sleep=ms=>new Promise(r=>setTimeout(r,ms)),signed=x=>Number(BigInt.asIntN(32,x));
function freshConsoleNetwork(state,now=Date.now()){
 const at=Date.parse(state?.at),age=now-at;
 return state?.stale===false&&Number.isFinite(age)&&age>=0&&age<=6000&&state.network?.connected===true&&net.isIPv4(state.network.ip)&&state.network.ip!=='0.0.0.0'?state.network:null;
}
function compressionLevelFor({level=1,explicit=false,state=null,policy='auto',now=Date.now()}={}){
 if(![0,1,3,6].includes(level))throw Error('Invalid compression level');if(!['auto','on','off'].includes(policy))throw Error('Invalid compression policy');
 if(explicit||level!==1)return level;if(policy==='on')return 1;if(policy==='off')return 0;
 // A measured gigabit route carries 1080p BC1 at 60 Hz without DEFLATE search.
 // Restore the proven LAN stored-block path: byte-identical BC1, cheaper host
 // and console work. TV mode alone never establishes an Ethernet connection.
 return freshConsoleNetwork(state,now)?.type==='ethernet'?0:1;
}
function wirePrefetchEnabled({environment=process.env.IZ2S_WIRE_PREFETCH,policy='auto',renderMode='auto'}={}){
 return environment!=='0'&&(environment==='1'||policy==='off'&&renderMode==='ram');
}
// Wi-Fi look-ahead is an explicit Off + RAM choice, never inferred from TV
// mode. Preserve Ethernet behavior and the single future-packet bound.
function wireRouteAllowed({state,policy='auto',renderMode='auto',effectiveLevel=1,heapDictionary=false,directInflate=true,now=Date.now()}={}){
 const network=freshConsoleNetwork(state,now);return network?.type==='ethernet'||network?.type==='wifi'&&policy==='off'&&renderMode==='ram'&&effectiveLevel===0&&heapDictionary===true&&directInflate===false;
}
// Deltas are calculated before conversion so long uptimes do not lose tick
// precision. Partial snapshots cannot be mistaken for a complete frame record.
function decodeTimings(raw,frequencyHz){
 if(!Buffer.isBuffer(raw)||raw.length!==40||!Number.isSafeInteger(frequencyHz)||frequencyHz<=0||frequencyHz>1e9)throw Error('Invalid decoder timing record');
 const ticks=Array.from({length:5},(_,i)=>raw.readBigUInt64LE(i*8)),complete=ticks[0]>0n&&ticks.every((t,i)=>i===0||t>=ticks[i-1]);
 const result={frequencyHz,ticks:ticks.map(String),complete,scope:'SDK receive including host wait; inflate plus mapped copy; CRC plus publication; publication through GPU ACK and native polling; excludes scanout'};
 if(complete){const ms=i=>Number(ticks[i+1]-ticks[i])*1000/frequencyHz;Object.assign(result,{receiveAndHostWaitMs:ms(0),inflateMs:ms(1),crcAndPublicationMs:ms(2),gpuWaitMs:ms(3)});}
 return result;
}
const {compressor}=require('./delta-compressor.cjs');
// Switch 2's hardware counter can differ from the legacy SDK's constant
// frequency. Compare midpoint-bracketed tick calls only at receiver startup;
// the best RTT from each burst bounds network uncertainty without frame RPC.
function calibrateFrequency(start,end,sdkReportedHz){
 const best=rows=>rows.filter(r=>Number.isFinite(r.before)&&Number.isFinite(r.after)&&r.after>=r.before&&typeof r.tick==='bigint').sort((a,b)=>(a.after-a.before)-(b.after-b.before))[0],a=best(start),b=best(end);
 if(!a||!b)return {valid:false,reason:'No valid clock samples'};
 const elapsedMs=(b.before+b.after-a.before-a.after)/2,uncertaintyMs=(a.after-a.before+b.after-b.before)/2;
 if(elapsedMs<100||elapsedMs>5000||uncertaintyMs/elapsedMs>.03||b.tick<=a.tick)return {valid:false,reason:'Clock sample uncertainty is too large',elapsedMs,uncertaintyMs};
 const measuredHz=Number(b.tick-a.tick)*1000/elapsedMs;
 if(!Number.isFinite(measuredHz)||measuredHz<1e6||measuredHz>1e9)return {valid:false,reason:'Clock estimate is outside supported range',elapsedMs,uncertaintyMs,measuredHz};
 const known=[sdkReportedHz,31250000].find(n=>Number.isSafeInteger(n)&&Math.abs(measuredHz-n)/n<.02),frequencyHz=known||Math.round(measuredHz);
 return {valid:true,frequencyHz,measuredHz:Math.round(measuredHz),elapsedMs,uncertaintyMs,source:known?'startup measurement / recognized counter rate':'startup measurement'};
}
async function receiver({rpc,mapped,video={base:mapped+S.SLOT,stride:S.STRIDE,capacity:S.STRIDE},directory,onInput=()=>{},onPause=()=>{},onEncoded=()=>{},programFactory=programs,consoleState=()=>null,compressionLevel:requestedLevel,compressionPolicy=()=> 'auto',renderMode=()=> 'auto'}){
 const profileDecode=process.env.IZ12_STREAM_PROFILE==='1',wireSetting=process.env.IZ2S_WIRE_PREFETCH;
 const wireEnabled=()=>wirePrefetchEnabled({environment:wireSetting,policy:compressionPolicy(),renderMode:renderMode()});
 const compressionLevel=Number(requestedLevel??process.env.IZ12_STREAM_COMPRESSION_LEVEL??1),explicitCompression=requestedLevel!==undefined||process.env.IZ12_STREAM_COMPRESSION_LEVEL!==undefined;
 if(![0,1,3,6].includes(compressionLevel))throw Error('IZ12_STREAM_COMPRESSION_LEVEL must be 0, 1, 3 or 6');
 console.log('Run10: preparing persistent receiver v3 after the original menu.');
 const phase=await require('./platform.cjs').require('core/services/post-menu.cjs').assertReady(rpc,directory),codec=await require('./zlib-sdk.cjs').create({rpc,directory}),sdk=rpc.a.sdk;
 // Decoder inspection is an explicit offline tool; no concurrent probe at stream start.
 let api,base,server,thread=null,p,waiting=null,closing=false,retained=false,closePromise=null,width=0,height=0,currentFormat=-1,currentRenderMode=null,slots,pipeline,transportError,peer,wire=null,wirePrefetchDisabled=false;const sockets=new Set(),queue=[];
 const report={...phase,transport:'SDK DEFLATE dictionary / persistent TCP v2',qualifiedFunctionsOnly:true,compiledArmUpload:false,privateVideoStorage:video.base!==mapped+S.SLOT,protocolRevision:3,maximumSize:[1920,1080],created:new Date().toISOString(),running:false,ready:false};
 const guard=Buffer.alloc(64,0xa7);
 const acceleration=await require('./fast-compression.cjs').prepare();report.hostCompression=acceleration.backend;if(acceleration.reason)report.hostCompressionFallback=acceleration.reason;
 const save=()=>fs.writeFile(path.join(directory,'pc-screen-receiver.json'),JSON.stringify(report,null,2));const zip=compressor({codecPath:acceleration.path});
 function disconnect(error){transportError??=error;if(wire&&!wire.failed)wire.fail(error);report.error=error.message;if(waiting){const w=waiting;waiting=null;w.deadline.clear();w.reject(error);}save().catch(()=>{});}
 function wireRouteReady(){return wireRouteAllowed({state:consoleState(),policy:compressionPolicy(),renderMode:renderMode(),effectiveLevel:compressionLevelFor({level:compressionLevel,explicit:explicitCompression,state:consoleState(),policy:compressionPolicy()}),heapDictionary:p?.heapDictionary,directInflate:p?.directInflate});}
 async function clearHeapDictionaries(){
  for(let slot=0;slot<2;slot++){const at=base+D.HEAP_DICTIONARY+slot*D.HEAP_STRIDE;await rpc.write(at,Buffer.alloc(D.HEAP_STRIDE));await rpc.write(at-64,guard);await rpc.write(at+D.MAX_FRAME,guard);}
 }
 async function stopWorker(){
  closing=true;wire?.close(Object.assign(Error('Delta wire stopped'),{code:'FRAME_PIPELINE_INVALIDATED',recoverable:true}));wire=null;
  if(retained)throw Error('Delta receiver resources retained after incomplete shutdown');
  if(thread!==null){await rpc.write(mapped+S.HEADER+4,Buffer.alloc(4));await rpc.write(mapped+S.HEADER+76,Buffer.alloc(4));const out=Buffer.alloc(8);out.writeBigUInt64LE(BigInt(p.addresses.exit));await rpc.write(mapped+S.HEADER+32,out);}
  for(const socket of sockets)socket.destroy();queue.length=0;
  if(server){server.closeAllConnections?.();await new Promise(resolve=>server.close(resolve));server=null;}
  disconnect(Error('Delta receiver closed'));
  if(thread!==null){let joined=false;try{for(let i=0;i<150;i++){const n=signed((await rpc.callAddress(sdk+code.tryWait.offset,[thread])).result);if(n===1){if((await rpc.callAddress(sdk+code.join.offset,[thread,base+8])).result!==0n)throw Error('Delta receiver join failed');joined=true;break;}if(n!==0)throw Error('Invalid delta thread state');await sleep(20);}}catch(e){report.stopError=e.message;}
   if(!joined){retained=true;report.resourcesRetained=true;await save();throw Error('Delta receiver did not join; allocations retained');}thread=null;
   const fd=(await rpc.read(base+0x2300,4)).readInt32LE();if(fd>=0)await api.call('socketClose',[fd]);
   if(!(await rpc.read(base+0x500000-64,64)).equals(guard)||!(await rpc.read(base+0x500000+D.BLOCK,64)).equals(guard)){retained=true;report.guardViolation=true;await save();throw Error('Delta output guard changed; no further codec calls');}
   if(p.heapDictionary)for(let slot=0;slot<2;slot++){const at=base+D.HEAP_DICTIONARY+slot*D.HEAP_STRIDE;if(!(await rpc.read(at-64,64)).equals(guard)||!(await rpc.read(at+D.MAX_FRAME,64)).equals(guard)){retained=true;report.guardViolation=true;await save();throw Error('Delta heap dictionary guard changed; resources retained');}}
  }
  report.running=false;report.stopped=new Date().toISOString();await save();width=0;height=0;peer=null;
 }
 async function close(){if(closePromise)return closePromise;closePromise=(async()=>{try{await stopWorker();await codec.close();}finally{try{await pipeline?.close();}finally{await zip.close();}}})();return closePromise;}
 async function snapshot(){
  const h=await rpc.read(mapped+S.HEADER,96),t=await rpc.read(p.telemetry,64);
  if(p.timingAddress){const raw=await rpc.read(p.timingAddress+(h.readUInt32LE()&1)*64,40);report.decodeTimings=decodeTimings(raw,report.tickFrequencyHz);}
  report.lastState={at:new Date().toISOString(),publication:h.readUInt32LE(),ack:h.readUInt32LE(12),crc:h.readUInt32LE(64),expectedCRC:h.readUInt32LE(72),gpuError:h.readUInt32LE(80),headerExpectedBytes:p.layout.header,bodyExpectedBytes:report.inFlight?.bodyBytes??null,stage:Number(t.readBigUInt64LE()),stageName:['not started','reading header','reading compressed data','inflating blocks','calculating CRC','publishing frame','sending acknowledgement'][Number(t.readBigUInt64LE())]||'unknown',headerReceived:String(t.readBigInt64LE(8)),bodyReceived:String(t.readBigInt64LE(16)),inflateResult:String(t.readBigInt64LE(24)),connectResult:String(t.readBigInt64LE(32)),ackSent:String(t.readBigInt64LE(40)),resetResult:String(t.readBigInt64LE(48)),dictionaryResult:String(t.readBigInt64LE(56))};await save();return report.lastState;
 }
 function timingFor(packet){if(!profileDecode)return null;const timing=decodeTimings(packet.subarray(396),report.tickFrequencyHz);timing.frequencySource=report.tickFrequencySource;timing.sdkReportedFrequencyHz=report.sdkReportedTickFrequencyHz;return timing;}
 function inputFor(packet){const timing=timingFor(packet);if(timing)report.decodeTimings=timing;onInput(packet.subarray(0,396));onPause(false);}
 async function configure(w,h,format){
  if(thread!==null)await stopWorker();if(retained)throw Error('Delta receiver retained');closing=false;transportError=null;
  if(await api.result('netAddress',[api.base+2048]))throw Object.assign(Error('Console network adapter has no address yet'),{code:'FRAME_ADAPTER_NOT_READY'});const consoleIP=[...await api.read(2048,4)].join('.');if(consoleIP==='0.0.0.0')throw Object.assign(Error('Console network adapter is waiting for an address'),{code:'FRAME_ADAPTER_NOT_READY'});report.consoleIP=consoleIP;
  const {routeTo}=require('./platform.cjs').require('core/platform/network.cjs');
  const host=process.env.HB_STREAM_HOST||(await routeTo(consoleIP)).address;
  if(!net.isIPv4(host)||host==='0.0.0.0')throw Error('Invalid PC stream host');
  server=net.createServer({allowHalfOpen:true},socket=>{
   socket.on('error',e=>{if(waiting?.peer===socket)disconnect(e);});
   if(closing||socket.remoteAddress!==consoleIP||sockets.size>=1){socket.destroy();return;}
   socket.setNoDelay(true);sockets.add(socket);queue.push(socket);let received=Buffer.alloc(0);const acknowledgementBytes=profileDecode?436:396;
   if(wireEnabled()&&!wirePrefetchDisabled)wire=require('./delta-wire.cjs').create({socket,acknowledgementBytes,onInput:inputFor,onPause:()=>{onPause(true);const own=wire;snapshot().then(state=>{if(wire!==own||own?.failed||closing)return;const error=require('./pc-stream-tcp.cjs').frameDiagnosticError(state);if(error){if(error.inputFailure)report.inputFailure=error.inputFailure;disconnect(error);}}).catch(error=>{report.diagnosticError=error.message;});},onError:disconnect});
   else socket.on('data',bytes=>{received=Buffer.concat([received,bytes]);if(received.length>acknowledgementBytes){disconnect(Error('Oversized delta acknowledgement'));socket.destroy();return;}if(received.length===acknowledgementBytes){const own=waiting;if(!own||own.peer!==socket){disconnect(Error('Unexpected delta acknowledgement'));return;}waiting=null;own.deadline.clear();const ack=Number(received.readBigUInt64LE());const input=received.subarray(0,396),timing=profileDecode?decodeTimings(received.subarray(396),report.tickFrequencyHz):null;received=Buffer.alloc(0);if(ack!==own.expected){own.reject(Error('Delta acknowledgement differs'));return;}if(timing){timing.frequencySource=report.tickFrequencySource;timing.sdkReportedFrequencyHz=report.sdkReportedTickFrequencyHz;report.decodeTimings=timing;}onInput(input);onPause(false);own.resolve(ack);}});
   socket.on('close',()=>{sockets.delete(socket);const i=queue.indexOf(socket);if(i>=0)queue.splice(i,1);if(!closing)disconnect(Object.assign(Error('Delta persistent socket closed before the next frame'),{code:'FRAME_SOCKET_CLOSED'}));});
  });
  server.on('error',disconnect);await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,host,resolve);});const port=server.address().port;
  const sa=Buffer.alloc(16);sa[0]=16;sa[1]=2;sa.writeUInt16BE(port,2);host.split('.').forEach((n,i)=>sa[4+i]=Number(n));await rpc.write(base+0x2400,sa);
  const selectedRenderMode=renderMode(),decodePolicy=require('./inflate-policy.cjs').decodePolicyFor(w,h,format,selectedRenderMode);
  p=programFactory({base,sdk,mapped,video,width:w,height:h,format,...decodePolicy,functions:{...api.functions,reset:codec.functions.nnutilZlib_inflateReset,dictionary:codec.functions.nnutilZlib_inflateSetDictionary,inflate:codec.functions.nnutilZlib_inflate,crc32:codec.functions.nnutilZlib_crc32},strm:codec.strm,profileDecode});
  report.directInflate=p.directInflate;report.heapDictionary=!!p.heapDictionary;report.dictionarySource=p.heapDictionary?'owned RAM mirror per inactive slot':'mapped inactive GPU slot';report.inflateOutput=p.directInflate?'mapped GPU output':'owned RAM block then mapped copy';
  await rpc.write(base+D.STAGING,D.seed(base,p.bytes));await rpc.write(base+0x500000-64,Buffer.concat([guard,Buffer.alloc(D.BLOCK),guard]));await rpc.write(base+0x2000,p.constants);await rpc.write(base+0x2600,p.output);await rpc.write(base+0x2800,p.stages);await rpc.write(p.telemetry,Buffer.alloc(64));if(p.timingAddress)await rpc.write(p.timingAddress,Buffer.alloc(128));await rpc.write(base+0x2300,Buffer.from([255,255,255,255,0,0,0,0]));
  // Each host dictionary belongs to one alternating inactive GPU slot.
  await pipeline.reset(p.bytes);if(p.heapDictionary)await clearHeapDictionaries();slots=[Buffer.alloc(p.bytes),Buffer.alloc(p.bytes)];for(let i=0;i<2;i++)await rpc.write(video.base+i*video.stride,slots[i]);
  for(const f of p.frames){await rpc.write(f.at,f.frame);await rpc.write(f.snapshot,f.frame);}await rpc.write(p.addresses.exit,p.exit);await rpc.write(mapped+S.HEADER,p.header);
  if(!(await rpc.read(p.frames[0].snapshot,p.frames[0].frame.length)).equals(p.frames[0].frame))throw Error('Delta snapshot readback differs');
  if((await rpc.callAddress(sdk+code.create.offset,[base,0,sdk+code.longjmp.offset,p.addresses.boot])).result!==0n)throw Error('Delta pthread create failed');thread=(await rpc.read(base,8)).readBigUInt64LE();
  width=w;height=h;currentFormat=format;currentRenderMode=selectedRenderMode;report.renderMode=selectedRenderMode;report.running=true;report.ready=true;report.base=base;report.width=w;report.height=h;report.format=S.formatName(format);report.bytes=p.bytes;report.port=port;report.blocks=p.layout.count;report.thread=String(thread);delete report.error;delete report.inputFailure;await save();
 }
 try{
  api=await require('./platform.cjs').require('core/console/native/client.cjs').create(rpc,{...Object.fromEntries(['socket','recv','send','socketClose','netAddress'].map(n=>[n,defs[n]])),connect:require('./platform.cjs').require('core/console/abi/connect-function.json')},0x10000);
  if(profileDecode){const timing=require('./platform.cjs').require('core/console/abi/timing-code.json').functions;for(const fn of [timing.tick,timing.frequency])await rpc.request({command:'verify-function',address:sdk+fn.offset,hex:fn.bytes.slice(0,512)});const frequencyHz=Number((await rpc.callAddress(sdk+timing.frequency.offset,[])).result);if(!Number.isSafeInteger(frequencyHz)||frequencyHz<=0||frequencyHz>1e9)throw Error('Invalid qualified SDK tick frequency');report.sdkReportedTickFrequencyHz=frequencyHz;
   try{const samples=async()=>{const rows=[];for(let i=0;i<3;i++){const before=performance.now(),tick=(await rpc.callAddress(sdk+timing.tick.offset,[])).result,after=performance.now();rows.push({before,after,tick});}return rows;},first=await samples();await sleep(200);report.tickCalibration=calibrateFrequency(first,await samples(),frequencyHz);}catch(error){report.tickCalibration={valid:false,reason:error.message};}
   report.tickFrequencyHz=report.tickCalibration.valid?report.tickCalibration.frequencyHz:31250000;
   report.tickFrequencySource=report.tickCalibration.valid?report.tickCalibration.source:'historical Switch 2 observation; startup calibration unavailable';report.profiledAcknowledgementBytes=436;}
  for(const name of ['create','join','tryWait','exit','longjmp','loadTable','call4','saveResult64','sleep']){const f=code[name];await rpc.request({command:'verify-function',address:sdk+f.offset,hex:f.bytes.slice(0,512)});}
  await rpc.request({command:'verify-function',address:sdk+0x586fb0,hex:require('./platform.cjs').require('core/console/abi/ipc-functions.json').copy.hex});
  const allocation=await rpc.call(5732112,[rpc.output,D.ALLOCATION_ALIGNMENT,D.ALLOCATION]);if(allocation.result!==0n)throw Error('Delta allocation failed');base=Number(allocation.output.readBigUInt64LE());report.allocation={base,bytes:D.ALLOCATION,alignment:D.ALLOCATION_ALIGNMENT};D.validateBase(base,8);
  pipeline=require('./delta-prefetch.cjs').create({zip,base,level:()=>compressionLevelFor({level:compressionLevel,explicit:explicitCompression,state:consoleState(),policy:compressionPolicy()}),networkType:()=>freshConsoleNetwork(consoleState())?.type||'unknown'});
 }catch(e){report.error=e.message;await save().catch(()=>{});await zip.close();await codec.close();throw e;}
 return {report,async reset(){
  // Reuse this owned allocation and encoder only after the old native worker
  // joined. Configure resets both dictionaries before accepting a fresh frame.
  if(closePromise)throw Error('Delta receiver is closing');
  await stopWorker();if(p){await pipeline.reset(p.bytes);if(p.heapDictionary)await clearHeapDictionaries();}else await zip.discardAll();closing=false;transportError=null;report.ready=false;delete report.consoleIP;
 },async prepare(w,h,format=1){if(closing||transportError)throw transportError||Error('Delta receiver is closing');if(thread===null||w!==width||h!==height||format!==currentFormat||renderMode()!==currentRenderMode)await configure(w,h,format);report.warmedAt=new Date().toISOString();await save();},prepareFrame(bytes,w,h,format=1){if(closing||transportError||thread===null||w!==width||h!==height||format!==currentFormat||renderMode()!==currentRenderMode)return null;return pipeline.prepareFrame(bytes,w,h,format);},commitPrepared(token,metrics=Buffer.alloc(32)){
  if(!wireEnabled()||wirePrefetchDisabled||!wire||closing||transportError||!wireRouteReady())return false;
  const future=pipeline.commitPrepared(token,metrics);if(!future)return false;
  try{wire.write(future.ticket,future.encoded.packet);report.futureWirePackets=(report.futureWirePackets||0)+1;return true;}catch(error){error.code??='FRAME_PIPELINE_INVALIDATED';error.recoverable=true;disconnect(error);throw error;}
 },wirePrefetchReady:()=>!!(wireEnabled()&&!wirePrefetchDisabled&&wire&&!closing&&!transportError&&wireRouteReady()),disableWirePrefetch(reason='wire pipeline rejected'){wirePrefetchDisabled=true;report.wirePrefetchDisabled=String(reason).slice(0,300);},isWireCommitted:token=>pipeline?.isWireCommitted(token)||false,hasWireCommitted:()=>pipeline?.hasWireCommitted()||false,discardPrepared:token=>pipeline?.discardPrepared(token),async push(bytes,w,h,metrics=Buffer.alloc(32),format=1,prepared){
  if(thread===null||w!==width||h!==height||format!==currentFormat||renderMode()!==currentRenderMode)await configure(w,h,format);
  if(closing||waiting||transportError)throw transportError||Error('Delta receiver is not ready');
  const {ticket,encoded,compressionMs,compressionWaitMs,precompressed}=await pipeline.acquire(bytes,w,h,format,metrics,prepared);
  const expected=ticket.expected;let committed=false;
  try{
  const until=Date.now()+120000;while(!peer&&!queue.length){if(closing||transportError)throw transportError||Error('Delta transport closed');if(Date.now()>until)throw Object.assign(Error('Console did not connect to post-menu receiver'),{code:'ETIMEDOUT'});await sleep(2);}if(!peer)peer=queue.shift();
  report.inFlight={sequence:ticket.sequence,packetBytes:encoded.packet.length,bodyBytes:encoded.packet.readUInt32LE(20),started:new Date().toISOString()};
  let transferAckMs,frameDecodeTimings;
  if(wire){
   if(!wire.owns(ticket))wire.write(ticket,encoded.packet);
   const result=await wire.consume(ticket);transferAckMs=result.transferAckMs;frameDecodeTimings=timingFor(result.packet);
  }else{
  const ack=new Promise((resolve,reject)=>{waiting={peer,expected,resolve,reject,deadline:require('./ack-wait.cjs').deadline({onPause:()=>{onPause(true);const pending=waiting;snapshot().then(state=>{if(!pending||waiting!==pending)return;const error=require('./pc-stream-tcp.cjs').frameDiagnosticError(state);if(error){if(error.inputFailure)report.inputFailure=error.inputFailure;disconnect(error);}}).catch(e=>{report.diagnosticError=e.message;save().catch(()=>{});});},onTimeout:()=>disconnect(Object.assign(Error('Delta acknowledgement timeout'),{code:'FRAME_ACK_TIMEOUT'}))})};});
  const transferStarted=performance.now();
  const written=new Promise((resolve,reject)=>peer.write(encoded.packet,e=>e?reject(e):resolve()));
  await Promise.all([ack,written]);transferAckMs=performance.now()-transferStarted;
  frameDecodeTimings=report.decodeTimings;
  }
  pipeline.commit(ticket);committed=true;
  delete report.inFlight;report.lastAcknowledgedAt=new Date().toISOString();
  onEncoded({directInflate:p.directInflate,heapDictionary:!!p.heapDictionary,transferAckMs:+transferAckMs.toFixed(2),...(profileDecode?{decodeTimings:frameDecodeTimings}:{}),wirePrefetch:!!wire,wireQueued:wire?.count||0,futureWirePackets:report.futureWirePackets||0,compressionWaitMs:+compressionWaitMs.toFixed(2),compressionOverlap:precompressed,dictionarySkippedBlocks:encoded.dictionarySkippedBlocks,compressionBudgetMs:encoded.compressionBudgetMs,dictionaryBudgetExceeded:encoded.dictionaryBudgetExceeded,payloadBytes:encoded.packet.length,rawFrameBytes:bytes.length,changedBlocks:encoded.changedBlocks,literalBlocks:encoded.literalBlocks,blocks:encoded.blocks,compressionMs:+compressionMs.toFixed(2),workerEncodingMs:+encoded.workerEncodingMs.toFixed(2),compressionLevel:encoded.compressionLevel,compressionMode:encoded.compressionMode,compressionBackend:encoded.compressionBackend,dictionaryBlocks:encoded.dictionaryBlocks,fastBlocks:encoded.fastBlocks,keyframe:encoded.keyframe});return expected;
  }catch(error){if(!committed)try{await pipeline.abort(ticket);}catch(cleanup){error.cleanupError=cleanup.message;}throw error;}
 },interrupt(error){disconnect(error);for(const socket of sockets)socket.destroy();},snapshot,close};
}
function uploader(options){
 const metrics={transport:'SDK DEFLATE dictionary / persistent TCP v2',compressedPayloadBytes:0,rawPayloadBytes:0};let stream;
 stream=require('./pc-stream-tcp.cjs').uploader({...options,save:async s=>options.save?.(Object.assign(s,metrics,{uploadedBytes:metrics.compressedPayloadBytes})),receiverFactory:args=>receiver({...args,consoleState:options.consoleState,compressionPolicy:options.compressionPolicy,renderMode:options.renderMode,onEncoded:r=>{metrics.compressedPayloadBytes+=r.payloadBytes;metrics.rawPayloadBytes+=r.rawFrameBytes;Object.assign(metrics,{directInflate:r.directInflate,heapDictionary:r.heapDictionary,wirePrefetch:r.wirePrefetch,wireQueued:r.wireQueued,futureWirePackets:r.futureWirePackets,transferAckMs:r.transferAckMs,...(r.decodeTimings?{decodeTimings:r.decodeTimings}:{}),compressionWaitMs:r.compressionWaitMs,compressionOverlap:r.compressionOverlap,dictionarySkippedBlocks:r.dictionarySkippedBlocks,compressionBudgetMs:r.compressionBudgetMs,dictionaryBudgetExceeded:r.dictionaryBudgetExceeded,lastWireBytes:r.payloadBytes,compressionRatio:+(r.rawFrameBytes/r.payloadBytes).toFixed(2),compressionMs:r.compressionMs,workerEncodingMs:r.workerEncodingMs,compressionQueueAndTransferMs:+Math.max(0,r.compressionMs-r.workerEncodingMs).toFixed(2),compressionLevel:r.compressionLevel,compressionMode:r.compressionMode,compressionBackend:r.compressionBackend,dictionaryBlocks:r.dictionaryBlocks,fastBlocks:r.fastBlocks,changedBlocks:r.changedBlocks,literalBlocks:r.literalBlocks,blocks:r.blocks,keyframe:r.keyframe});Object.assign(stream.stats,metrics);}})});
 Object.assign(stream.stats,metrics);const push=stream.push.bind(stream);stream.push=async(...args)=>{const result=await push(...args);stream.stats.uploadedBytes=metrics.compressedPayloadBytes;return {...result,...metrics,uploadedBytes:metrics.compressedPayloadBytes};};return stream;
}
module.exports={receiver,uploader,compressor,compressionLevelFor,wireRouteAllowed,wirePrefetchEnabled,decodeTimings,calibrateFrequency};
