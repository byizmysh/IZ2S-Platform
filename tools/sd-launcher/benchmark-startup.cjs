'use strict';
// Local CPU/filesystem only. Measures the real cold pinned-module provider
// lookup used during controller startup; no console connection is created.
const fs=require('node:fs/promises'),path=require('node:path'),{performance}=require('node:perf_hooks');
const root=path.resolve(__dirname,'../..'),archive=require('../../core/archive.cjs'),registry=require('../../core/modules.cjs');
async function main(label='before'){
 const out=path.join(root,'data/build-cache/startup-profile');await fs.mkdir(out,{recursive:true});
 const inventory=label==='before'?await registry.capture(root):JSON.parse(await fs.readFile(path.join(out,'module-pins.json')));if(label==='before')await fs.writeFile(path.join(out,'module-pins.json'),JSON.stringify(inventory));process.env.IZ2S_MODULE_PINS=JSON.stringify(inventory);
 let zipCalls=0,zipMilliseconds=0,zipInputBytes=0;const encode=archive.encode;
 archive.encode=function(files){zipCalls++;zipInputBytes+=files.reduce((n,f)=>n+f.bytes.length,0);const start=performance.now();try{return encode(files);}finally{zipMilliseconds+=performance.now()-start;}};
 const start=performance.now(),provider=await require('../../core/components.cjs').create(root).provider('controller-preferences',{optional:true}),elapsedMs=performance.now()-start;
 const report={label,at:new Date().toISOString(),localOnly:true,consoleCommands:0,modules:Object.keys(inventory.modules).length,providerFound:!!provider,elapsedMs,zipCalls,zipMilliseconds,zipInputBytes};
 if(label!=='before'){const start=performance.now(),inventory=await registry.list(root);report.fullInventory={elapsedMs:performance.now()-start,modules:inventory.modules.length,errors:inventory.errors,zipCalls,zipInputBytes};}
 await fs.writeFile(path.join(out,'provider-'+label+'.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
