'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const check=require('../core/console/sd-launcher/check.cjs');
const source=path.resolve(__dirname,'../core/console'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function fixture(t){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'iz2s-runtime-integrity-'));
 t.after(()=>fs.rmSync(root,{recursive:true,force:true}));
 const copy=name=>{const target=path.join(root,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(source,name),target);};
 for(const [folder,file,binary]of [
  ['sd-launcher/native','build/manifest.json','build/launcher.bin'],
  ['recovery','native/build/manifest.json','native/build/recovery.bin'],
  ['recovery/sd-app','manifest.json','launcher.izapp'],
  ['sd-launcher/reader','native.json','runtime.bin'],
  ['sd-launcher/instructions','native.json','runtime.bin'],
 ]){
  copy(folder+'/'+file);copy(folder+'/'+binary);
  const manifest=JSON.parse(fs.readFileSync(path.join(source,folder,file)));
  for(const name of Object.keys(manifest.sources||{}))copy(path.posix.normalize(folder+'/'+name));
 }
 for(const name of ['recovery/reconnect-app.c','recovery/sd-app/native/native.bin','sd-launcher/instructions/runtime.c','sd-launcher/instructions/checkpoint.h'])copy(name);
 return {root,directory:path.join(root,'sd-launcher')};
}
function update(file,change){const m=JSON.parse(fs.readFileSync(file));change(m);fs.writeFileSync(file,JSON.stringify(m));}
test('the shipped supervisor and all required native companions agree',()=>{
 assert(check.verifyRuntime().sha256);
});
test('a rebuilt supervisor cannot hide stale recovery and reader ABI fingerprints',t=>{
 const {root,directory}=fixture(t),header=path.join(directory,'native/native-app.h');
 fs.appendFileSync(header,'\n/* Updated native interface. */\n');const hash=sha(fs.readFileSync(header));
 update(path.join(directory,'native/build/manifest.json'),m=>{m.sources['native-app.h']=hash;});
 assert(check.verify(directory).sha256);
 assert.throws(()=>check.verifyRuntime(directory),e=>e.code==='IZ2S_SD_BUILD_STALE'&&e.component==='Restore connection'&&/no console commands/.test(e.message));
 update(path.join(root,'recovery/sd-app/manifest.json'),m=>{m.nativeAbiSha256=hash;});
 assert.throws(()=>check.verifyRuntime(directory),e=>e.code==='IZ2S_SD_BUILD_STALE'&&e.component==='Book Reader');
});
test('a damaged recovery package is rejected before console startup',t=>{
 const {root,directory}=fixture(t);check.verifyRuntime(directory);
 const file=path.join(root,'recovery/sd-app/launcher.izapp'),bytes=fs.readFileSync(file);bytes[bytes.length-1]^=1;fs.writeFileSync(file,bytes);
 assert.throws(()=>check.verifyRuntime(directory),e=>e.code==='IZ2S_SD_BUILD_STALE'&&e.component==='Restore connection');
});
