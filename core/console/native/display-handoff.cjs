// A native application borrows the display at a joined frame boundary while
// the graphics host and its supervising connection remain alive.
const fs=require('node:fs/promises'),path=require('node:path');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function write(file,value){const temp=file+'.host.tmp';await fs.writeFile(temp,JSON.stringify(value,null,2)+'\n',{mode:0o600});await fs.rename(temp,file);}
function create({directory,session,stopWorker,startWorker,report,save,ownerAlive=pid=>{try{process.kill(pid,0);return true;}catch{return false;}},interval=100}){
 const file=path.join(directory,'native-display-handoff.json');
 return async function atBoundary(){
  let request;try{request=JSON.parse(await fs.readFile(file));}catch(e){if(e.code==='ENOENT')return false;throw e;}
  if(request.state!=='requested')return false;
  if(request.version!==1||request.session!==session||!Number.isSafeInteger(request.ownerPid)||request.ownerPid<=0||!/^[a-f0-9]{32}$/.test(request.token)||!ownerAlive(request.ownerPid))throw Error('Invalid native display handoff owner');
  const runtime=JSON.parse(await fs.readFile(path.join(directory,'app-runtime.json')));
  if(runtime.active?.ownerPid!==request.ownerPid)throw Error('Native display handoff lacks the current application reservation');
  await stopWorker();
  const state={...request,state:'paused',hostPid:process.pid,pausedAt:new Date().toISOString(),workerJoined:true};
  report.nativeHandoff={...state};await save();await write(file,state);
  for(;;){
   await pause(interval);
   let latest;try{latest=JSON.parse(await fs.readFile(file));}catch{continue;}
   if(latest.token!==request.token||latest.session!==session||latest.ownerPid!==request.ownerPid)continue;
   // An abandoned owner leaves the joined worker paused. There is no automatic
   // resume while an application could still own the graphics queue or memory.
   if(latest.state!=='release'||latest.cleanupKnown!==true)continue;
   await startWorker();
   const resumed={...state,state:'resumed',cleanupKnown:true,resumedAt:new Date().toISOString()};
   report.nativeHandoff={...resumed};await save();await write(file,resumed);return true;
  }
 };
}
module.exports={create};
