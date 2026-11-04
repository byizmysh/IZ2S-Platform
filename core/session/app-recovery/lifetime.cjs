// Close wins over an in-flight start; ownership never escapes a cancelled start.
function create({open=require('./server.cjs').create}={}) {
 let opening=null,closing=null,closed=false;
 return {
  start(options){
   if(closed)return Promise.resolve(null);
   if(!opening)opening=(async()=>{
    const service=await open(options);
    if(closed){await service.close();return null;}
    return service;
   })();
   return opening;
  },
  close(){
   if(closing)return closing;
   closed=true;
   closing=(async()=>{
    const service=await opening?.catch(()=>null);
    await service?.close();
   })();
   return closing;
  },
 };
}
module.exports={create};
