// Capture each snapshot before enqueueing. A later strict save cannot be
// overtaken by an older optional report, even across the public helper modules.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const pending=new Map();
function write(file,value,{directoryMode}={}){
 let bytes;try{bytes=Buffer.from(JSON.stringify(value,null,2)+'\n');}catch(error){return Promise.reject(error);}
 const key=path.resolve(file),previous=pending.get(key)||Promise.resolve();
 const task=previous.catch(()=>{}).then(async()=>{
  await fs.mkdir(path.dirname(file),{recursive:true,...(directoryMode===undefined?{}:{mode:directoryMode})});
  const temporary=file+'.'+crypto.randomUUID()+'.tmp';
  try{await fs.writeFile(temporary,bytes,{mode:0o600});await fs.rename(temporary,file);}
  catch(error){await fs.rm(temporary,{force:true}).catch(()=>{});throw error;}
 });
 pending.set(key,task);task.finally(()=>{if(pending.get(key)===task)pending.delete(key);}).catch(()=>{});
 return task;
}
module.exports={write};
