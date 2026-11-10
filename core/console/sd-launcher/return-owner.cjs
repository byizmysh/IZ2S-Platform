'use strict';
// A temporary native app returns the parent menu without waiting for a human
// to close its host CLI. Only a fresh console observation and joined cleanup
// release ownership; PID death, a stale file and timeouts never do.
function create({observe,holdMenu,close,release,save=async()=>{},intervalMs=500,retryMs=2000}){
 const report={enabled:true,state:'waiting',observations:0,cleanupAttempts:0,released:false};let stopped=false,timer,running;
 async function persist(){try{await save(report);}catch(error){report.telemetryError=error.message;}}
 async function check(){
  if(stopped)return report;
  let state;try{state=await observe();report.observations++;}
  catch(error){report.state='unverified';report.error=error.message;return report;}
  if(stopped)return report;
  if(!state||state.state!==2||state.poisoned||state.error){report.state='retained';report.error='Native application cleanup is not verified';stopped=true;await persist();return report;}
  if(state.active||!Number.isSafeInteger(state.transitions)||state.transitions<2){report.state=state.active?'active':'waiting';return report;}
  try{
   const lease=await holdMenu();if(stopped){await lease.release({rescan:false});return report;}
   if(!lease?.active)throw Error('Native menu ownership was not acknowledged');
   report.state='closing';report.cleanupAttempts++;await close();
   // close has joined the exact worker, freed its graphics resources and
   // restored its borrowed FS capability. The lease is consumed by close.
   report.state='returning';await release();report.state='released';report.released=true;report.releasedAt=new Date().toISOString();stopped=true;await persist();
  }catch(error){if(stopped&&report.state!=='closing'&&report.state!=='returning')return report;if(error.menuBusy&&!error.resourcesRetained){report.state='active';return report;}report.state='retained';report.error=error.message;stopped=true;await persist();}
  return report;
 }
 function step(){if(running)return running;const work=check();running=work;return work.finally(()=>{if(running===work)running=null;});}
 function schedule(){if(stopped)return;timer=setTimeout(()=>{timer=null;step().catch(async error=>{report.state='retained';report.error=error.message;stopped=true;await persist();}).finally(schedule);},report.state==='unverified'?retryMs:intervalMs);}
 return {report,step,start(){if(!timer&&!stopped)schedule();},stop(){stopped=true;clearTimeout(timer);timer=null;}};
}
module.exports={create};
