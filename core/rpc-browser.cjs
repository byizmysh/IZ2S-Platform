const fs = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
async function open(root, request = {}, {launch = spawn,
  findSession = require('../tools/dolphin-fs-rpc/library.cjs').findSession,
  register = require('./workspace.cjs').register} = {}) {
  const session = await findSession(root, undefined, {standalone:false});
  if (request.attemptId && request.attemptId !== session.id) throw Error('The console session changed. Reopen Super File Bros.');
  if (!/^[a-zA-Z0-9-]{1,80}$/.test(session.id || '')) throw Error('Invalid active attempt');
  const previous=(await require('./workspace.cjs').list(root)).find(tab=>tab.id==='files');
  if(request.automatic && previous?.state==='stopped' && previous.sessionId===session.id)
    return {ok:true,skipped:true,tab:previous};
  const script = path.join(root,'tools/fs-rpc-browser/start.cjs');
  await fs.access(script);
  const listener = await new Promise((resolve,reject) => {
    const child = launch(process.execPath,[script,'--no-open'],{cwd:root,detached:true,
      stdio:['ignore','ignore','pipe','ipc'],env:{...process.env,NS2_FS_BROWSER:'0'}});
    let detail='',settled=false;
    const finish = (error,result) => {
      if (settled) return; settled=true; clearTimeout(timer);
      child.off('message',message);child.off('close',closed);child.off('error',failed);
      child.stderr?.unref?.();child.unref();error?reject(error):resolve(result);
    };
    const failed = error => finish(error);
    const message = value => {if(value?.type==='rpc-browser-opened') {
      try {require('./workspace.cjs').localUrl(value.url);finish(null,value);}catch(error){finish(error);}
    }};
    const closed = () => finish(Error('Could not start Super File Bros. '+detail.trim()));
    const timer=setTimeout(()=>finish(Error('Super File Bros. startup timed out. Open Super File Bros. to retry.')),12000);
    child.stderr?.on('data',bytes=>{detail=(detail+bytes).slice(-1200);});
    child.once('error',failed);child.once('close',closed);child.on('message',message);
  });
  const tab = await register(root,{id:'files',title:'Super File Bros.',kind:'files',url:listener.url,ownerPid:listener.pid,sessionId:session.id,
    automatic:request.automatic===true,...(request.automatic?{requestId:'files-'+session.id}:{})});
  return {ok:true,attemptId:session.id,tab};
}
async function onSessionReady(root,row,{preview=false,openPage=open}={}) {
  if(preview||row?.status!=='running'||!row.runDirectory)return {ok:true,skipped:true};
  let state;
  try {state=JSON.parse(await fs.readFile(path.join(row.runDirectory,'fs-rpc-state.json'),'utf8'));}
  catch(error){
    if(error.code!=='ENOENT')throw error;
    try {const result=JSON.parse(await fs.readFile(path.join(row.runDirectory,'result.json'),'utf8'));state={state:result.verified&&result.rpc?.state==='listening'?'ready':'unavailable'};}
    catch(e){if(e.code==='ENOENT')return {ok:true,skipped:true};throw e;}
  }
  return state.state==='ready'?openPage(root,{automatic:true,attemptId:row.id}):{ok:true,skipped:true};
}
module.exports={open,onSessionReady};
