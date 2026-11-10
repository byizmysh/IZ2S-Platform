'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),{compile}=require('./instructions/compile.cjs');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function select(manifest){
 if(manifest.native)return null;
 if(manifest.localRuntime!==undefined)return {plan:manifest.localRuntime};
 return require('./instructions/profiles.cjs').select(manifest);
}
function nativeManifest(manifest,compiled,plan){return {...manifest,native:{format:'iz2s.native-app',abi:2,code:'generated/local-instructions.bin',dataBytes:compiled.dataBytes,...(compiled.capabilities.length?{capabilities:compiled.capabilities}:{}),...(plan.assets?.length?{assets:plan.assets}:{})}};}
function eligibility(manifest,files){
 try{
  const profile=select(manifest);if(!profile)return null;
  if(profile.files)for(const [name,digest]of Object.entries(profile.files)){const file=files.find(f=>f.name===name);assert(file&&sha(file.bytes)===digest,'Local instructions require the exact qualified application build: '+name);}
  const compiled=compile(profile.plan,{privateBytes:manifest.memory?.bytes||0}),m=nativeManifest(manifest,compiled,profile.plan),policy=require('./package.cjs').eligibility(m);assert(policy.compatible,policy.reason);require('./assets.cjs').inspectPacked(m,files);
  return {compatible:true,adapter:'launcher-instructions-v1',experimental:true,instructions:{version:1,rules:compiled.rules,operations:compiled.operations,controllerBytes:compiled.bytes.length,hostRequired:false}};
 }catch(e){return {compatible:false,reason:e.message};}
}
async function prepare(app){
 const profile=select(app.manifest);if(!profile)return null;
 const files=[];if(profile.files)for(const name of Object.keys(profile.files))files.push({name,bytes:(await require('../../services/app-catalog.cjs').asset(app.root,name,80*1024*1024)).bytes});
 // readApp already checked declared immutable assets. Profile pins are checked
 // again against actual source bytes, never just an id or version label.
 if(!profile.files)for(const a of require('./assets.cjs').declarations({native:{assets:profile.plan.assets}}))files.push({name:a.file,bytes:(await require('../../services/app-catalog.cjs').asset(app.root,a.file,128*1024*1024)).bytes});
 const policy=eligibility(app.manifest,files);assert(policy?.compatible,policy?.reason);const compiled=compile(profile.plan),folder=path.join(__dirname,'instructions'),report=JSON.parse(await fs.readFile(path.join(folder,'native.json'))),file=path.join(folder,'runtime.bin'),bytes=await fs.readFile(file);
 assert.equal(report.format,'iz2s.local-instructions-native-v1');assert.equal(report.abi,2);assert.equal(report.bytes,bytes.length);assert.equal(report.sha256,sha(bytes));assert.equal(report.sourceSha256,sha(await fs.readFile(path.join(folder,'runtime.c'))),'Rebuild changed local instructions runtime');assert.equal(report.nativeAbiSha256,sha(await fs.readFile(path.join(__dirname,'native/native-app.h'))),'Rebuild local instructions after ABI changes');
 assert.equal(report.checkpointSha256,sha(await fs.readFile(path.join(folder,'checkpoint.h'))),'Rebuild changed checkpoint controller');
 const manifest=nativeManifest(app.manifest,compiled,profile.plan),sdAssets=await require('./assets.cjs').inspect(app.root,manifest);
 return {...app,manifest,sdAssets,files:{...app.files,nativeCode:{file,bytes},nativeData:{bytes:compiled.bytes}},localInstructions:policy.instructions};
}
module.exports={select,eligibility,prepare,compile};
