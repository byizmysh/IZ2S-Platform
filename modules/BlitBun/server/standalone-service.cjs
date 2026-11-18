// Independently restartable capture service attached to an existing menu.
const fs=require('node:fs/promises'),path=require('node:path');
async function main(directory,port=39392){
 directory=path.resolve(directory);const rpc=require(path.join(directory,'shared/rpc.cjs')),p=JSON.parse(await fs.readFile(path.join(directory,'launcher-auto.json')));
 if(!p.ready||p.session!==rpc.a.session)throw Error('Menu session differs');
 const runtime=JSON.parse(await fs.readFile(path.join(directory,'app-runtime.json')));
 if(runtime.active||!runtime.catalog.apps.every(a=>['doom','pop-calc','blitbun'].includes(a.id)))throw Error('Use the built-in-only menu for an independently attached capture service');
 const server=await require('./application-host.cjs').create({rpc,mapped:p.controls.mapped,directory,port:Number(port),transport:'tcp'});
 const shell=JSON.parse(await fs.readFile(path.join(directory,'run11-runtime.json'))).shell;server.setVideoStorage(shell?.privateStorage);
 let stop=false;process.on('SIGINT',()=>stop=true);process.on('SIGTERM',()=>stop=true);
 try{while(!stop){await new Promise(r=>setTimeout(r,250));

  if(await fs.access(path.join(directory,'result.json')).then(()=>true,()=>false))break;}}
 finally{await server.close();}
}
module.exports={main};
if(require.main===module)main(...process.argv.slice(2)).catch(e=>{console.error(e.message);process.exitCode=1;});
