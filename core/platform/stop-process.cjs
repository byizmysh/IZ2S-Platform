'use strict';
const path=require('node:path'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
const execute=promisify(execFile),pending=new WeakMap();
// Windows kill(SIGINT) terminates only the direct process, without running its
// signal handlers. Stop this owned process tree so its TCP listener cannot leak.
function stop(child,{platform=process.platform,signal='SIGINT',run=execute}={}){
 if(!child||child.exitCode!==null||child.signalCode!==null)return Promise.resolve(false);
 if(pending.has(child))return pending.get(child);
 if(platform!=='win32')return Promise.resolve().then(()=>child.kill(signal));
 if(!Number.isSafeInteger(child.pid)||child.pid<2)return Promise.reject(Error('No live child process to stop.'));
 const command=path.win32.join(process.env.SystemRoot||'C:\\Windows','System32','taskkill.exe');
 const request=run(command,['/PID',String(child.pid),'/T','/F'],{windowsHide:true,timeout:10000,maxBuffer:65536})
  .then(()=>true).catch(error=>{if(child.exitCode!==null||child.signalCode!==null)return false;throw error;})
  .finally(()=>pending.delete(child));
 pending.set(child,request);return request;
}
module.exports={stop};
