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
 const archive=require(path.join(platform,'core/archive.cjs'));
 const bytes=await archive.pack(home),checked=archive.inspect(bytes);assert.equal(checked.descriptor.version,meta.version);
 for(const {name} of checked.files){
  const parts=name.toLowerCase().split('/'),base=parts.at(-1);
  assert(!parts.some(p=>['bios','games','user-bios','user-games'].includes(p))&&!/^(?:scph.*\.bin|bios\.bin|ps[1x]_rom\.bin)$/.test(base)&&! /\.(?:iso|cue|chd|pbp|ccd|sbi|sub)$/.test(base),'BIOS/game data must not be distributed: '+name);
 }
 const moduleFile=path.join(output,'izpsx-emulator-'+meta.version+'.iza');await fs.writeFile(moduleFile,bytes);
 const frozen=path.join(ROOT,'assets/sd-release'),receipt=JSON.parse(await fs.readFile(path.join(frozen,'release.json')));
 assert.equal(receipt.version,meta.version);
 await fs.rm(path.join(output,'sd'),{recursive:true,force:true});await fs.mkdir(path.join(output,'sd'));
 for(const [name,digest]of Object.entries(receipt.files)){
  assert(!name.includes('/')&&!name.includes('\\'));
  const b=await fs.readFile(path.join(frozen,name));assert.equal(crypto.createHash('sha256').update(b).digest('hex'),digest);
  await fs.writeFile(path.join(output,'sd',name),b);
 }
 const app={manifest:JSON.parse(await fs.readFile(path.join(home,'switch/app.json')))};
 const bundle=JSON.parse(await fs.readFile(path.join(frozen,'sd-bundle.json')));
 assert.equal(bundle.id,pkg.id);assert.equal(bundle.launcher.sha256,receipt.files['launcher.iza']);
 for(const asset of bundle.assets)assert.equal(receipt.files[asset.file],asset.sha256);
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const report={version:meta.version,module:path.basename(moduleFile),moduleSha256:sha(bytes),sdPackageSha256:bundle.launcher.sha256,nativeAbi:7,nativeEngineSha256:app.manifest.native.assets[0].sha256,nativeRebuilt:fromBuild,consoleExecuted:false,installed:false,published:false};
 await fs.writeFile(path.join(output,'package-report.json'),JSON.stringify(report,null,2)+'\n');return report;
}
if(require.main===module)build({fromBuild:process.argv.includes('--from-build')}).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.stack);process.exitCode=1;});
module.exports={build};
