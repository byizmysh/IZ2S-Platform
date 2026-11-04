// Optional host telemetry only. Ownership, transitions and cleanup use strict saves.
const {clean}=require('./files.cjs');
const storageCodes=new Set(['ENOSPC','EDQUOT','EIO','EROFS','EACCES','EPERM','ENOENT','ENOTDIR','EISDIR','EMFILE','ENFILE','EBUSY','ESTALE']);
function create({write,label='Host telemetry',clock=Date.now,intervalMs=1000,retryMs=10000,budgetMs=250,log=console.error}){
 if(typeof write!=='function'||!Number.isFinite(intervalMs)||intervalMs<1||!Number.isFinite(retryMs)||retryMs<intervalMs||!Number.isFinite(budgetMs)||budgetMs<1)throw Error('Invalid telemetry writer settings');
 const status={state:'ready',attempts:0,writes:0,failures:0,error:null,retryAt:null,pending:false};
 let nextAttempt=-Infinity,pending=null,failed=false,lateError=null;
 function note(message){try{log(label+': '+message);}catch{}}
 function delayed(message){if(!failed){failed=true;note(message);}}
 function settle(record,result){
  record.settled=true;if(pending===record){pending=null;status.pending=false;}
  if(result.ok){
   status.writes++;status.state='ready';status.error=null;status.retryAt=null;
   if(failed){failed=false;note('report writing recovered.');}
  }else if(storageCodes.has(result.error?.code)){
   const error=result.error;nextAttempt=clock()+retryMs;status.state='delayed';status.failures++;
   status.error={code:error.code,message:clean(error.message).slice(0,400)};status.retryAt=nextAttempt;
   delayed('report writing delayed ('+error.code+'); the launcher remains active.');
  }else{
   status.state='error';status.retryAt=null;status.error={code:result.error?.code||'EUNEXPECTED',message:clean(result.error?.message||result.error).slice(0,400)};
   if(record.timedOut)lateError={error:result.error};
  }
  return result;
 }
 async function sample({force=false}={}){
  if(lateError){const {error}=lateError;lateError=null;throw error;}
  const now=clock();if(pending||(now<nextAttempt&&!(force&&status.state==='ready')))return false;
  const record={settled:false,timedOut:false};pending=record;status.pending=true;
  nextAttempt=now+intervalMs;status.attempts++;
  // The task always settles to data. A late rejection has an owner even after
  // this sample's wait budget expires; no replacement write starts meanwhile.
  let operation;try{operation=write();}catch(error){operation=Promise.reject(error);}
  record.task=Promise.resolve(operation).then(
   ()=>settle(record,{ok:true}),error=>settle(record,{ok:false,error}),
  );
  let timer;
  try{
   const result=await Promise.race([record.task,new Promise(resolve=>{
    timer=setTimeout(()=>{
     if(!record.settled){record.timedOut=true;status.state='stalled';status.retryAt=null;
      status.error={code:'ETIMEDOUT',message:'Optional report write exceeded its '+budgetMs+' ms wait budget.'};
      delayed('report writing stalled; the launcher remains active and no replacement write will be queued.');}
     resolve(null);
    },budgetMs);
   })]);
   if(!result)return false;
   if(!result.ok&&!storageCodes.has(result.error?.code))throw result.error;
   return result.ok;
  }finally{clearTimeout(timer);}
 }
 return {sample,status};
}
module.exports={create};
