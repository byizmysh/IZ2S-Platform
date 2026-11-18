// Preserve press/release boundaries, while replacing stale queued motion.
function create(dispatch,onError=()=>{}){
 const queue=[],waiters=[];let running=false,closed=false;const report={received:0,coalesced:0,maxPending:0,dispatched:0};
 const settled=()=>{if(!running&&!queue.length)for(const resolve of waiters.splice(0))resolve();};
 async function drain(){if(running)return;running=true;try{while(queue.length&&!closed){const next=queue.shift();try{await dispatch(next);report.dispatched++;}catch(e){onError(e);}}}finally{running=false;settled();}}
 function push(m){if(closed)return;report.received++;const last=queue.at(-1);if(m.type==='touch'&&m.phase==='touchMove'&&last?.type==='touch'&&last.phase==='touchMove'){queue[queue.length-1]=m;report.coalesced++;}else queue.push(m);
  // A stalled renderer cannot create an unbounded IPC backlog.
  if(queue.length>64){queue.length=0;queue.push({type:'touch',phase:'touchCancel'});onError(Error('Input queue reset after renderer stall'));}
  report.maxPending=Math.max(report.maxPending,queue.length);drain();
 }
 return {push,report,idle(){return !running&&!queue.length?Promise.resolve():new Promise(resolve=>waiters.push(resolve));},close(){closed=true;queue.length=0;for(const resolve of waiters.splice(0))resolve();}};
}
module.exports={create};
