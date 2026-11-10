'use strict';
// An acknowledged transfer is console activity even while rendering is paused.
// Queued jobs, PC preparation and stale telemetry never prove a live connection.
function connected({status,runtime,link,jobs=[],now=Date.now()}) {
  if(status!=='running'||!runtime?.menuReady)return false;
  if(link&&link.state!=='connected')return false;
  let contact=Date.parse(runtime.updated||0);
  for(const job of jobs){
    const p=job.progress;
    if(!['loading','publishing'].includes(job.status)||!['transfer','sd-write','sd-verify'].includes(p?.phase)||!(p.completedBytes>0))continue;
    const at=Date.parse(p.updated);
    if(Number.isFinite(at)&&at<=now)contact=Math.max(contact||0,at);
  }
  return Number.isFinite(contact)&&contact<=now&&now-contact<6000&&
    (!link||![2,3].includes(link.protocol)||contact>=Date.parse(link.at||0));
}
module.exports={connected};
