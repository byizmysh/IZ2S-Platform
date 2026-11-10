'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const ROOT=path.resolve(__dirname,'..');
async function build({fromBuild=false}={}){
 const platform=path.resolve(process.env.IZPSX_PANEL||process.env.IZ2S_ROOT||path.join(ROOT,'../IZ2S-Platform'));
 const descriptor=JSON.parse(await fs.readFile(path.join(platform,'package.json')));
 assert.equal(descriptor.name,'iz2s-platform','Set IZPSX_PANEL to the compatible platform source root');
 const output=path.join(ROOT,'dist');await fs.mkdir(output,{recursive:true});
 const source=fromBuild?path.join(ROOT,'build/izpsx-sd/module'):path.join(ROOT,'module');
 if(fromBuild){await fs.rm(path.join(ROOT,'module'),{recursive:true,force:true});await fs.cp(source,path.join(ROOT,'module'),{recursive:true});}
 const home=path.join(ROOT,'module'),pkg=JSON.parse(await fs.readFile(path.join(home,'module.json'))),meta=require('../package.json');
 assert.equal(pkg.id,'izpsx');assert.equal(pkg.version,meta.version);
 const archive=require(path.join(platform,'core/archive.cjs')),catalog=require(path.join(platform,'core/services/app-catalog.cjs')),sd=require(path.join(platform,'core/console/sd-launcher/package.cjs'));
 const bytes=await archive.pack(home);assert.equal(archive.inspect(bytes).descriptor.version,meta.version);
 const moduleFile=path.join(output,'izpsx-emulator-'+meta.version+'.izm');await fs.writeFile(moduleFile,bytes);
 const app=await catalog.readApp(path.join(home,'switch'));assert.equal(app.manifest.native.abi,5);assert(sd.eligibility(app).compatible);
 const bundle=await sd.build(app.root,path.join(output,'sd'));assert.equal(sd.decode(bundle.bytes).id,'izpsx');
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const report={version:meta.version,module:path.basename(moduleFile),moduleSha256:sha(bytes),sdPackageSha256:bundle.sha256,nativeAbi:5,nativeEngineSha256:app.manifest.native.assets[0].sha256,nativeRebuilt:fromBuild,consoleExecuted:false,installed:false,published:false};
 await fs.writeFile(path.join(output,'package-report.json'),JSON.stringify(report,null,2)+'\n');return report;
}
if(require.main===module)build({fromBuild:process.argv.includes('--from-build')}).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.stack);process.exitCode=1;});
module.exports={build};
