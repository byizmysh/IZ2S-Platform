'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function create({root,configuration,preparePlan=value=>require('../common.cjs').plan(root,value),fetchImpl=fetch,pause=sleep,pollMs=500,reconnectMs=120000,now=Date.now,identifyLocalHost=require('../../devices/cardputer-adv/usb-status.cjs').localHost}){
 const settings=configuration.settings,jobs=new Map(),cancelled=new Set();
 let stagedPlans={},stagedId=null,stagedBootId=null,stagedUsbGeneration=null;
 let wire=Promise.resolve(),statusRead=null,lastGood=null,lastSeenAt=0,statusFailures=0;
 const normalized=plan=>({steps:plan.steps,source:plan.source||'',sourceAfter:plan.sourceAfter,capsLock:settings.cardputerCapsLock==='on',...(plan.sourceMode?{sourceMode:plan.sourceMode}:{})});
 const hash=value=>crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
 function configured(){
  const requirements=require('../../devices/cardputer-adv/settings.cjs').validate({...settings});
  if(requirements.length)throw Object.assign(Error(requirements.join('; ')),{code:'SETUP_REQUIRED'});
 }
 function transient(error){return ['TypeError','TimeoutError','AbortError'].includes(error.name)||
  ['ECONNRESET','ECONNREFUSED','ETIMEDOUT','EHOSTUNREACH','ENETUNREACH','UND_ERR_SOCKET'].includes(error.code||error.cause?.code)||error.status>=500;}
 function request(route,options={}){
  // The ESP32 WebServer handles one HTTP client at a time. Status reads and
  // command receipts share a wire queue; a failed command is never re-posted.
  const result=wire.then(()=>sendRequest(route,options));wire=result.catch(()=>{});return result;
 }
 async function sendRequest(route,{method='GET',body,timeout=5000}={}){
  configured();const response=await fetchImpl(settings.cardputerEndpoint.replace(/\/$/,'')+route,{method,redirect:'error',signal:AbortSignal.timeout(timeout),headers:{authorization:'Bearer '+settings.cardputerToken,connection:'close',...(body===undefined?{}:{'content-type':'application/json'})},...(body===undefined?{}:{body:JSON.stringify(body)})});
  const text=await response.text();if(Buffer.byteLength(text)>16384)throw Error('Cardputer returned an oversized response');
  let value;try{value=JSON.parse(text);}catch{throw Error('Cardputer returned an invalid response');}
  if(!response.ok)throw Object.assign(Error(value.error||'Cardputer returned HTTP '+response.status),{status:response.status});
  return value;
 }
 function status(){
  if(statusRead)return statusRead;
  statusRead=(async()=>{
   let state;
   try{state=await request('/v1/status',{timeout:7500});}
   catch(error){if(!transient(error))throw error;await pause(150);state=await request('/v1/status',{timeout:2000});}
   if(state.device!=='iz2s-cardputer-adv'||state.apiVersion!==2||!/^[a-f0-9]{32}$/.test(state.bootId||''))throw Error('Install the IZ2S Cardputer ADV firmware from this panel');
   lastGood=state;lastSeenAt=now();statusFailures=0;return state;
  })().catch(error=>{statusFailures++;throw error;}).finally(()=>{statusRead=null;});
  return statusRead;
 }
 async function inspect(){
  try{const state=await status(),compatible=state.usbKeyboardFormat==='boot-8-byte',usb=require('../../devices/cardputer-adv/usb-status.cjs').describe(state,await identifyLocalHost(state.usbSerial));return {reachable:true,moduleReady:true,installed:true,keyboard:usb.keyboard,keyboardBusy:state.busy===true,setupRequired:!usb.keyboard,firmwareUpdateRequired:!compatible,service:'cardputer-wifi',firmwareVersion:state.version,offlineLineCompletion:state.offlineLineCompletion===true,batchRawLines:state.batchRawLines===true,usbGeneration:state.usbGeneration,rawTextInput:state.rawTextInput===true,sourceLineSelection:state.sourceLineSelection===true,gamepadInput:state.gamepadInput===true,boundedLineInput:state.boundedLineInput===true,freeHeap:state.freeHeap,minFreeHeap:state.minFreeHeap,maxAllocHeap:state.maxAllocHeap,lastRequestError:state.lastRequestError,gamepadReady:usb.keyboard&&state.gamepadReady===true,bootId:state.bootId,usbState:usb.state,usb,detail:usb.detail};}
  catch(error){
   const pending=transient(error)&&lastGood!==null&&now()-lastSeenAt<30000&&statusFailures<3;
   const volumes=await require('../../cardputer.cjs').media().catch(()=>[]);
   const storage=volumes.length?{usbStorage:true,storageTarget:volumes[0].target}:{};
   return {reachable:false,moduleReady:false,keyboard:false,setupRequired:!pending,pending,
    lastSeenAt:lastSeenAt?new Date(lastSeenAt).toISOString():null,bootId:lastGood?.bootId,
    service:'cardputer-wifi',...storage,error:volumes.length?'ADV is connected to this PC in USB storage mode. Exit storage and start IZ2S-ADV.bin in M5Launcher to enable Wi-Fi and keyboard input.':pending?'ADV status is delayed; rechecking Wi-Fi. Input waits for a fresh response.':error.message};
  }
 }
 async function confirmUsb(){
  const device=await status(),usb=require('../../devices/cardputer-adv/usb-status.cjs').describe(device,await identifyLocalHost(device.usbSerial));
  if(usb.localHost==='this-pc')throw Error(usb.detail);
  if(device.busy)throw Error('Stop input before confirming the USB connection');
  // Older firmware can still be confirmed in RUN21 without a device-side acknowledgement.
  if(!usb.confirmationSupported)return {ok:true,localOnly:true};
  const receipt=await request('/v1/usb/confirm',{method:'POST',body:{bootId:device.bootId,usbGeneration:usb.generation}});
  if(receipt.confirmed!==true||receipt.bootId!==device.bootId)throw Error('USB confirmation was not acknowledged');
  return {ok:true};
 }
 async function persist(file,value){await require('../../../core/services/json-write.cjs').write(file,value,{directoryMode:0o700});}
 async function stagePlans(values,onEvent=()=>{}){
  const plans={};
  for(const [name,value]of Object.entries(values)){
   if(!/^[a-z][a-z0-9-]{0,31}$/.test(name))throw Error('Invalid Cardputer input action');
   const plan=await preparePlan({...value,id:value.id||'00000000-0000-4000-8000-000000000000'});
   if(plan.steps.length>64||Buffer.byteLength(plan.source||'')>(plan.sourceMode==='raw-lines'?32768:16384))throw Error('Cardputer input exceeds its source or step limit');
   plans[name]=normalized(plan);
  }
  if(Object.keys(plans).length>16||Buffer.byteLength(JSON.stringify(plans))>65536)throw Error('Cardputer input package exceeds 64 KiB');
   const contentId=hash(plans),device=await status();
   if(Object.values(plans).some(plan=>plan.steps.some(step=>step.type==='gamepad'))&&!device.gamepadInput)throw Error('Install dual HID Cardputer ADV firmware 0.22.2 before automatic controller input');
   if(Object.values(plans).some(plan=>plan.sourceMode==='raw-lines')&&!device.sourceLineSelection)throw Error('Install unified Cardputer ADV firmware 0.22.1 or newer from Device setup before using CivSwitch');
   if(Object.values(plans).some(plan=>plan.steps.some(step=>step.type==='text'))&&!device.rawTextInput)throw Error('Install Cardputer ADV firmware 0.22.0 or newer from Device setup before using CivSwitch');
  if(device.contentId!==contentId){
   if(device.busy)throw Error('Stop Cardputer input before replacing its text package');
   onEvent({detail:'Loading all input text into Cardputer ADV'});
   const receipt=await request('/v1/content/'+contentId,{method:'PUT',body:plans,timeout:10000});
   if(receipt.contentId!==contentId)throw Error('Cardputer input package was not confirmed');
  }
  stagedPlans=plans;stagedId=contentId;stagedBootId=device.bootId;stagedUsbGeneration=device.usbGeneration;
  onEvent({detail:'All input text is stored on Cardputer ADV; waiting for the selected action'});
  return {contentId,actions:Object.keys(plans)};
 }
 async function readJob(id,onEvent=()=>{},recover=false){
  const deadline=now()+reconnectMs;let announced=false;
  for(;;){
   if(cancelled.has(id))throw Error('Input job cancelled');
   try{return await request('/v1/jobs/'+id,{timeout:recover?2000:7500});}
   catch(error){
    if(!recover||!transient(error)||now()>=deadline)throw error;
    if(!announced){onEvent({detail:'ADV connection delayed; waiting for the same input job receipt. No input is replayed.'});announced=true;}
    await pause(Math.min(500,Math.max(0,deadline-now())));
   }
  }
 }
 async function receipt(id){
  if(!/^[a-f\d-]{36}$/.test(id))throw Error('Invalid input job identifier');
  const record=JSON.parse(await fs.readFile(path.join(root,'data/state/cardputer-input-jobs',id+'.json'),'utf8'));
  const device=await status();if(device.bootId!==record.bootId)throw Error('ADV restarted; the previous line cannot be recovered automatically');
  const value=await readJob(id);
  if(value.id!==id||value.bootId!==record.bootId||value.digest&&value.digest!==record.digest||record.sourceLine!==undefined&&value.sourceLine!==undefined&&value.sourceLine!==record.sourceLine)throw Error('The previous line receipt does not match');
  if(record.usbGeneration!==undefined&&device.usbGeneration!==record.usbGeneration)throw Error('USB connection changed after input stopped');
  return {...value,recordedSourceLine:record.sourceLine,recordedSourceThrough:record.sourceThrough,usbGenerationVerified:record.usbGeneration!==undefined};
 }
 async function followExisting(id,onEvent=()=>{}){
  // Read-only reattachment: never stage, POST input or repeat any HID report.
  let value=await receipt(id);
  if(!Number.isInteger(value.recordedSourceThrough))throw Error('The old input job is not an autonomous batch');
  for(;;){
   const line=value.line;
   if(Number.isInteger(line))onEvent({detail:'Stored input progress',fields:{committedLine:line,line,total:value.total}});
   if(value.status==='complete')return {ok:true};
   if(!['running','requested'].includes(value.status))throw Error(value.error||'Stored input did not complete');
   await pause(pollMs);
   const next=await readJob(id,onEvent,true);
   if(next.id!==id||next.bootId!==value.bootId||next.digest&&next.digest!==value.digest||next.usbCurrentGeneration!==undefined&&next.usbCurrentGeneration!==value.usbCurrentGeneration)throw Error('Stored job or USB identity changed; input was not replayed');
   value={...value,...next};
  }
 }
 async function performPlan(value,onEvent=()=>{}){
  const plan=await preparePlan(value);if(cancelled.has(plan.requestId))throw Error('Input job was cancelled; no commands were sent');
  if(jobs.has(plan.requestId))return jobs.get(plan.requestId).promise;
  if(plan.steps.length>64||Buffer.byteLength(plan.source||'')>(plan.sourceMode==='raw-lines'?32768:16384))throw Error('Cardputer accepts at most 64 steps and 16 KiB of source per plan');
  const folder=path.join(root,'data/state/cardputer-input-jobs'),file=path.join(folder,plan.requestId+'.json');await fs.mkdir(folder,{recursive:true,mode:0o700});
  const job={cancelled:false,promise:null};jobs.set(plan.requestId,job);
  job.promise=(async()=>{
   const content=normalized(plan);
   let planKey=Object.keys(stagedPlans).find(key=>hash(stagedPlans[key])===hash(content));
   if(!planKey){await stagePlans({input:value},onEvent);planKey='input';}
   else if((await status()).contentId!==stagedId){const receipt=await request('/v1/content/'+stagedId,{method:'PUT',body:stagedPlans,timeout:10000});if(receipt.contentId!==stagedId)throw Error('Cardputer text upload was not confirmed');}
   const payload={requestId:plan.requestId,...content,...(plan.sourceMode==='raw-lines'?{sourceLine:plan.sourceLine,...(plan.sourceThrough!==undefined?{sourceThrough:plan.sourceThrough}:{})}:{})};
   const digest=crypto.createHash('sha256').update(JSON.stringify(payload)).digest('hex');
   let receipt,previous=await fs.readFile(file,'utf8').then(JSON.parse,error=>{if(error.code==='ENOENT')return null;throw error;});
   if(previous?.digest&&previous.digest!==digest)throw Error('Input job identity belongs to a different plan');
   if(previous?.status==='complete')return {ok:true,duplicate:true};
   if(previous&&['cancelled','failed'].includes(previous.status))throw Error('This input job already ended; it was not replayed');
   const device=await status();if(plan.sourceMode==='raw-lines'&&stagedBootId&&(device.bootId!==stagedBootId||stagedUsbGeneration!==undefined&&device.usbGeneration!==stagedUsbGeneration))throw Error('ADV or USB changed during this sequence; input was not replayed');if(device.usbKeyboardFormat!=='boot-8-byte')throw Error('Update Cardputer ADV firmware from First Setup before entering commands on Switch.');const usb=require('../../devices/cardputer-adv/usb-status.cjs').describe(device,await identifyLocalHost(device.usbSerial));if(!usb.keyboard)throw Error(usb.detail);
   if(plan.steps.some(step=>step.type==='gamepad')&&(!device.gamepadInput||!device.gamepadReady))throw Error('The dual HID controller interface is not ready; no commands were sent');
   if(device.busy&&device.jobId!==plan.requestId)throw Error('Cardputer is executing another input job');
   if(previous&&previous.bootId!==device.bootId)throw Error('Cardputer restarted after this job; commands were not replayed');
   const record={id:plan.requestId,digest,bootId:device.bootId,status:'requested',usbGeneration:device.usbGeneration,sourceLine:plan.sourceLine,sourceThrough:plan.sourceThrough,sourceMode:plan.sourceMode,contentId:stagedId,planKey};
   if(!previous){await fs.writeFile(file,JSON.stringify(record),{flag:'wx',mode:0o600});}
   if(cancelled.has(plan.requestId)||job.cancelled)throw Error('Input job cancelled');
   try{
    if(previous)receipt=await readJob(plan.requestId,onEvent,true);
    else{
     try{receipt=await request('/v1/jobs',{method:'POST',body:{requestId:plan.requestId,digest,bootId:device.bootId,contentId:stagedId,planKey,...(plan.sourceMode==='raw-lines'?{sourceLine:plan.sourceLine,...(plan.sourceThrough!==undefined?{sourceThrough:plan.sourceThrough}:{})}:{})}});}
     catch(error){
      if(error.status>=400&&error.status<500)throw Error('ADV rejected the input request before typing: '+error.message);
      // A lost POST response never causes a second POST or a replay of HID input.
      try{receipt=await readJob(plan.requestId,onEvent,true);}catch{const uncertain=Error('Cardputer acceptance is unconfirmed. The saved job identity must be checked before continuing.');uncertain.name='TimeoutError';throw uncertain;}
     }
    }
    let line=0,step=0;
    for(;;){
     if(receipt.id!==plan.requestId||receipt.bootId!==device.bootId||receipt.digest&&receipt.digest!==digest||receipt.sourceLine!==undefined&&plan.sourceMode==='raw-lines'&&receipt.sourceLine!==plan.sourceLine||receipt.usbCurrentGeneration!==undefined&&receipt.usbCurrentGeneration!==device.usbGeneration)throw Error('Cardputer job, source line or USB identity changed; input was not replayed');
     if(job.cancelled||cancelled.has(plan.requestId))throw Error('Input job cancelled');
     if(receipt.line>line){line=receipt.line;onEvent({detail:'Entered source line',fields:{line,total:receipt.total,committedLine:plan.sourceThrough!==undefined?line:undefined}});}
     if(receipt.step>step){step=receipt.step;onEvent({detail:'Executing input plan',fields:{step,total:plan.steps.length}});}
     if(receipt.status==='complete'){await persist(file,{...record,status:'complete'});return {ok:true};}
     if(!['running','requested'].includes(receipt.status))throw Error(receipt.error||'Cardputer input '+receipt.status);
     await pause(pollMs);
     receipt=await readJob(plan.requestId,onEvent,true);
    }
   }catch(error){
    if(!transient(error)){try{await request('/v1/stop',{method:'POST',body:{id:plan.requestId},timeout:2000});}catch{}}
    await persist(file,{...record,status:job.cancelled?'cancelled':'failed',error:error.message});throw error;
   }
  })();return job.promise;
 }
 async function halt(id){
  if(!/^[a-f\d-]{36}$/.test(id))throw Error('Invalid input job identifier');
  cancelled.add(id);const job=jobs.get(id);if(job)job.cancelled=true;
  const device=await status();if(device.busy&&device.jobId!==id)throw Error('Another job owns the Cardputer keyboard');
  const stopped=await request('/v1/stop',{method:'POST',body:{id:device.busy?id:''}});
  if(stopped.status==='running')throw Error('Cardputer input cancellation is unconfirmed');
  if(job){let timer;try{await Promise.race([job.promise.catch(()=>{}),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Cardputer input still stopping')),6000);})]);}finally{clearTimeout(timer);}}
  return {stopped:true};
 }
 return {adapter:'cardputer-wifi',inspect,receipt,followExisting,confirmUsb,stagePlans,performPlan,halt,async perform(){throw Error('Cardputer input requires an engine-provided plan');}};
}
module.exports={create};
