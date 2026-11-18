// Own every pending renderer task before releasing or replacing its transport.
function ownTransport(stream,{heartbeat}={}){
 const preparations=new Set(),uploads=new Set();let closing=false,closePromise;
 for(const name of ['prepare','prepareFrame','push'])if(typeof stream[name]==='function'){
  const original=stream[name].bind(stream),pending=name==='push'?uploads:preparations;
  stream[name]=(...args)=>{
   if(closing)return Promise.reject(Error('Render transport is closing'));
   const task=Promise.resolve().then(()=>original(...args)).then(async value=>{if(name==='push')await heartbeat?.();return value;});
   pending.add(task);task.then(()=>pending.delete(task),()=>pending.delete(task));return task;
  };
 }
 const close=stream.close.bind(stream);
 stream.close=()=>closePromise??=(async()=>{closing=true;await Promise.allSettled([...preparations]);await close();await Promise.allSettled([...uploads]);})();
 return stream;
}
async function replaceRenderer({pause,stopEncoder,closeTransport,select,createTransport,startEncoder}){
 await pause();await stopEncoder();await closeTransport();await select();await createTransport();await startEncoder();
}
module.exports={ownTransport,replaceRenderer};
