'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function create({root,configuration,preparePlan=value=>require('../common.cjs').plan(root,value),fetchImpl=fetch,pause=sleep,pollMs=500,identifyLocalHost=require('../../devices/cardputer-adv/usb-status.cjs').localHost}){
 const settings=configuration.settings,jobs=new Map(),cancelled=new Set();
 let stagedPlans={},stagedId=null;
 const normalized=plan=>({steps:plan.steps,source:plan.source||'',sourceAfter:plan.sourceAfter,capsLock:settings.cardputerCapsLock==='on'});
 const hash=value=>crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');
 function configured(){
  const requirements=require('../../devices/cardputer-adv/settings.cjs').validate({...settings});
  if(requirements.length)throw Object.assign(Error(requirements.join('; ')),{code:'SETUP_REQUIRED'});
 }
 async function request(route,{method='GET',body,timeout=5000}={}){
  configured();const response=await fetchImpl(settings.cardputerEndpoint.replace(/\/$/,'')+route,{method,redirect:'error',signal:AbortSignal.timeout(timeout),headers:{authorization:'Bearer '+settings.cardputerToken,...(body===undefined?{}:{'content-type':'application/json'})},...(body===undefined?{}:{body:JSON.stringify(body)})});
  const text=await response.text();if(Buffer.byteLength(text)>16384)throw Error('Cardputer returned an oversized response');
  let value;try{value=JSON.parse(text);}catch{throw Error('Cardputer returned an invalid response');}
  if(!response.ok)throw Object.assign(Error(value.error||'Cardputer returned HTTP '+response.status),{status:response.status});
  return value;
 }
 async function status(){
  const state=await request('/v1/status');
  if(state.device!=='iz2s-cardputer-adv'||state.apiVersion!==2||!/^[a-f0-9]{32}$/.test(state.bootId||''))throw Error('Install the IZ2S Cardputer ADV firmware from this panel');
  return state;
 }
 async function inspect(){
  try{const state=await status(),compatible=state.usbKeyboardFormat==='boot-8-byte',usb=require('../../devices/cardputer-adv/usb-status.cjs').describe(state,await identifyLocalHost(state.usbSerial));return {reachable:true,moduleReady:true,installed:true,keyboard:usb.keyboard,keyboardBusy:state.busy===true,setupRequired:!usb.keyboard,firmwareUpdateRequired:!compatible,service:'cardputer-wifi',firmwareVersion:state.version,bootId:state.bootId,usbState:usb.state,usb,detail:usb.detail};}
  catch(error){return {reachable:false,moduleReady:false,keyboard:false,setupRequired:true,service:'cardputer-wifi',error:error.message};}
 }
 async function persist(file,value){await require('../../../core/services/json-write.cjs').write(file,value,{directoryMode:0o700});}
 async function stagePlans(values,onEvent=()=>{}){
  const plans={};
  for(const [name,value]of Object.entries(values)){
   if(!/^[a-z][a-z0-9-]{0,31}$/.test(name))throw Error('Invalid Cardputer input action');
   const plan=await preparePlan({...value,id:value.id||'00000000-0000-4000-8000-000000000000'});
   if(plan.steps.length>64||Buffer.byteLength(plan.source||'')>16384)throw Error('Cardputer input exceeds its source or step limit');
   plans[name]=normalized(plan);
  }
  if(Object.keys(plans).length>16||Buffer.byteLength(JSON.stringify(plans))>32768)throw Error('Cardputer input package exceeds 32 KiB');
  const contentId=hash(plans),device=await status();
  if(device.contentId!==contentId){
   if(device.busy)throw Error('Stop Cardputer input before replacing its text package');
   onEvent({detail:'Loading all input text into Cardputer ADV'});
   const receipt=await request('/v1/content/'+contentId,{method:'PUT',body:plans,timeout:10000});
   if(receipt.contentId!==contentId)throw Error('Cardputer input package was not confirmed');
  }
  stagedPlans=plans;stagedId=contentId;
  onEvent({detail:'All input text is stored on Cardputer ADV; waiting for the selected action'});
  return {contentId,actions:Object.keys(plans)};
 }
 async function performPlan(value,onEvent=()=>{}){
  const plan=await preparePlan(value);if(cancelled.has(plan.requestId))throw Error('Input job was cancelled; no commands were sent');
  if(jobs.has(plan.requestId))return jobs.get(plan.requestId).promise;
  if(plan.steps.length>64||Buffer.byteLength(plan.source||'')>16384)throw Error('Cardputer accepts at most 64 steps and 16 KiB of source per plan');
  const folder=path.join(root,'data/state/cardputer-input-jobs'),file=path.join(folder,plan.requestId+'.json');await fs.mkdir(folder,{recursive:true,mode:0o700});
  const job={cancelled:false,promise:null};jobs.set(plan.requestId,job);
  job.promise=(async()=>{
   const content=normalized(plan);
   let planKey=Object.keys(stagedPlans).find(key=>hash(stagedPlans[key])===hash(content));
   if(!planKey){await stagePlans({input:value},onEvent);planKey='input';}
   else if((await status()).contentId!==stagedId){const receipt=await request('/v1/content/'+stagedId,{method:'PUT',body:stagedPlans,timeout:10000});if(receipt.contentId!==stagedId)throw Error('Cardputer text upload was not confirmed');}
   const payload={requestId:plan.requestId,...content};
   const digest=crypto.createHash('sha256').update(JSON.stringify(payload)).digest('hex');
   let receipt,previous=await fs.readFile(file,'utf8').then(JSON.parse,error=>{if(error.code==='ENOENT')return null;throw error;});
   if(previous?.digest&&previous.digest!==digest)throw Error('Input job identity belongs to a different plan');
   if(previous?.status==='complete')return {ok:true,duplicate:true};
   if(previous&&['cancelled','failed'].includes(previous.status))throw Error('This input job already ended; it was not replayed');
   const device=await status();if(device.usbKeyboardFormat!=='boot-8-byte')throw Error('Update Cardputer ADV firmware from First Setup before entering commands on Switch.');const usb=require('../../devices/cardputer-adv/usb-status.cjs').describe(device,await identifyLocalHost(device.usbSerial));if(!usb.keyboard)throw Error(usb.detail);
   if(device.busy&&device.jobId!==plan.requestId)throw Error('Cardputer is executing another input job');
   if(previous&&previous.bootId!==device.bootId)throw Error('Cardputer restarted after this job; commands were not replayed');
   const record={id:plan.requestId,digest,bootId:device.bootId,status:'requested'};
   if(!previous){await fs.writeFile(file,JSON.stringify(record),{flag:'wx',mode:0o600});}
   if(cancelled.has(plan.requestId)||job.cancelled)throw Error('Input job cancelled');
   try{
    if(previous)receipt=await request('/v1/jobs/'+plan.requestId);
    else{
     try{receipt=await request('/v1/jobs',{method:'POST',body:{requestId:plan.requestId,digest,bootId:device.bootId,contentId:stagedId,planKey}});}
     catch(error){
      // A lost POST response never causes a second POST or a replay of HID input.
      try{receipt=await request('/v1/jobs/'+plan.requestId);}catch{throw Error('Cardputer acceptance is unconfirmed. Stop input and restart the console before another attempt.');}
     }
    }
    let line=0,step=0;
    for(;;){
     if(receipt.id!==plan.requestId||receipt.bootId!==device.bootId)throw Error('Cardputer job or boot identity changed; input was not replayed');
     if(job.cancelled||cancelled.has(plan.requestId))throw Error('Input job cancelled');
     if(receipt.line>line){line=receipt.line;onEvent({detail:'Entered source line',fields:{line,total:receipt.total}});}
     if(receipt.step>step){step=receipt.step;onEvent({detail:'Executing input plan',fields:{step,total:plan.steps.length}});}
     if(receipt.status==='complete'){await persist(file,{...record,status:'complete'});return {ok:true};}
     if(!['running','requested'].includes(receipt.status))throw Error(receipt.error||'Cardputer input '+receipt.status);
     await pause(pollMs);receipt=await request('/v1/jobs/'+plan.requestId);
    }
   }catch(error){
    try{await request('/v1/stop',{method:'POST',body:{id:plan.requestId},timeout:2000});}catch{}
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
 return {adapter:'cardputer-wifi',inspect,stagePlans,performPlan,halt,async perform(){throw Error('Cardputer input requires an engine-provided plan');}};
}
module.exports={create};
