// Physical output mode selects a required adapter; neither property implies
// the other. State events wake consumers without a video or RPC polling loop.
const net=require('node:net');
function describe(state,{enabled=true,now=Date.now(),maxAgeMs=6000,allowDockWifi=false}={}){
 if(!enabled)return {ready:true,code:0,required:null,reason:'route-check-disabled'};
 const age=now-Date.parse(state?.at),mode=['docked','handheld'].includes(state?.mode)?state.mode:null,required=mode==='docked'?(allowDockWifi?'ethernet-or-wifi':'ethernet'):mode==='handheld'?'wifi':null;
 const fresh=state?.stale===false&&Number.isFinite(age)&&age>=0&&age<maxAgeMs;
 const connected=state?.network?.connected===true&&net.isIPv4(state.network.ip)&&state.network.ip!=='0.0.0.0';
 const accepted=required==='ethernet-or-wifi'?['ethernet','wifi'].includes(state?.network?.type):state?.network?.type===required;
 const ready=!!(fresh&&required&&connected&&accepted);
 return {ready,mode,required,network:state?.network?.type||null,ip:connected?state.network.ip:null,code:ready?0:!fresh||!mode?3:mode==='docked'?1:2,reason:ready?'physical-route-ready':!fresh||!mode?'waiting-for-console-state':mode==='docked'?(allowDockWifi?'waiting-for-dock-network':'waiting-for-ethernet'):'waiting-for-wifi'};
}
function create({enabled=true,allowDockWifi=()=>false,onChange=async()=>{},now=Date.now,maxAgeMs=6000,schedule=setTimeout,cancel=clearTimeout}={}){
 let state=null,current={...describe(null,{enabled,now:now(),maxAgeMs,allowDockWifi:typeof allowDockWifi==='function'?allowDockWifi():allowDockWifi}),generation:0},closed=false,timer=null,barrier=Promise.resolve(),failure=null,settledGeneration=0;const waiters=new Set();
 const key=value=>JSON.stringify([value.ready,value.mode,value.required,value.network,value.ip,value.code]);
 const error=()=>Object.assign(Error(current.reason==='waiting-for-ethernet'?'Waiting for the dock Ethernet connection':current.reason==='waiting-for-dock-network'?'Waiting for the dock Ethernet or Wi-Fi connection':current.reason==='waiting-for-wifi'?'Waiting for the portable Wi-Fi connection':'Waiting for fresh console mode and network state'),{code:'FRAME_ROUTE_PENDING',recoverable:true,retryAfterMs:100});
 function wake(){for(const fn of [...waiters])fn();}
 function update(next){
  if(closed)return barrier;state=next;if(timer){cancel(timer);timer=null;}
  const value=describe(state,{enabled,now:now(),maxAgeMs,allowDockWifi:typeof allowDockWifi==='function'?allowDockWifi():allowDockWifi});
  if(enabled&&state?.stale===false&&Number.isFinite(Date.parse(state.at))){const remaining=Date.parse(state.at)+maxAgeMs-now();if(remaining>0){timer=schedule(()=>{timer=null;update(state);},remaining);timer?.unref?.();}}
  if(key(value)===key(current))return barrier;
  const previous=current;current={...value,generation:current.generation+1};const own=current;
  barrier=barrier.then(()=>{if(!closed)return onChange(own,previous);}).catch(e=>{failure=e;}).then(()=>{settledGeneration=own.generation;wake();});return barrier;
 }
 function allowNow(){if(closed||failure||!current.ready||settledGeneration!==current.generation)return false;const fresh=describe(state,{enabled,now:now(),maxAgeMs,allowDockWifi:typeof allowDockWifi==='function'?allowDockWifi():allowDockWifi});if(key(fresh)!==key(current)){update(state);return false;}return true;}
 async function ready(){for(;;){const own=barrier;await own;if(own!==barrier)continue;if(closed)throw Object.assign(Error('Streaming route gate is closed'),{name:'AbortError'});if(failure)throw failure;const fresh=describe(state,{enabled,now:now(),maxAgeMs,allowDockWifi:typeof allowDockWifi==='function'?allowDockWifi():allowDockWifi});if(key(fresh)!==key(current)){await update(state);continue;}if(!current.ready)throw error();return {...current};}}
 function wait({signal}={}){
  return new Promise((resolve,reject)=>{let settled=false;const finish=(e,value)=>{if(settled)return;settled=true;waiters.delete(check);signal?.removeEventListener('abort',abort);e?reject(e):resolve(value);};const abort=()=>finish(Object.assign(Error('Route wait cancelled'),{name:'AbortError'}));const check=()=>{ready().then(value=>finish(null,value),e=>{if(!e.recoverable)finish(e);});};waiters.add(check);signal?.addEventListener('abort',abort,{once:true});if(signal?.aborted)abort();else check();});
 }
 function close(){closed=true;if(timer)cancel(timer);timer=null;wake();}
 return {update,refresh:()=>update(state),ready,wait,close,allowNow,drain:()=>barrier,snapshot:()=>({...current})};
}
module.exports={describe,create};
