'use strict';
// Real local library and shell raster, using one saved pin snapshot in both
// runs. Writes diagnostics only; never opens a console RPC connection.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{performance}=require('node:perf_hooks');
async function main(label='before'){
 const root=path.resolve(__dirname,'../..'),out=path.join(root,'data/build-cache/startup-profile');process.env.IZ2S_MODULE_PINS=await fs.readFile(path.join(out,'module-pins.json'),'utf8');
 const repo=await require('../../core/packages/repository.cjs').create(root,{shell:'izpslauncher'});await repo.list();const destination=path.join(out,'preview-'+label);await fs.mkdir(destination,{recursive:true});const start=performance.now(),result=await repo.previewApps(destination),elapsedMs=performance.now()-start;
 const rasterStart=performance.now(),tiles=await require('../../shells/izpslauncher/server/raster.cjs').raster(result,out,'preview-tiles-'+label),rasterMs=performance.now()-rasterStart;
 let copiedBytes=0;async function walk(p){for(const e of await fs.readdir(p,{withFileTypes:true})){const f=path.join(p,e.name);if(e.isDirectory())await walk(f);else if(e.isFile())copiedBytes+=(await fs.stat(f)).size;}}await walk(destination);
 const report={label,localOnly:true,consoleCommands:0,elapsedMs,rasterMs,copiedBytes,tileSha256:crypto.createHash('sha256').update(tiles).digest('hex'),apps:result.apps.map(a=>({id:a.id,fingerprint:a.fingerprint})),rejected:result.rejected};await fs.writeFile(path.join(out,'previews-'+label+'.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({label,elapsedMs,rasterMs,copiedBytes,tileSha256:report.tileSha256,apps:report.apps.length,rejected:report.rejected.length}));
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
