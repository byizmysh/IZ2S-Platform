'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
async function build(){
 for(const name of ['iz-exchange','iz-exchange-fs'])await require('../modules/'+name+'/switch/build.cjs').main();
 execFileSync(process.execPath,[path.join(root,'modules/BookReader/tools/build.cjs')],{cwd:root,stdio:'inherit',env:{...process.env,IZ2S_ROOT:root}});
 const result=await require('../sdk/publish-modules.cjs').publish(root);
 if(result.errors.length)throw Error(JSON.stringify(result.errors));
 // Preserve the standalone version of the ARM input protocol across launcher releases.
 await require('../sdk/pack-agent.cjs').pack(root);
 console.log('Built three IZ2S modules and the compatible ARM input package.');
}
if(require.main===module)build().catch(error=>{console.error(error);process.exitCode=1;});
module.exports={build};
