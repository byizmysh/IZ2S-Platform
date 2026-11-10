'use strict';
const path=require('node:path'),assert=require('node:assert/strict');
const {readJson,saveJson}=require('../../session/ledger.cjs'),{privateIP}=require('../../session/network-discovery.cjs');
const {SwitchableConnection}=require('./switchable.cjs');
async function candidates({root,peer,port,discovered=[]}){
 const preferences=(await readJson(path.join(root,'data/state/connections.json'),{})).lan||{},history=await readJson(path.join(root,'data/state/console-addresses.json'),{});
 const modes=Array.isArray(preferences.modes)?preferences.modes:['lan','wifi'],rows=[];
 for(const mode of ['lan','wifi'])if(modes.includes(mode)){
  rows.push(...discovered.filter(p=>p.mode===mode),{address:preferences.addresses?.[mode],port,mode});
  for(const entry of history.observations||[])if(entry.mode===mode)rows.push({...entry,port});
 }
 const known=discovered.find(p=>p.address===peer)||rows.find(p=>p.address===peer);
 if(!known||modes.includes(known.mode))rows.push({address:peer,port,mode:known?.mode||'unknown'});
 return rows.filter(p=>privateIP(p.address)).sort((a,b)=>{
  const signed=p=>p.source==='authenticated-console-announcement';
  return Number(signed(b))-Number(signed(a))||Date.parse(b.receivedAt||0)-Date.parse(a.receivedAt||0)||0;
 });
}
function create({native,directory,root,peer,onState=()=>{},open=require('./connection.cjs').open,listen=require('./discovery.cjs').listen,verifyImage=require('./check.cjs').verify}){
 const stable=new SwitchableConnection(native.connection);native.connection=stable;
 let connection,discovery,arming,enabled=false,closed=false,lastState='unarmed',lastEvent='',writes=Promise.resolve(),lastError,probeRequested=false,probeGeneration=0;
 const reportFile=path.join(directory,'native-recovery-worker.json');
 function event(row){if(!enabled||!['connected','synchronizing','waiting-for-network','waiting-for-command-completion','closed'].includes(row.state))return;const key=JSON.stringify([row.state,row.address,row.mode]);if(key===lastEvent)return;lastEvent=key;lastState=row.state;
  // Route attempts are diagnostics. The parent sees only meaningful native
  // link transitions and never mistakes a new TCP socket for synchronization.
  onState(row);
 }
 async function arm(){
  assert(!closed,'Recovery manager closed');if(enabled)return status();if(arming)return arming;
  arming=(async()=>{
   assert(!stable.stopped&&!stable.inflight&&!stable.awaitingResponse&&!stable.error,'Legacy control is not at a healthy command boundary');
   const credentials=await readJson(path.join(directory,'native-recovery-private.json')),report=await readJson(reportFile),manifest=verifyImage();
   assert(credentials&&report&&report.startedThread&&report.allocationsRetained&&!report.closed&&!report.handedOver,'Install a fresh recovery worker before handover');
   assert(report.session===path.basename(directory)&&credentials.consoleSession===report.session&&credentials.sdk===native.addresses.sdk&&credentials.scratch===native.addresses.scratch,'Recovery worker belongs to a different console process');
   assert(report.bootstrapSha256===manifest.sha256&&report.port===credentials.port,'Recovery worker image differs');
   assert(Number.isSafeInteger(report.context)&&report.context>=4096&&report.context<2**39,'Invalid recovery context');
   const b=await native.read(report.context,manifest.layout.server),L=manifest.layout;
   assert(b.readUInt32LE(L.magic)===0x33524e49&&b.readUInt32LE(L.version)===3&&b.readUInt32LE(L.state)===2,'Recovery worker has not initialized');
   assert(b.readUInt32LE(L.port)===credentials.port&&b.readBigUInt64LE(L.sdk)===BigInt(credentials.sdk)&&b.readBigUInt64LE(L.scratch)===BigInt(credentials.scratch),'Recovery context address binding differs');
   assert(b.subarray(L.session,L.session+16).equals(Buffer.from(credentials.session,'hex'))&&b.subarray(L.key,L.key+32).equals(Buffer.from(credentials.key,'hex')),'Recovery worker identity differs');
   connection=await open({directory,credentials,candidates:()=>candidates({root,peer,port:credentials.port,discovered:discovery?.candidates()||[]}),onState:event});
   assert(connection.journal.snapshot().committed.sequence===0&&!connection.journal.snapshot().pending,'Initial handover requires an unused command journal');
   try{discovery=await listen({credentials,onCandidate:row=>{
    connection.wake();if(row.afterGap===true||row.wakeRequested===true){probeRequested=true;probeGeneration++;}
    if(enabled){const generation=probeGeneration;connection.probeRoute({preferred:row,force:probeRequested}).then(authenticated=>{
     // Keep an offline app request/gap pending across failed probes. Every
     // later signed beacon can retry it; there is no one-shot UDP trigger.
     if(authenticated&&generation===probeGeneration)probeRequested=false;
    });}
   }});}catch(e){lastError='Address discovery: '+e.message;}
   let timer;try{await Promise.race([connection.start(),new Promise((resolve,reject)=>{timer=setTimeout(()=>reject(Error('Recovery worker authentication timed out; legacy connection retained')),10000);})]);}finally{clearTimeout(timer);}
   assert(!stable.inflight&&!stable.awaitingResponse&&!stable.stopped,'Legacy command began during recovery authentication');
   // Save ownership intent before retiring the old route. A failure here
   // leaves that route untouched; installation must never be repeated blindly.
   report.handoverPrepared=true;report.controlLifetime='console-process';report.handoverPreparedAt=new Date().toISOString();await saveJson(reportFile,report);
   stable.handover(connection);enabled=true;probeRequested=false;
   writes=writes.then(async()=>{
    report.handedOver=true;report.handedOverAt=new Date().toISOString();await saveJson(reportFile,report);
    const file=path.join(directory,'connection.json'),local=await readJson(file);
    if(local){assert(local.addresses?.session===credentials.consoleSession&&local.addresses.sdk===credentials.sdk&&local.addresses.scratch===credentials.scratch,'Local RPC endpoint belongs to another session');// Native packet ABI stays unchanged; protocol 3 is its outer recovery transport.
    local.addresses={...local.addresses,controlProtocol:3,recoveryEnabled:true};await saveJson(file,local);}
   }).catch(e=>{lastError=e.message;});
   await writes;event({state:'connected',address:connection.remoteAddress,mode:connection.mode,at:new Date().toISOString()});return status();
  })().catch(async e=>{if(!enabled){connection?.close();connection=null;await discovery?.close();discovery=null;}lastError=e.message;throw e;}).finally(()=>{arming=null;});
  return arming;
 }
 function status(){return {available:true,enabled,state:enabled?connection.suspended?'waiting-for-network':lastState:'unarmed',protocol:enabled?3:null,address:enabled?connection.remoteAddress:null,mode:enabled?connection.mode:null,...(lastError?{diagnostic:lastError}:{})};}
 return {arm,status,connection:stable,wake(){connection?.wake();discovery?.refresh();if(enabled)connection.probeRoute({force:true});return status();},async close(){if(closed)return;closed=true;connection?.close();await discovery?.close();await writes;}};
}
module.exports={create,candidates};
