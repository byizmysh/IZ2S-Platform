// At most one prepared frame for each alternating dictionary. The inactive
// slot can be encoded while the other slot waits for its network/GPU ACK.
const path=require('node:path'),{Worker}=require('node:worker_threads'),D=require('./block-delta.cjs');
function compressor({codecPath=null,workerFactory=options=>new Worker(path.join(__dirname,'block-delta-worker.cjs'),options)}={}){
 let id=0,closed=false;const jobs=new Map(),reservations=new Map(),worker=workerFactory({workerData:{codecPath}});
 function settle(job,error,value){clearTimeout(job.timer);jobs.delete(job.id);if(error&&reservations.get(job.slot)?.id===job.id)reservations.delete(job.slot);if(error)job.reject(error);else job.resolve(value);job.complete();}
 function fail(error){for(const job of [...jobs.values()])settle(job,error);reservations.clear();}
 worker.on('error',error=>{closed=true;fail(error);});worker.on('exit',code=>{closed=true;fail(Error('Delta encoder exited: '+code));});
 worker.on('message',message=>{
  const job=jobs.get(message.id);if(!job)return;
  if(job.discarded)return settle(job,Object.assign(Error('Delta frame discarded'),{code:'DELTA_FRAME_DISCARDED',recoverable:true}));
  if(message.error)return settle(job,Error(message.error));
  const reservation=reservations.get(job.slot);if(!reservation||reservation.id!==job.id)return settle(job,Error('Delta frame reservation differs'));
  reservation.state='prepared';settle(job,null,{...message,packet:Buffer.from(message.packet.buffer,message.packet.byteOffset,message.packet.byteLength)});
 });
 function discard(slot){
  const reservation=reservations.get(slot);if(!reservation)return Promise.resolve();const job=jobs.get(reservation.id);
  reservation.discarded=true;if(job)job.discarded=true;reservations.delete(slot);
  if(!closed)worker.postMessage({command:'discard',slot});return job?job.settled:Promise.resolve();
 }
 return {
  reset(bytes){if(closed||jobs.size)throw Error('Delta encoder unavailable or busy');D.layout(bytes);reservations.clear();worker.postMessage({command:'reset',bytes});},
  commit(slot){const reservation=reservations.get(slot);if(closed||!reservation||reservation.state!=='prepared'||reservation.discarded)throw Error('Delta dictionary commit differs');reservations.delete(slot);worker.postMessage({command:'commit',slot});},
  discard,
  discardAll(){const pending=[...jobs.values()].map(job=>job.settled);return Promise.all([...pending,...[...reservations.keys()].map(discard)]).then(()=>{});},
  encode(data){
   if(closed)return Promise.reject(Error('Delta encoder unavailable or busy'));
   if(data.slot!==0&&data.slot!==1)return Promise.reject(Error('Invalid dictionary slot'));
   if(reservations.has(data.slot)||jobs.size>=2)return Promise.reject(Error('Previous delta frame for this slot has not been acknowledged'));
   return new Promise((resolve,reject)=>{
    const frame=Uint8Array.from(data.frame),message={...data,frame,blockBytes:D.BLOCK,id:++id};let complete;const settled=new Promise(r=>complete=r),job={id,slot:data.slot,resolve,reject,settled,complete};
    jobs.set(id,job);reservations.set(data.slot,{id,state:'encoding'});job.timer=setTimeout(()=>{closed=true;fail(Error('Delta compression timed out'));worker.terminate();},5000);
    try{worker.postMessage(message,[frame.buffer]);}catch(error){settle(job,error);}
   });
  },
  async close(){if(closed){fail(Error('Delta encoder closed'));await worker.terminate();return;}closed=true;fail(Error('Delta encoder closed'));await worker.terminate();}
 };
}
module.exports={compressor};
