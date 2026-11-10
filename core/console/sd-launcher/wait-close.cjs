'use strict';
// Only observes an already requested stop. No stop replay, join or release.
async function wait({tryWait,progress,sleep,clock=()=>performance.now(),idleMs=10000,maxMs=360000}){
 const start=clock();let lastAdvance=start,lastProbe=start,seen=-1,saving=false;
 while(clock()-start<maxMs){
  const result=await tryWait();if(result===1n)return;if(result!==0n)throw Object.assign(Error('Invalid SD worker wait result'),{resourcesRetained:true});
  const now=clock();if(now-lastProbe>=500){
   lastProbe=now;const state=await progress();
   if(state.saving){
    if(!Number.isSafeInteger(state.done)||state.done<0||state.done<seen)throw Object.assign(Error('Invalid SD checkpoint progress'),{resourcesRetained:true});
    if(!saving||state.done>seen)lastAdvance=clock();seen=state.done;saving=true;
   }else if(saving){saving=false;lastAdvance=clock();}
  }
  if(clock()-lastAdvance>=idleMs)break;await sleep(250);
 }
 throw Object.assign(Error('SD supervisor has not stopped; allocations retained'),{resourcesRetained:true});
}
module.exports={wait};
