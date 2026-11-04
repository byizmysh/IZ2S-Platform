// User-opened module settings have a lifetime independent of console sessions.
const path=require('node:path'),fs=require('node:fs/promises');
const active=new Map(),pending=new Map(),closeTasks=new WeakMap();
function closeHandle(handle){
 if(!closeTasks.has(handle))closeTasks.set(handle,Promise.resolve().then(()=>handle.close()));
 return closeTasks.get(handle);
}
async function open(root,id){
 root=path.resolve(root);const key=root+'\0'+id;
 if(pending.has(key))return pending.get(key);
 const task=(async()=>{
  const previous=active.get(key);
  if(previous){
   if(await require('./platform/listener.cjs').listening(previous.url)!==false)return {url:previous.url};
   try{await closeHandle(previous);}catch(error){if(error.resourcesRetained||error.code!=='ERR_SERVER_NOT_RUNNING')throw error;}
   if(active.get(key)===previous)active.delete(key);
  }
  const module=await require('./modules.cjs').read(root,id),settings=module.descriptor.server?.settings;
  if(!settings?.entry)throw Error('This module does not provide settings');
  await require('./modules.cjs').verify(root,[id]);
  const entry=require(require('./descriptors.cjs').local(module.folder,settings.entry));
  if(typeof entry.open!=='function')throw Error('Module settings must export open');
  const home=require('./module-paths.cjs').home(root,id),dataDirectory=path.join(home,'data');
  await fs.mkdir(dataDirectory,{recursive:true});
  const handle=await entry.open({root,moduleHome:home,dataDirectory,sdStorage:require('./module-storage.cjs').forModule(id),services:require('../sdk/platform.cjs').forModule(root,id,{kind:'settings'})});
  const url=new URL(handle.url);
  if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||typeof handle.close!=='function'){await handle.close?.();throw Error('Invalid module settings listener');}
  active.set(key,handle);return {url:handle.url};
 })().finally(()=>pending.delete(key));pending.set(key,task);return task;
}
async function stop(root,id){const key=path.resolve(root)+'\0'+id;await pending.get(key);const handle=active.get(key);if(handle){await closeHandle(handle);if(active.get(key)===handle)active.delete(key);}return {ok:true};}
async function close(root){
 root=path.resolve(root);await Promise.allSettled([...pending].filter(([key])=>key.startsWith(root+'\0')).map(([,value])=>value));
 const errors=[];
 for(const [key,handle]of active)if(key.startsWith(root+'\0')){
  try{await closeHandle(handle);if(active.get(key)===handle)active.delete(key);}catch(error){errors.push(error);}
 }
 if(errors.length)throw Object.assign(new AggregateError(errors,'Some module settings could not confirm shutdown: '+errors.map(error=>error.message).join('; ')),{resourcesRetained:errors.some(error=>error.resourcesRetained)});
}
module.exports={open,close,stop,isOpen:(root,id)=>active.has(path.resolve(root)+'\0'+id)};
