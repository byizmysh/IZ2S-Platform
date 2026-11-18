// One cancellable consumer of the latest frame. Publication is an event;
// hidden/paused sources wait without a timer or a growing frame queue.
function create(read){
 let pending=null,closed=false;const stats={waits:0,wakes:0,aborts:0,notifications:0};
 function inspect(request){const state=read();if(closed||state.closed||request.sourceEpoch!==undefined&&state.sourceEpoch!==request.sourceEpoch)return {ready:true,value:null};if(state.error)return {ready:true,error:Error(String(state.error))};const frame=state.frame;if(!state.hidden&&!state.suspended&&!state.reconfiguring&&frame&&frame.sequence!==request.afterSequence)return {ready:true,value:frame};return {ready:false};}
 function finish(result){const own=pending;if(!own)return;pending=null;own.signal?.removeEventListener('abort',own.abort);stats.wakes++;if(result.error)own.reject(result.error);else own.resolve(result.value);}
 function notify(){stats.notifications++;if(pending){const result=inspect(pending);if(result.ready)finish(result);}}
 function waitFrame({afterSequence=-1,sourceEpoch,signal}={}){
  if(signal?.aborted)return Promise.reject(Object.assign(Error('Frame wait cancelled'),{name:'AbortError'}));
  if(pending)return Promise.reject(Error('A frame waiter is already active'));
  const request={afterSequence,sourceEpoch,signal},result=inspect(request);if(result.ready)return result.error?Promise.reject(result.error):Promise.resolve(result.value);
  stats.waits++;return new Promise((resolve,reject)=>{const abort=()=>{if(pending?.abort!==abort)return;stats.aborts++;finish({error:Object.assign(Error('Frame wait cancelled'),{name:'AbortError'})});};pending={...request,resolve,reject,abort};signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();else notify();});
 }
 function close(){closed=true;finish({value:null});}
 return {waitFrame,notify,close,stats};
}
module.exports={create};
