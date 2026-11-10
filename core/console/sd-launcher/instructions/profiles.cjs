'use strict';
// PC profiles are consumed at package preparation only. Their compiled bytes
// live in launcher.izapp, so neither this file nor the PC is needed at runtime.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const bundled=[require('./izdoom-prime18.json'),require('./ims-gpu-pcm-cityfast.json')],root=path.resolve(__dirname,'../../../..'),directory=path.join(root,'data/local-instructions');
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
