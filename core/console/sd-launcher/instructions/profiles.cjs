'use strict';
// PC profiles are consumed at package preparation only. Their compiled bytes
// live in launcher.iza, so neither this file nor the PC is needed at runtime.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const doom=require('./izdoom-prime18.json');
// Audio Test ships the same game and PCM image with a different PC audio
// provider. Its SD mode uses the existing finite AudioOut plan.
const doomAudioTest={
 ...doom,
 id:'izdoom-audio-test',
 version:'0.18.4-audio-test',
 files:{...doom.files,'app.json':'61f2b0cbf9d6f2680c63c6c3fbce0bf34a848606031423f6fad139ef87dbc76a'},
};
// These shaders publish READY after clearing the ring. The 0.18.2 shaders
// publish READY only after writing the first 8192 PCM frames.
const doomWrittenFrames=784+23*4;
const doomBeforePriming={
 ...doom.plan,
 rules:doom.plan.rules.map(rule=>({
  ...rule,
  when:rule.when.filter(condition=>condition.space!=='controls'||condition.offset!==doomWrittenFrames),
 })),
};
const doomNative={
 ...doom,
 id:'izdoom-native18',
 version:'0.18.0-experimental',
 files:{
  ...doom.files,
  'app.json':'41a5c38c395d63141a6f598d252b28781a8e5a13fbd6d9d5188d850b958fab1c',
  'memory.bin.deflate':'2e081397ecc7fda2b357d9da978edb4360ed04b99f81028be6d4ba5dad82c903',
  'app.frag.spv':'7a87e1961a51dc67bfcc46374fad9a0cb07bee361a70b2e27f77f6349bd60ef2',
  'after.comp.spv':'7f8683d8e967660405e336b65b5048e7cdf76a9104420dfeb70dae7371158ad2',
 },
 plan:doomBeforePriming,
};
const doomWide={
 ...doom,
 id:'izdoom-wide18',
 version:'0.18.1-experimental',
 files:{
  ...doom.files,
  'app.json':'93bfcb3d9ac2107abdd4d05305893a60c4785ee959940ac0e37f8ab5e924e888',
  'after.comp.spv':'18ad5fff1d20a5a74dea1935f5ffb6e07b1c25215e8bf330aae15d18048e9562',
 },
 plan:doomBeforePriming,
};
const sonicTransfer=require('./izsonic-transfer18.json');
const sonicResidentReleases=require('./izsonic-resident-releases.json');
// These releases use the same PCM offset, READY/ARM/START handshake and
// clock fields as Lossless Load. Their resident allocation is 76 MiB.
const sonicResidentPlan={
 ...sonicTransfer.plan,
 guards:sonicTransfer.plan.guards.map(guard=>
  guard.space==='controls'&&guard.offset===792?{...guard,value:76*1024*1024}:guard),
};
const sonicResident=Object.entries(sonicResidentReleases).map(([id,{version,files}])=>({
 format:sonicTransfer.format,id,version,files,plan:sonicResidentPlan,
}));
const cityFast=require('./ims-gpu-pcm-cityfast.json');
// Resident Music and Dock Diagnostics share Fast Start's PCM layout and
// playback handshake. Pin the diagnostic shaders separately.
const cityResident={
 ...cityFast,
 id:'ims-gpu-pcm-city',
 version:'0.1.0',
 files:{...cityFast.files,'app.json':'7c9bd82e6799df1d9751e06dfbd9f41de6df906c95408e782aa78c75f4278134'},
};
const cityDiagnostics={
 ...cityFast,
 id:'ims-gpu-pcm-citydiag',
 version:'0.1.2',
 files:{
  ...cityFast.files,
  'app.json':'30414bc5eed9d8620c8402b07d4959dd82787fea595923545ba761be6180bf50',
  'app.frag.spv':'92a0d2b660102bb5c675b46d9bb16f20ea69c1b6a5f3b35a9f4a778938776b7a',
  'app.after.spv':'c22aeba29ce3c83c08d631e5a57080b01e10777896e838cd7a5cf68ed3e723d3',
 },
};
const bundled=[doom,doomAudioTest,doomNative,doomWide,cityFast,cityResident,cityDiagnostics,
 sonicTransfer,...sonicResident],root=path.resolve(__dirname,'../../../..'),directory=path.join(root,'data/local-instructions');
function required(m){return [...new Set(['app.json',m.initialState,m.data,m.memory?.initial,...Object.values(m.shaders||{}),m.computeSequence?.begin,m.computeSequence?.after,...(m.localRuntime?.assets||[]).map(a=>a.file)].filter(Boolean))];}
function validate(p){
 assert(p?.format==='iz2s.local-instructions-profile'&&/^[a-z0-9][a-z0-9-]{0,46}$/.test(p.id)&&typeof p.version==='string'&&p.version.length<=32,'Invalid local instruction profile identity');
 assert(p.files&&typeof p.files==='object'&&!Array.isArray(p.files)&&Object.keys(p.files).length<=128,'Invalid local profile file identities');
 for(const [name,sha]of Object.entries(p.files)){require('../../../packages/capsule.cjs').safeName(name);assert(/^[a-f0-9]{64}$/.test(sha),'Invalid local profile checksum');}
 require('./compile.cjs').compile(p.plan);return p;
}
function select(m){
 if(!/^[a-z0-9][a-z0-9-]{0,46}$/.test(m.id||''))return null;
 const file=path.join(directory,m.id+'.json');let bytes;try{const descriptor=fs.openSync(file,'r');try{const stat=fs.fstatSync(descriptor);assert(stat.isFile()&&stat.size<=65536,'Local instruction profile is too large');bytes=fs.readFileSync(descriptor);}finally{fs.closeSync(descriptor);}}catch(e){if(e.code!=='ENOENT')throw e;}
 const p=bytes?validate(JSON.parse(bytes)):bundled.find(p=>p.id===m.id&&p.version===m.version);if(!p)return null;assert.equal(p.id,m.id,'Local profile belongs to another module');assert.equal(p.version,m.version,'Local instructions were prepared for a different app version');
 for(const name of required(m))assert(Object.hasOwn(p.files,name),'Local profile must pin the actual application file: '+name);return p;
}
async function pin(app,plan){
 assert(!app.manifest.native,'This app already has its own autonomous native entry');
 const rows={};for(const name of new Set([...required(app.manifest),...(plan.assets||[]).map(a=>a.file)])){const file=await require('../../../services/app-catalog.cjs').asset(app.root,name,128*1024*1024);rows[name]=crypto.createHash('sha256').update(file.bytes).digest('hex');}
 const profile=validate({format:'iz2s.local-instructions-profile',id:app.id,version:app.version,files:rows,plan});require('./compile.cjs').compile(plan,{privateBytes:app.manifest.memory?.bytes||0});
 const file=path.join(directory,app.id+'.json');await require('../../../session/ledger.cjs').saveJson(file,profile);return {file,profile};
}
module.exports={directory,required,select,pin,validate};
