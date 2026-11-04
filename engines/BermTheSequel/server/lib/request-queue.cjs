// Reorder only requests that have not started. An executing native operation
// is indivisible; audio never interrupts it or runs alongside another request.
function create({now=()=>performance.now(),maxAudioBurst=2,normalMaxWaitMs=25}={}){
 const normal=[],audio=[],waiters=[];let running=false,burst=0;
 function pump(){
  if(running)return;
  if(!normal.length&&!audio.length){for(const resolve of waiters.splice(0))resolve();return;}
  const overdue=normal.length&&now()-normal[0].at>=normalMaxWaitMs;
  const urgent=audio.length&&!overdue&&(!normal.length||burst<maxAudioBurst);
  const item=(urgent?audio:normal).shift();burst=urgent?burst+1:0;running=true;
  Promise.resolve().then(item.task).then(item.resolve,item.reject).finally(()=>{running=false;pump();});
 }
 return {enqueue(task,{priority='normal'}={}){
  if(!['normal','audio'].includes(priority))return Promise.reject(Error('Invalid native request priority'));
  return new Promise((resolve,reject)=>{(priority==='audio'?audio:normal).push({task,resolve,reject,at:now()});pump();});
 },idle(){return running||normal.length||audio.length?new Promise(resolve=>waiters.push(resolve)):Promise.resolve();}};
}
module.exports={create};
