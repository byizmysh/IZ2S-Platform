// A native application borrows the display at a joined frame boundary while
// the graphics host and its supervising connection remain alive.
const fs=require('node:fs/promises'),path=require('node:path');
const reservation=require('./display-reservation.cjs');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function write(file,value){const temp=file+'.host.tmp';await fs.writeFile(temp,JSON.stringify(value,null,2)+'\n',{mode:0o600});await fs.rename(temp,file);}
function create({directory,session,stopWorker,startWorker,report,save,ownerAlive=pid=>{try{process.kill(pid,0);return true;}catch{return false;}},interval=100}){
 const file=path.join(directory,'native-display-handoff.json');
 return async function atBoundary(){
  let request;try{request=JSON.parse(await fs.readFile(file));}catch(e){if(e.code==='ENOENT')return false;throw e;}
  if(request.state!=='requested')return false;
  const reject=async reason=>{await write(file,{...request,state:'rejected',notStarted:true,reason});return false;};
  // Admission has not touched the renderer. Refuse a stale/invalid requester
  // without terminating the otherwise healthy launcher and its connection.
  if(request.version!==1||request.session!==session||!Number.isSafeInteger(request.ownerPid)||request.ownerPid<=0||!/^[a-f0-9]{32}$/.test(request.token)||!ownerAlive(request.ownerPid))return reject('Invalid native display handoff owner');
  const runtime=request.reservation===1?null:JSON.parse(await fs.readFile(path.join(directory,'app-runtime.json')));
  let reserved;try{reserved=await reservation.matches(directory,request,runtime);}catch(error){if(!(error instanceof SyntaxError))throw error;return reject('Native display reservation is invalid');}
  if(!reserved)return reject('Native display handoff lacks the current application reservation');
  try{await stopWorker();}catch(error){
   if(!error.menuBusy||error.resourcesRetained)throw error;
   await write(file,{...request,state:'rejected',notStarted:true,reason:error.message,...(error.activeApplication?{activeApplication:error.activeApplication}:{})});return false;
  }
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
