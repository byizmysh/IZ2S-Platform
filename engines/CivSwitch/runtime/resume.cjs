'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
async function main(directory,shell='izlauncher'){
 directory=path.resolve(directory);const root=path.resolve(__dirname,'../../..');
 const loop=JSON.parse(await fs.readFile(path.join(directory,'civswitch-loop.json')));
 if(!loop.completed||loop.error)throw Error('Stop and verify the previous Civ7 frame loop before resuming');
 process.env.IZ11_STATE_DIR=directory;process.env.IZ2S_SHELL=shell;process.env.IZ15_SHELL=shell;process.env.HB_APPS_DIR=path.join(directory,'apps');
 process.env.IZ2S_MODULE_PINS=JSON.stringify(await require('../../../core/modules.cjs').capture(root));
 await fs.rm(path.join(directory,'civswitch-loop.stop'),{force:true});
 await require('./frame-loop.cjs').main(directory,{shell});
}
if(require.main===module)main(process.argv[2],process.argv[3]).catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={main};
