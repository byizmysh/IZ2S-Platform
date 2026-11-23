'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const ROOT=path.resolve(__dirname,'..');
async function build({fromBuild=false}={}){
 const platform=path.resolve(process.env.IZPSX_PANEL||process.env.IZ2S_ROOT||path.join(ROOT,'../IZ2S-Platform'));
 const descriptor=JSON.parse(await fs.readFile(path.join(platform,'package.json')));
 assert.equal(descriptor.name,'iz2s-platform','Set IZPSX_PANEL to the compatible platform source root');
 const output=path.join(ROOT,'dist');await fs.mkdir(output,{recursive:true});
 const home=path.join(ROOT,'module');
 const source=fromBuild?path.join(ROOT,'build/izpsx-hen/module'):home;
 const meta=require('../package.json');
 const archive=require(path.join(platform,'core/archive.cjs'));
 const checked=archive.inspect(await archive.pack(source));
 assert.equal(checked.descriptor.id,'izpsx');
 assert.equal(checked.descriptor.version,meta.version);
 for(const {name} of checked.files){
  const parts=name.toLowerCase().split('/'),base=parts.at(-1);
  assert(!parts.some(p=>['bios','games','user-bios','user-games'].includes(p))&&!/^(?:scph.*\.bin|bios\.bin|ps[1x]_rom\.bin)$/.test(base)&&! /\.(?:iso|cue|chd|pbp|ccd|sbi|sub)$/.test(base),'BIOS/game data must not be distributed: '+name);
 }
 const frozen=path.join(ROOT,'assets/sd-release'),receipt=JSON.parse(await fs.readFile(path.join(frozen,'release.json')));
 assert.equal(receipt.version,meta.version);
 const files=new Map();
 for(const [name,digest]of Object.entries(receipt.files)){
  assert(!name.includes('/')&&!name.includes('\\'));
  const b=await fs.readFile(path.join(frozen,name));assert.equal(crypto.createHash('sha256').update(b).digest('hex'),digest);
  files.set(name,b);
 }
 const manifest=JSON.parse(await fs.readFile(path.join(source,checked.descriptor.switch.entry)));
 assert.equal(manifest.id,checked.descriptor.id);
 assert.equal(manifest.version,meta.version);
 const bundle=JSON.parse(files.get('sd-bundle.json'));
 assert.equal(bundle.id,checked.descriptor.id);
 assert.equal(bundle.launcher.sha256,receipt.files['launcher.iza']);
 assert.equal(bundle.launcher.bytes,files.get('launcher.iza').length);
 assert.deepEqual([...files.keys()].sort(),['launcher.iza','sd-bundle.json',...bundle.assets.map(a=>a.file)].sort());
 assert.deepEqual(bundle.assets.map(a=>[a.path,a.bytes,a.sha256]),
  manifest.native.assets.map(a=>[a.path,a.bytes,a.sha256]));
 for(const asset of bundle.assets){
  assert.equal(asset.file,asset.path);
  assert.equal(receipt.files[asset.file],asset.sha256);
  assert.equal(files.get(asset.file).length,asset.bytes);
 }
 if(fromBuild){
  await fs.rm(home,{recursive:true,force:true});
  await fs.cp(source,home,{recursive:true});
 }
 const bytes=archive.encode([...files].map(([name,bytes])=>({name,bytes})));
 const moduleFile=path.join(output,'izpsx-emulator-'+meta.version+'.iza');
 await fs.writeFile(moduleFile,bytes);
 const sd=path.join(output,'sd');
 await fs.rm(sd,{recursive:true,force:true});
 await fs.mkdir(sd);
 for(const [name,contents] of files)await fs.writeFile(path.join(sd,name),contents);
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const report={version:meta.version,module:path.basename(moduleFile),moduleSha256:sha(bytes),sdPackageSha256:bundle.launcher.sha256,nativeAbi:manifest.native.abi,nativeEngineSha256:manifest.native.assets[0].sha256,nativeRebuilt:false,consoleExecuted:false,installed:false,published:false};
 await fs.writeFile(path.join(output,'package-report.json'),JSON.stringify(report,null,2)+'\n');return report;
}
if(require.main===module)build({fromBuild:process.argv.includes('--from-build')}).then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.stack);process.exitCode=1;});
module.exports={build};
