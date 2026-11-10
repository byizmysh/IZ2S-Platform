'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const root=path.resolve(__dirname,'../..');
async function main([command,...args]){
 if(command==='build'){
  const [appDirectory,outputDirectory]=args;if(!appDirectory||!outputDirectory)throw Error('build APP_DIRECTORY OUTPUT_DIRECTORY');const result=await require('../../core/console/sd-launcher/package.cjs').build(path.resolve(appDirectory),path.resolve(outputDirectory));console.log(JSON.stringify({id:result.id,output:result.output,bundle:result.bundle,assets:result.assets.map(a=>({path:a.path,bytes:a.bytes,sha256:a.sha256})),bytes:result.bytes.length,sha256:result.sha256}));return;
 }
 if(command==='inspect'){const bytes=await fs.readFile(args[0]),pkg=require('../../core/console/sd-launcher/package.cjs').decode(bytes);console.log(JSON.stringify({...pkg,sections:Object.fromEntries(Object.entries(pkg.sections).map(([n,b])=>[n,b.length]))},null,2));return;}
 if(command==='install'){
  const [session,file]=args;if(!/^launcher-[A-Za-z0-9]+$/.test(session||''))throw Error('install SESSION FILE.izapp');const directory=path.join(root,'data/sessions',session),bytes=await fs.readFile(file),assets=await require('../../core/console/sd-launcher/assets.cjs').loadBundle(file,bytes);
  const a=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));let storage,retained=false;
  try{storage=await require('../../core/console/sd-launcher/storage.cjs').create(a);console.log(JSON.stringify(await require('../../core/console/sd-launcher/install.cjs').installBytes(storage,bytes,{assets})));await fs.writeFile(path.join(directory,'sd-launcher-rescan.request'),'1\n');}catch(e){retained=!!e.resourcesRetained;throw e;}finally{if(storage&&!storage.uncertain&&!retained)await a.close();}return;
 }
 throw Error('Use build, inspect or install');
}
if(require.main===module)main(process.argv.slice(2)).catch(e=>{console.error(e.message);process.exitCode=1;});module.exports={main};
