'use strict';

const assert=require('node:assert/strict');
const fs=require('node:fs/promises');
const path=require('node:path');
const {spawn}=require('node:child_process');
const {randomUUID}=require('node:crypto');
const readline=require('node:readline');
const {resolve}=require('../../../core/module-files.cjs');
const runtime=require('../runtime.cjs');
const active=new Map();
const vacooId='vacootube', idleMs=8000, retryMs=10000;

async function windows(){
  const capture = require(resolve('blitbun', 'server/lib/native-window-capture.cjs'));
  const {windows: rows} = await capture.list();
  return rows.filter(row => Number.isSafeInteger(row.id) && row.id > 0 && capture.capturable(row)).map(row => ({
    id:row.id,title:String(row.title||'Window').slice(0,240),
  }));
}

async function start(id,selection,out,{existingLink=false,onReady,onStable,onEnd}={}){
  assert(id==='blitbun'||id==='vacootube','Unknown PC source.');
  assert(id!=='blitbun'||Number.isSafeInteger(selection)&&selection>0,'Choose the window to share.');
  const previous=active.get(id);
  assert(!previous?.stopping,'Wait for the PC source to finish stopping.');
  if(previous&&previous.selection===selection)return previous.ready;
  assert(!previous,'Stop the current source before choosing another window.');
  const logDirectory=runtime.logs(out);
  const row={selection,child:null,ready:null,stopping:false};
  active.set(id,row);
  row.ready=(async()=>{
    await fs.mkdir(logDirectory,{recursive:true});
    const log=await fs.open(path.join(logDirectory,id+'-source.log'),'a',0o600);
    if(row.stopping){await log.close();throw Error('The PC source was stopped.');}
    const child=spawn(process.execPath,[path.join(__dirname,'pc-media-source.cjs'),id,...(id==='blitbun'?[String(selection)]:[])],{
      windowsHide:true,stdio:['ignore','pipe',log.fd],env:{...process.env,IZ2S_CORE_OUT:runtime.output(out),
        IZ2S_PC_MEDIA_EXISTING_LINK:existingLink?'1':'0'},
    });
    row.child=child;
    void log.close().catch(()=>{});
    return new Promise((resolve,reject)=>{
      let ready=false,reason,ended=false;
      function finish(error){
        if(ended)return;
        ended=true;
        clearTimeout(timer);lines.close();
        if(active.get(id)===row)active.delete(id);
        if(!ready)reject(error);
        if(!row.stopping)onEnd?.(error);
      }
      const lines=readline.createInterface({input:child.stdout});
      const timer=setTimeout(()=>{
        reason=Error('The PC source did not produce video.');
        child.kill('SIGTERM');
      },25000);
      child.once('error',finish);
      child.once('close',()=>finish(reason??Error('The PC source stopped. See /data/log/'+id+'-source.log.')));
      lines.on('line',line=>{
        try{
          const record=JSON.parse(line);
          if(record.source!==id)return;
          if(record.state==='publishing media'&&!row.stopping){
            ready=true;clearTimeout(timer);onReady?.(record.audio);resolve({running:true,audio:record.audio});
          }else if(record.state==='stable')onStable?.();
          else if(record.state==='error'&&typeof record.error==='string')reason=Error(record.error);
        }catch{/* Capture diagnostics are separate from the readiness message. */}
      });
    });
  })().catch(error=>{if(!row.child&&active.get(id)===row)active.delete(id);throw error;});
  return row.ready;
}

function stop(id){
  assert(id==='blitbun'||id==='vacootube','Unknown PC source.');
  const row=active.get(id);
  if(row)row.stopping=true;
  row?.child?.kill('SIGTERM');
  return {stopping:!!row};
}

function sourceDemand(id,out,{launch=(id,selection,folder,events)=>start(id,selection,folder,{existingLink:true,...events}),
  halt=stop,isActive=()=>active.has(id),report=console.error,
  idleTimeout=idleMs,retryTimeout=retryMs}={}){
  let timer=null,retry=null,selection,automatic=false,enabled=false,mutedByUser=false,closed=false,
    recovered=false,generation=0,phase='idle',error=null,audio=null,pending=null,windowRequest=null;
  function clearRetry(){clearTimeout(retry);retry=null;}
  function stopSource(){
    generation++;clearRetry();enabled=automatic=false;phase='idle';audio=error=null;
    return halt(id);
  }
  function idle(){
    timer=null;
    windowRequest=null;
    mutedByUser=false;
    if(automatic)stopSource();
  }
  function launchSource(){
    const current=++generation;
    phase='starting';audio=null;
    const failed=reason=>{
      if(current!==generation||!enabled||closed||phase==='retrying'||phase==='failed')return;
      error=reason.message;
      report(id+' source:',error);
      if(recovered){phase='failed';return;}
      recovered=true;phase='retrying';
      retry=setTimeout(()=>{
        retry=null;
        if(current===generation&&enabled&&!closed)void launchSource().catch(()=>{});
      },retryTimeout);
      retry.unref?.();
    };
    pending=Promise.resolve().then(()=>{
      if(current!==generation||!enabled||closed)throw Error('The PC source was stopped.');
      return launch(id,selection,out,{
      onReady(value){if(current===generation){phase='running';audio=value??null;error=null;}},
      onStable(){if(current===generation)recovered=false;},
      onEnd:failed,
      });
    }).then(value=>{
      if(current===generation&&phase==='starting'){phase='running';audio=value?.audio??null;error=null;}
      return value;
    },reason=>{failed(reason);throw reason;});
    return pending;
  }
  function request(hasVideo){
    if(closed)return;
    if(timer)timer.refresh();
    else{
      timer=setTimeout(idle,idleTimeout);
      timer.unref();
    }
    if(hasVideo||mutedByUser||enabled||isActive())return;
    if(id==='blitbun'){
      windowRequest??=randomUUID();
      return;
    }
    enabled=automatic=true;recovered=false;
    void launchSource().catch(()=>{});
  }
  function manualStart(window){
    assert(!closed,'PC Link is shutting down.');
    assert(id!== 'blitbun'||Number.isSafeInteger(window)&&window>0,'Choose the window to share.');
    if(enabled&&isActive()&&selection!==window)throw Error('Stop the current source before choosing another window.');
    if(enabled&&['running','starting'].includes(phase))return phase==='starting'?pending:{running:true,audio};
    clearRetry();
    windowRequest=null;
    selection=window;
    automatic=false;
    enabled=true;recovered=false;mutedByUser=false;
    return launchSource();
  }
  function manualStop(){
    windowRequest=null;
    if(timer)mutedByUser=true;
    return stopSource();
  }
  function close(){
    closed=true;
    clearTimeout(timer);
    manualStop();
  }
  return {request,start:manualStart,stop:manualStop,close,status:()=>({phase,audio,error,windowRequest})};
}

const vacooDemand=(out,options)=>sourceDemand(vacooId,out,options);
async function requestMedia(id,action,out,selection){
  assert(id==='blitbun'||id===vacooId);
  assert(action==='start'||action==='stop');
  const service=require('./pc-link-service.cjs');
  const state=action==='start'?await service.ensure(out):await service.status();
  if(!state&&action==='stop')return stop(id);
  assert(state?.service?.root===path.resolve(__dirname,'../../..'),'Start PC Link from this 0.3.0 installation.');
  const origin='http://127.0.0.1:39525';
  const response=await fetch(origin+'/media/'+id+'/'+action,{
    method:'POST',headers:{origin,'content-type':'application/json'},body:JSON.stringify({window:selection}),
    signal:AbortSignal.timeout(28000),
  });
  if(response.status===404)return action==='start'?start(id,selection,out):stop(id);
  const result=await response.json();
  if(!response.ok)throw Error(result.error||'PC Link could not control the video source.');
  return result;
}

module.exports={windows,start,stop,vacooDemand,sourceDemand,requestMedia};
