'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const root=path.resolve(__dirname,'../../..');
async function main({host,shell='izlauncher',directory,moduleIds=[]}){
  if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Choose IZlauncher or IZPSlauncher');
  directory=path.resolve(directory);await fs.mkdir(directory,{recursive:true,mode:0o700});
  process.env.IZ11_STATE_DIR=directory;process.env.IZ2S_SHELL=shell;process.env.IZ15_SHELL=shell;
  process.env.IZ15_LOADER='civswitch';
  process.env.HB_APPS_DIR=path.join(directory,'apps');await fs.mkdir(process.env.HB_APPS_DIR,{recursive:true});
  process.env.IZ2S_MODULE_PINS=JSON.stringify(await require('../../../core/modules.cjs').capture(root));
  await require('../../../core/session/preload.cjs').prepare(root,moduleIds,process.env.HB_APPS_DIR,{shell});
  let stopped=false;const stop=()=>{stopped=true;};process.once('SIGTERM',stop);process.once('SIGINT',stop);
  const started=Date.now(),timings={};
  const stage=async(name,action)=>{console.log('CIV7_LAUNCHER_STAGE '+name);if(stopped)throw Error('Civ7 launcher stopped');const at=Date.now();await action();timings[name]=Date.now()-at;if(stopped)throw Error('Civ7 launcher stopped');};
  const diagnostics=path.join(root,'data/diagnostics/civswitch-runtime');
  await stage('driver',()=>require('./qualify.cjs').qualify({host,directory:diagnostics}));
  await stage('connection',()=>require('./prepare-session.cjs').prepare({host,directory,diagnostics}));
  await stage('display',()=>require('./prepare-session.cjs').main(host,'display',directory,shell));
  const display=JSON.parse(await fs.readFile(path.join(directory,'vulkan-display.json')));
  if(!display.verified||!display.presentationAccepted||!display.scratchRestored||display.error)throw Error('Civ7 display preparation failed');
  await stage('menu',()=>require('./prepare-session.cjs').main(host,'menu',directory,shell));
  await fs.writeFile(path.join(directory,'civswitch-startup.json'),JSON.stringify({shell,timings,prepareMs:Date.now()-started},null,2),{mode:0o600});
  try{await require('./frame-loop.cjs').main(directory,{shell});}finally{process.removeListener('SIGTERM',stop);process.removeListener('SIGINT',stop);}
}
if(require.main===module)main({host:process.argv[2],shell:process.argv[3],directory:process.argv[4],moduleIds:JSON.parse(process.argv[5]||'[]')}).catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={main};
