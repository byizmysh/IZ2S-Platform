'use strict';
const {randomUUID}=require('node:crypto');
function create(){
 let current=null;
 return {
  snapshot:()=>current?{id:current.id,kind:current.kind,stopping:current.controller.signal.aborted,...current.progress}:null,
  progress(value){if(current)current.progress=value;},
  cancel(id){
   if(!current)return {ok:true,running:false};
   if(id&&id!==current.id)throw Error('The preparation operation changed. Refresh its status before cancelling.');
   current.controller.abort();return {ok:true,running:true,cancelling:true};
  },
  async run({id=randomUUID(),kind},work){
   if(current)throw Error('Wait for the current preparation operation to finish');
   const job={id,kind,controller:new AbortController(),progress:{}};current=job;
   try{return await work(job.controller.signal);}
   catch(error){if(job.controller.signal.aborted&&error.name==='AbortError')return {ok:false,cancelled:true};throw error;}
   finally{if(current===job)current=null;}
  },
 };
}
module.exports={create};
