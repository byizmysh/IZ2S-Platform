'use strict';
const net=require('node:net'),fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {connectedToThisPc}=require('../../devices/switch1/usb-status.cjs');
const keys={enter:40,escape:41,backspace:42,tab:43,capslock:57,f4:61,f7:64,f8:65,home:74,delete:76,end:77,right:79,left:80,down:81,up:82};
const plain="1234567890-=[]\\;'`,./",shifted='!@#$%^&*()_+{}|:"~<>?';
const usages=[30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56];
function character(char,caps=false){
 if(/[a-z]/i.test(char))return [((char===char.toUpperCase())!==caps)?2:0,char.toLowerCase().charCodeAt(0)-93];
 if(char===' ')return [0,44];if(char==='\t')return [0,43];
 let at=plain.indexOf(char);if(at>=0)return [0,usages[at]];at=shifted.indexOf(char);if(at>=0)return [2,usages[at]];
 throw Error('Switch keyboard supports ASCII text only');
}
function connect(settings,{timeout=2500}={}){
 return new Promise((resolve,reject)=>{
  const socket=net.createConnection({host:settings.switchAddress,port:Number(settings.switchPort||24872)});
  let pending=null,buffer='',failed=null;
  const fail=error=>{failed=error;if(pending){clearTimeout(pending.timer);pending.reject(error);pending=null;}socket.destroy();reject(error);};
  socket.setNoDelay(true);socket.on('error',fail);socket.on('close',()=>fail(Error('Switch 1 connection closed; input was not replayed')));
  socket.on('data',data=>{
   buffer+=data.toString('ascii');if(buffer.length>128)return fail(Error('Invalid Switch keyboard response'));
   const at=buffer.indexOf('\n');if(at<0)return;
   if(!pending||at!==buffer.length-1)return fail(Error('Unexpected Switch keyboard response'));
   const response=buffer.slice(0,at),p=pending;buffer='';pending=null;clearTimeout(p.timer);p.resolve(response);
  });
  function exchange(line){
   if(failed)return Promise.reject(failed);if(pending)return Promise.reject(Error('Keyboard request already in progress'));
   return new Promise((resolve,reject)=>{pending={resolve,reject,timer:setTimeout(()=>fail(Error('Switch keyboard acknowledgement timed out; input was not replayed')),timeout)};socket.write(line+'\n');});
  }
  const opening=setTimeout(()=>fail(Error('Switch 1 did not answer on Wi-Fi')),timeout);
  socket.once('connect',async()=>{clearTimeout(opening);try{if(await exchange('AUTH '+String(settings.switchToken).replace(/[\s-]/g,'').toLowerCase())!=='OK')throw Error('Switch 1 pairing failed');resolve({exchange,close:()=>socket.destroy()});}catch(e){fail(e);}});
  socket.once('close',()=>clearTimeout(opening));
 });
}
function create({root,configuration,preparePlan=value=>require('../common.cjs').plan(root,value),pause=ms=>new Promise(r=>setTimeout(r,ms)),now=Date.now,localKeyboard=connectedToThisPc,recoverAddress,open=connect}){
 const settings=configuration.settings,jobs=new Map(),cancelled=new Set();let active=null,statusRead=null,lastRecovery=configuration.inputDiscovery&&!configuration.inputDiscovery.paired?now():-Infinity;
 function configured(){const problems=require('../../devices/switch1/settings.cjs').validate({...settings});if(problems.length)throw Error(problems.join(' '));}
 function result(ready,busy=false){return {reachable:true,moduleReady:true,installed:true,keyboard:ready,keyboardBusy:busy,setupRequired:!ready,service:'switch-wifi',address:settings.switchAddress,rawTextInput:true,sourceLineSelection:false,gamepadInput:false,gamepadReady:false,usbState:ready?'configured':'not attached',detail:ready?'Switch 1 USB keyboard ready.':'Connect Switch 1 USB-C to the Switch 2 dock and keep IZ2S SB4 Keyboard open.'};}
 async function inspect(){
  if(active)return {...result(active.ready&&now()-active.seen<5000,true)};
  if(statusRead)return statusRead;
  statusRead=(async()=>{
   let client;
   try {
    configured();
    async function readStatus(){
     client=await open(settings);
     const reply=await client.exchange('STATUS');
     if(!/^IZSWITCH 1 (READY|WAIT)$/.test(reply))throw Error('Open IZ2S SB4 Keyboard on Switch 1');
     return reply;
    }
    let reply;
    try {reply=await readStatus();} catch(error) {
     client?.close();client=null;
     if(!recoverAddress||active||now()-lastRecovery<30000)throw error;
     lastRecovery=now();await recoverAddress();
     if(active)throw error;
     reply=await readStatus();
    }
    const ready=reply.endsWith('READY');
    if(ready&&await localKeyboard()){
     const detail='USB is connected to this PC. Move the cable to the Switch 2 dock.';
     return {...result(false), usbState:'this-pc', usb:{localHost:'this-pc',detail}, detail};
    }
    return result(ready);
   } catch(e){
    return {reachable:false,moduleReady:false,keyboard:false,setupRequired:true,service:'switch-wifi',error:e.message};
   } finally { client?.close(); }
  })().finally(()=>{statusRead=null;});
  return statusRead;
 }
 async function prepared(value){const plan=await preparePlan(value);if(plan.sourceMode||plan.steps.some(s=>s.type==='gamepad'))throw Error('Switch 1 keyboard supports SmileBASIC 4 keyboard plans; controller input is unavailable');return plan;}
 async function stagePlans(values){for(const value of Object.values(values))await prepared({...value,id:value.id||'00000000-0000-4000-8000-000000000000'});return {actions:Object.keys(values)};}
 async function performPlan(value,onEvent=()=>{}){
  const plan=await prepared(value);configured();
  if(cancelled.has(plan.requestId))throw Error('Input job cancelled');
  const digest=crypto.createHash('sha256').update(JSON.stringify(plan)).digest('hex');
  if(jobs.has(plan.requestId)){const old=jobs.get(plan.requestId);if(old.digest!==digest)throw Error('Input job identity belongs to a different plan');return old.promise;}
  if(active)throw Error('Switch 1 is already typing another input job');
  // Reserve before any await so two submitters cannot own the keyboard.
  const job={id:plan.requestId,ready:false,seen:0,client:null};active=job;
  const promise=(async()=>{
   const file=path.join(root,'data/state/switch-input-jobs',plan.requestId+'.json');
   let record,created=false;
   function check(){if(cancelled.has(job.id))throw Error('Input job cancelled');}
   async function exchange(line){check();const r=await job.client.exchange(line);job.seen=now();return r;}
   async function request(line){if(await exchange(line)!=='OK')throw Error('Switch keyboard did not acknowledge USB delivery');}
   async function wait(ms){for(let left=ms;left>0;){check();const chunk=Math.min(left,400);await pause(chunk);left-=chunk;if(left>0)await request('PING');}}
   async function tap(mod,key){await request('REPORT '+Buffer.from([mod,0,key,0,0,0,0,0]).toString('hex'));await wait(60);await request('RELEASE');await wait(80);}
   async function text(value){const leds=await exchange('LEDS');if(!/^LEDS [0-1][0-9a-f]$/.test(leds))throw Error('Invalid keyboard LED response');const caps=!!(parseInt(leds.slice(5),16)&2);for(const ch of value)await tap(...character(ch,caps));}
   async function source(){const lines=plan.source.replace(/\r\n?/g,'\n').replace(/\n$/,'').split('\n');for(let i=0;i<lines.length;i++){
    await tap(2,keys.home);await tap(0,keys.delete);await text(lines[i]);
    if(lines[i].trim()){await tap(0,keys.escape);await wait(200);}await tap(0,keys.enter);await wait(300);
    if(lines[i].trim().toUpperCase()==='END')await wait(800);
    onEvent({detail:'Entered source line',fields:{line:i+1,total:lines.length}});
   }}
   try{
    if(statusRead)await statusRead;check();
    await fs.mkdir(path.dirname(file),{recursive:true,mode:0o700});
    record=await fs.readFile(file,'utf8').then(JSON.parse,e=>{if(e.code==='ENOENT')return null;throw e;});
    if(record){if(record.digest===digest&&record.status==='complete')return {ok:true,duplicate:true};throw Error('This Switch input job already started; commands were not replayed. Start a fresh attempt after checking SB4.');}
    job.client=await open(settings);check();
    if(await exchange('STATUS')!=='IZSWITCH 1 READY')throw Error('Switch 1 USB keyboard is not ready');job.ready=true;
    record={id:job.id,digest,status:'requested'};await fs.writeFile(file,JSON.stringify(record),{flag:'wx',mode:0o600});created=true;
    for(let at=0;at<=plan.steps.length;at++){
     check();if(plan.source!==null&&plan.source!==undefined&&at===plan.sourceAfter)await source();if(at===plan.steps.length)break;
     const step=plan.steps[at];
     if(step.type==='sleep')await wait(step.ms);
     else {
      if(step.type==='line'||step.type==='text'){await text(step.text);if(step.type==='line'){await tap(0,keys.escape);await wait(500);await tap(0,keys.enter);}}
      else if(step.type==='key')await tap(0,keys[step.key]);
      await wait(step.delayMs);
     }
     onEvent({detail:'Executing input plan',fields:{step:at+1,total:plan.steps.length}});
    }
    await request('RELEASE');await fs.writeFile(file,JSON.stringify({...record,status:'complete'}));return {ok:true};
   }catch(e){if(created)await fs.writeFile(file,JSON.stringify({...record,status:cancelled.has(job.id)?'cancelled':'failed',error:e.message}));throw e;}
   finally{job.ready=false;job.client?.close();if(active===job)active=null;}
  })();jobs.set(plan.requestId,{digest,promise});return promise;
 }
 async function halt(id){cancelled.add(id);if(active&&active.id!==id)throw Error('Another job owns the Switch keyboard');active?.client?.close();await jobs.get(id)?.promise.catch(()=>{});return {stopped:true};}
 return {adapter:'switch-wifi',inspect,stagePlans,performPlan,halt,async perform(){throw Error('Switch keyboard input requires an engine-provided plan');}};
}
module.exports={create,character,connect};
