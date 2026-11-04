// Host-only diagnostics. Never issues RPC or copies console image/audio data.
const path=require('node:path'),files=require('./files.cjs'),telemetry=require('./telemetry.cjs');
function create({directory,stateRoot,clock=Date.now,log=console.error,budgetMs=250,maxQueuedEvents=128}){
 if(!Number.isSafeInteger(maxQueuedEvents)||maxQueuedEvents<1)throw Error('Invalid optional event queue limit');
 let row={session:path.basename(directory),stage:'starting',updated:new Date(clock()).toISOString()},lastRequest='',
  dropped=0,totalDropped=0,dropWarned=false;
 const events=[];
 const reportWriter=telemetry.create({clock,log,budgetMs,label:'Run11 diagnostics',
  write:()=>files.write(path.join(directory,'run11-diagnostics.json'),row)});
 function dropNotice(){
  if(!dropped||events.length>=maxQueuedEvents)return;
  events.push({type:'warning',message:'Optional Run11 event log dropped '+dropped+' records because its writer could not keep up.',
   extra:{droppedRecords:dropped,reason:'queue-limit'}});
  dropped=0;dropWarned=false;
 }
 const eventWriter=telemetry.create({clock,log,budgetMs,intervalMs:1,label:'Run11 event log',write:async()=>{
  while(events.length){const item=events.shift();await files.event(stateRoot,item.type,item.message,item.extra);dropNotice();}
 }});
 async function flush(force=false){await reportWriter.sample({force});}
 async function event(type,message,extra={}){
  dropNotice();
  if(events.length<maxQueuedEvents)events.push({type,message,extra});
  else{dropped++;totalDropped++;if(!dropWarned){dropWarned=true;try{log('Run11 event log: optional queue full; records are being dropped.');}catch{}}}
  await eventWriter.sample({force:true});
  // A producer may enqueue between the writer's empty-queue check and its
  // settlement callback. Start that queued work only after the old write ends.
  if(events.length&&!eventWriter.status.pending&&eventWriter.status.state==='ready')
   await eventWriter.sample({force:true});
 }
 return {
  async stage(stage,details={},force=false){row={...row,stage,operation:details,updated:new Date(clock()).toISOString()};await flush(force);},
  async observe(state,available=[]){
   const request=state.readUInt32LE(812),sequence=state.readUInt32LE(808),key=sequence+':'+request;
   row.console={selected:state.readUInt32LE(80),installed:state.readUInt32LE(112),picker:state.readUInt32LE(800)!==0,selection:state.readUInt32LE(804),sequence,request,busy:state.readUInt32LE(836)!==0,acknowledged:state.readUInt32LE(844),available:state.readUInt32LE(832)};
   row.lastStateAt=new Date(clock()).toISOString();
   if(request&&key!==lastRequest){const name=available[request-1]?.name||'Unknown package';row.packageRequest={name,request,sequence,at:row.lastStateAt};await event('package-request','Console requested '+name,{request,sequence});await flush(true);}
   lastRequest=key;
  },
  async fail(error){if(row.failure)return;const operation=row.stage;row.failure={error:files.clean(error.message),operation,at:new Date(clock()).toISOString()};row.stage='failed';row.updated=row.failure.at;await flush(true);await event('error','Run11 '+operation+': '+row.failure.error);},
  async cleanup(name,error){await event('cleanup-error','Cleanup '+name+': '+files.clean(error.message));},
  async complete(){row.stage='stopped';row.updated=new Date(clock()).toISOString();await flush(true);},
  status:{report:reportWriter.status,events:eventWriter.status,get queuedEvents(){return events.length;},get droppedEvents(){return totalDropped;}},
 };
}
// Cleanup must not replace the first operational error with ECONNREFUSED.
async function cleanup(primary,steps,report=async()=>{}){
 let first;
 for(const [name,action]of steps)try{await action();}catch(error){first||=error;try{await report(name,error);}catch{}}
 if(!primary&&first)throw first;
}
module.exports={create,cleanup};
