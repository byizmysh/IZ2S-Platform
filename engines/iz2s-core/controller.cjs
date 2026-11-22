'use strict';
// IZ2S controller. Executable inputs are module-local; receipts and logs stay in OUT.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),net=require('node:net'),os=require('node:os');
const {spawn}=require('node:child_process');
const runtime=require('./runtime.cjs');
const journal=require('./journal.cjs');
const stopProcess=require('../../core/platform/stop-process.cjs').stop;
const DEFAULT_OUT=runtime.output();
const inputTransports={'pc-cardputer-adv':'cardputer-wifi','pc-switch1':'switch-wifi','pc-arm':'ssh-gadget'};
const inputNames={'pc-cardputer-adv':'Cardputer ADV','pc-switch1':'Switch 1','pc-arm':'ARM PC'};
function inputFor(root,selected,settings,inputFactory,manager){
 const transport=inputTransports[selected];
 if(!transport)throw Error('Choose Switch 1, ARM PC or Cardputer ADV under Connection.');
 if(inputFactory)return inputFactory(settings);
 // Share the panel's transport and in-flight job. Switch 1 accepts one TCP
 // client: an independent status connection would be rejected during typing.
 if(manager.inputLink)return manager.inputLink(selected);
 return require('../../hardware/transports/'+transport+'/index.cjs').create({root,configuration:{settings}});
}
async function checkKeyboard(input){
 const device=await input.inspect();
 if(device.keyboardBusy)throw Error('USB keyboard is already typing.');
 if(!device.keyboard)throw Error(device.error||'USB keyboard is not ready.');
}
function create({root,manager,out=DEFAULT_OUT,launch=spawn,inputFactory,checkPort,resetLink=()=>require('./tools/pc-link-service.cjs').reset(out)}={}){
 const directory=runtime.journal(out);
 let job=null,child=null,input=null,inputId=null,starting=false,inputTask=null,finishing=false,serial=Promise.resolve(),setupLog=null;
 const processing=()=>starting||finishing||!!child||!!inputTask;
 const setupOwned=()=>job?.kind==='setup'&&!['saved','stopped','failed','interrupted'].includes(job.state);
 const running=()=>processing()||setupOwned();
 async function save(){if(!job)return;const id=job.id,value=structuredClone(job),write=require('../../core/services/json-write.cjs').write;serial=serial.then(async()=>{await write(path.join(directory,id+'.json'),value,{directoryMode:0o700});await write(path.join(directory,'latest.json'),value,{directoryMode:0o700});});await serial;}
 function hosts(){return [...new Set(Object.values(os.networkInterfaces()).flat().filter(x=>x&&x.family==='IPv4'&&!x.internal).map(x=>x.address))];}
 async function state(){
  const current=job,busy=processing(),owned=running();
  const previous=current||await fs.readFile(path.join(directory,'latest.json'),'utf8').then(JSON.parse).catch(error=>{if(error.code==='ENOENT')return null;throw error;});
  const value=previous?structuredClone(previous):null;
  if(value?.error)value.error=runtime.displayPath(value.error,out);
  const historicalAttempt=!!value&&!current;
  if(historicalAttempt&&['starting','waiting','connected','typing','review','checking','values','checked','saving','save_confirmation','stopping'].includes(value.state)){value.state='interrupted';value.error='The server restarted. Input is not repeated automatically.';value.needsReboot=true;}
  return {busy,owned,attempt:value,historicalAttempt,hosts:hosts(),out};
 }
 async function portFree(host){
  if(checkPort)return checkPort(host);
  await new Promise((resolve,reject)=>{const server=net.createServer();server.once('error',()=>reject(Error('Port 39380 is in use. End the other attempt before starting IZ2S.')));server.listen(39380,host,()=>server.close(resolve));});
 }
 async function start({freshDirect}={}){
  if(running())throw Error('The current attempt is still running. Input was not sent again.');
  if(manager.preview)throw Error('Launch is unavailable in Preview.');
  if(!freshDirect)throw Error('Confirm that Switch has fully restarted and Direct is open.');
  starting=true;
  try{
   const panel=await manager.snapshot();
   if(panel.active||panel.connected||['running','starting','stopping','offline'].includes(panel.civLauncher?.state))throw Error('Finish the active panel session first.');
   // Reuse IZ2S Core's resolved address, including its LAN/Wi-Fi selection.
   const host=panel.settings?.consoleHost||panel.connections?.devices?.server?.address;
   if(!net.isIPv4(host||'')||!hosts().includes(host))throw Error('No active PC address on the Switch network. Check the server card.');
   await runtime.verify();
   await portFree(host);
   const config=JSON.parse(await fs.readFile(path.join(root,'data/state/launch-configurations.json'))),settings=config.profiles[config.selected];
   input=inputFor(out,config.selected,settings||{},inputFactory,manager);
   await checkKeyboard(input);
   const id=crypto.randomUUID();inputId=crypto.randomUUID();
   job={id,kind:'launch',state:'starting',inputDevice:config.selected,inputName:inputNames[config.selected],startedAt:new Date().toISOString(),host,inputId,inputSent:false,needsReboot:false,core:null,error:null,run:null};await save();
   const output=journal.writer(path.join(directory,id+'.log'));await output.append('IZ2S core · 0.3.0\n');
   let tail='',launched=false,lines=Promise.resolve();
   const append=output.append;
   child=launch(process.execPath,[runtime.start,host],{cwd:__dirname,env:{...process.env,IZ2S_CORE_OUT:out,NS2_BASIC_ARITY:'5',TMPDIR:path.join(out,'tmp'),
    IZ2S_PC_LINK_HOSTS:JSON.stringify(panel.connections?.devices?.server?.paths?.map(row=>row.address).filter(Boolean)??null),
    IZ2S_CONSOLE_IPV4:panel.settings?.consoleAddress||'',IZ2S_CONSOLE_WIFI_IPV4:panel.settings?.consoleWifiAddress||''},stdio:['ignore','pipe','pipe']});
   const owned=child;
   async function line(text){
    const match=text.match(/^Run folder: (.+)$/);
    if(match&&!launched){
     const run=path.resolve(match[1]);if(path.dirname(run)!==runtime.runs(out)||!/^switch2-[A-Za-z0-9]+$/.test(path.basename(run)))throw Error('Unexpected installer directory.');
     job.run=run;job.state='typing';launched=true;job.inputSent=true;await save();
     // Use the same LOAD confirmation and delays as the normal Platform path.
     const plan={id:inputId,steps:require('../common.cjs').connectSteps({bootstrapName:'NS2IZM',command:'V=START(39380,'+host.replaceAll('.',',')+')'})};
     await fs.writeFile(path.join(directory,id+'-input.json'),JSON.stringify(plan,null,2));
     if(child!==owned||job.cancelled)return;
     inputTask=input.performPlan(plan,e=>void append('[input] '+e.detail+'\n')).then(async()=>{job.inputComplete=true;if(job.state==='typing')job.state='waiting';await save();},async e=>{job.error='Input: '+e.message;job.needsReboot=true;await append(job.error+'\n');await save();await stopProcess(owned).catch(error=>append('Stop failed: '+error.message+'\n'));}).finally(()=>{inputTask=null;});
    }
    if(text.includes('Connected.')){job.state='connected';await save();}
    if(/^IZ2S launch completed!(?:\s|$)/.test(text)){
     // The success marker is only a trigger to read the actual receipt.
     // Startup instructions mention this phrase before Run folder exists.
     if(!job.run)throw Error('The installer reported success before creating the attempt directory.');
     const receipt=JSON.parse(await fs.readFile(path.join(job.run,'switch2/ro/core-startup.json')));
     if(receipt.complete!==true||receipt.result!==0||!receipt.pid)throw Error('The core-startup.json receipt did not confirm the IZ2S launch.');
     job.core={complete:true,pid:receipt.pid,result:0};job.state='ready';await save();
    }
    const phase=text.match(/^([1-4])\/4 (.+)$/);if(phase){job.phase=Number(phase[1]);await save();}
    if(/^Error:/.test(text)){job.error??=text.slice(0,1600);await save();}
   }
   const consume=chunk=>{const text=chunk.toString();void append(chunk);tail+=text;let at;while((at=tail.indexOf('\n'))>=0){const row=tail.slice(0,at).replace(/\r$/,'');tail=tail.slice(at+1);lines=lines.then(()=>line(row)).catch(async e=>{job.error=e.message;job.needsReboot=true;await append('Error: '+e.message+'\n');await save();await stopProcess(owned).catch(error=>append('Stop failed: '+error.message+'\n'));});}};
   child.stdout.on('data',consume);child.stderr.on('data',consume);
   child.once('error',e=>{job.error=e.message;void append('Error: '+e.message+'\n');});
   child.once('close',async(code,signal)=>{
    if(child!==owned)return;finishing=true;await lines;child=null;job.exitCode=code;job.signal=signal;job.finishedAt=new Date().toISOString();
    if(job.core?.complete)job.state='ready';else{job.state=job.cancelled?'stopped':'failed';job.needsReboot=job.inputSent;if(!job.error&&!job.cancelled){job.error='The installer ended before IZ2S launch was confirmed.';await append('Error: '+job.error+'\n');}}
    await output.flush();await save();finishing=false;
   });
   return {ok:true,id};
  }finally{starting=false;}
 }
 async function setup(body={}){
  if(processing())throw Error('Wait for the current input to finish.');
  if(manager.preview)throw Error('Input is unavailable in Preview.');
  const checks=require('../../core/session/source-check.cjs'),slots=require('../../core/session/install-slot.cjs');
  const action=body.action;
  if(!['begin','verify','compare','save','saved'].includes(action))throw Error('Unknown First Setup step.');
  starting=true;
  try{
   const panel=await manager.snapshot();if(panel.active||panel.connected||['running','starting','stopping','offline'].includes(panel.civLauncher?.state))throw Error('Finish the active panel session first.');
   if(action==='begin'){
    if(setupOwned())throw Error('First Setup is already active. Finish or stop it.');
    if(!body.screenReady)throw Error('Confirm that Direct is open and the selected slot is empty.');
    const slot=slots.resolve(body.slot),source=runtime.bootstrap;
    await runtime.verify();
    const bytes=await fs.readFile(source);
    const plan=checks.makePlan(bytes,{sourceSlot:slot});
    const config=JSON.parse(await fs.readFile(path.join(root,'data/state/launch-configurations.json')));
    input=inputFor(out,config.selected,config.profiles[config.selected]||{},inputFactory,manager);
    await checkKeyboard(input);
    job={id:crypto.randomUUID(),kind:'setup',state:'typing',inputDevice:config.selected,inputName:inputNames[config.selected],startedAt:new Date().toISOString(),slot,source,sourceSha256:plan.sha256,check:plan,screen:'editor',inputSent:true,needsReboot:false,error:null};
    await save();setupLog=journal.writer(path.join(directory,job.id+'.log'));await setupLog.append('IZ2S core · First Setup · NS2IZM\n');
   }else{
    if(!job||job.kind!=='setup'||job.cancelled)throw Error('Start First Setup in this panel.');
    if(action==='saved'){
     if(job.state!=='save_confirmation'||!body.savedConfirmed)throw Error('Confirm the save completed on Switch.');
     job.state='saved';job.consoleReadback=false;job.userConfirmed=true;job.finishedAt=new Date().toISOString();job.needsReboot=true;await save();return {ok:true};
    }
    if(action==='compare'){
     if(job.state!=='values'||!body.linesConfirmed)throw Error('Confirm verification is complete and the line count is correct.');
     job.comparison=checks.compare(job.check,body.values);job.state=job.comparison.passed?'checked':'values';await save();return {ok:true,passed:job.comparison.passed};
    }
    if(!['review','values','checked'].includes(job.state)||!body.reviewed)throw Error('Check that the program has 229 lines and ends with END, then confirm.');
    await checkKeyboard(input);
   }
   const steps=action==='begin'?[{type:'line',text:'LIST '+job.slot+':1',delayMs:2000}]:[...(job.screen==='editor'?[{type:'key',key:'f4',delayMs:2000}]:[]),...(action==='verify'?job.check.commands:[slots.commands('NS2IZM',job.slot).save]).map(text=>({type:'line',text,delayMs:2000}))];
   inputId=crypto.randomUUID();job.inputId=inputId;job.inputComplete=false;job.state=action==='begin'?'typing':action==='verify'?'checking':'saving';await save();
   const plan={id:inputId,steps,...(action==='begin'?{sourceFile:job.source}:{})},owned=job,output=setupLog;
   await fs.writeFile(path.join(directory,job.id+'-'+action+'-input.json'),JSON.stringify(plan,null,2));
   inputTask=input.performPlan(plan,e=>{
    if(e.fields?.line)job.progress={line:e.fields.line,total:e.fields.total};
    void output.append('[input] '+(e.detail||'')+' '+JSON.stringify(e.fields||{})+'\n');
    void save();
   }).then(async()=>{
    if(job!==owned||job.cancelled)return;job.inputComplete=true;job.state=action==='begin'?'review':action==='verify'?'values':'save_confirmation';if(action!=='begin')job.screen='direct';await output.flush();await save();
   },async e=>{if(job!==owned)return;job.state=job.cancelled?'stopped':'failed';job.error=e.message;job.needsReboot=true;await output.append('Error: '+e.message+'\n');await save();}).finally(()=>{inputTask=null;});
   return {ok:true,id:job.id};
  }finally{starting=false;}
 }
 async function stop(){
  if(!child&&!inputTask&&!setupOwned())return {ok:true};job.cancelled=true;job.needsReboot=job.inputSent;job.state='stopping';await save();
  try{if(input&&inputId&&!job.inputComplete)await input.halt(inputId);}finally{if(child)await stopProcess(child);else if(job?.kind==='setup'){job.state='stopped';await save();}}return {ok:true};
 }
 async function reset(){
  if(manager.preview)throw Error('Session reset is unavailable in Preview.');
  if(running())throw Error('An IZ2S attempt is already active. Stop it first.');
  starting=true;
  try{
   const previous=(await state()).attempt;
   let warning=null;
   try{await resetLink();}catch(error){warning='Panel session reset. PC Link did not respond: '+error.message;}
   if(previous?.kind==='reset')return {ok:true,reset:true,warning};
   const at=new Date().toISOString();
   // Create a separate reset receipt: the successful attempt and its log
   // remain intact, while latest.json can no longer restore a stale session.
   job={id:crypto.randomUUID(),kind:'reset',state:'idle',startedAt:at,finishedAt:at,
    previousAttemptId:previous?.id||null,core:null,inputSent:false,needsReboot:false,error:null};
   await save();
   return {ok:true,reset:true,warning};
  }finally{starting=false;}
 }
 return {state,start,setup,stop,reset,running};
}
module.exports={create,DEFAULT_OUT};
