// OunceCake stays an unmodified, separately licensed upstream component.
const fs=require('node:fs/promises'),path=require('node:path'),net=require('node:net');
const REQUIRED_CAPABILITY='held-filesystem-rpc';
function availability(capabilities=[]){
 const available=Array.isArray(capabilities)&&capabilities.includes(REQUIRED_CAPABILITY);
 return {available,requiredCapability:REQUIRED_CAPABILITY,...(!available?{reason:'Game filesystem access requires a live FS endpoint. Enable FS endpoint before Launch, or use File Exchange /save, /tmp and /rom.'}:{})};
}
async function preflight(run,{capabilities}={}){
 if(capabilities!==undefined){const result=availability(capabilities);if(!result.available)return {...result,ready:false,run};}
 run=path.resolve(run);const engine=path.join(run,'switch2');let current,proof;
 try{current=JSON.parse(await fs.readFile(path.join(engine,'current-acquisition.json')));proof=JSON.parse(await fs.readFile(path.join(engine,'ro/root/current-root.json')));await fs.access(path.join(engine,'app/session.cjs'));}
 catch(e){if(e.code!=='ENOENT')throw e;return {...availability(),ready:false,run};}
 if(!path.isAbsolute(current.socketPath||'')||!Number.isInteger(proof.rootHandle)||proof.rootHandle<=0)return {available:false,requiredCapability:REQUIRED_CAPABILITY,ready:false,run,reason:'Held FS acquisition or root handle is invalid'};
 const info=await new Promise((resolve,reject)=>{
  const socket=net.createConnection(current.socketPath);let input='';socket.setEncoding('utf8');socket.setTimeout(5000,()=>socket.destroy(Error('Held FS endpoint did not answer')));
  socket.once('error',reject);socket.once('connect',()=>socket.write(JSON.stringify({command:'root-native-info'})+'\n'));
  socket.on('data',b=>{input+=b;if(input.length>1024*1024)socket.destroy(Error('FS reply too large'));});
  socket.once('end',()=>{try{const result=JSON.parse(input);if(result.error)throw Error(result.error);resolve(result);}catch(e){reject(e);}});
 }).catch(e=>({error:e.message}));
 if(info.error)return {available:false,requiredCapability:REQUIRED_CAPABILITY,ready:false,run,reason:info.error};
 return {available:true,requiredCapability:REQUIRED_CAPABILITY,ready:true,run,rootHandle:proof.rootHandle};
}
async function main(run,title){
 const status=await preflight(run);if(!status.ready)throw Error(status.reason);
 const args=[status.run];if(title)args.push(title);args.push('--port','2122');
 await require('./ouncecake/ouncecake.cjs').main(args);
}
async function list(run){
 const status=await preflight(run);if(!status.ready)throw Error(status.reason);
 const attachment=await require('./ouncecake/engine/session.cjs').attachGame(status.run);
 try{return await require('./ouncecake/engine/content.cjs').discoverGames({...attachment,shouldStop:()=>false});}finally{await attachment.close();}
}
module.exports={availability,preflight,main,list,REQUIRED_CAPABILITY};
