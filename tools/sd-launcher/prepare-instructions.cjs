'use strict';
// Usage: node tools/sd-launcher/prepare-instructions.cjs MODULE_ID PLAN_JSON
// Pins existing app bytes without editing/repacking its engine or module.
const fs=require('node:fs/promises'),path=require('node:path');
async function main(id,planFile){
 if(!/^[a-z0-9][a-z0-9-]{0,46}$/.test(id||'')||!planFile)throw Error('Choose a module id and a local instruction JSON file');
 const root=path.resolve(__dirname,'../..'),module=await require('../../core/modules.cjs').read(root,id);if(!module.switchRoot)throw Error('This module has no console application');
 const app=await require('../../core/services/app-catalog.cjs').readApp(module.switchRoot),handle=await fs.open(planFile,'r');let plan;try{if((await handle.stat()).size>32768)throw Error('Local instructions exceed the size limit');plan=JSON.parse(await handle.readFile('utf8'));}finally{await handle.close();}
 const result=await require('../../core/console/sd-launcher/instructions/profiles.cjs').pin(app,plan);console.log(JSON.stringify({module:id,profile:result.file,engineChanged:false,consoleCommands:0}));return result;
}
module.exports={main};if(require.main===module)main(process.argv[2],process.argv[3]).catch(e=>{console.error(e);process.exitCode=1;});
