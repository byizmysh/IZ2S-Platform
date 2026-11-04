const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {execFile}=require('node:child_process');
const {promisify}=require('node:util');
const {inflateSync}=require('node:zlib');
const run=promisify(execFile);
const ROOT=path.resolve(__dirname,'../../data/app-links'),MAX_APPS=32,SLOT_BYTES=131072,ATLAS_BYTES=1024*2048;
const check=(v,m)=>{if(!v)throw Error(m);};
async function asset(root,name,max){
 check(typeof name==='string'&&name.length>0&&!path.isAbsolute(name)&&!name.split(/[\\/]/).includes('..'),'Asset path must stay inside its app');
 const file=await fs.realpath(path.resolve(root,name));check(file.startsWith(root+path.sep),'Asset escapes its app folder');
 const stat=await fs.stat(file);check(stat.isFile()&&stat.size>0&&stat.size<=max,'Invalid asset size: '+name);
 return {file,bytes:await fs.readFile(file)};
}
async function readApp(directory,{validate=true}={}){
 const root=await fs.realpath(directory),manifestAsset=await asset(root,'app.json',16384),m=JSON.parse(manifestAsset.bytes);
 check(m.format==='hblauncher.gpu-app'&&m.abi===1,'Unsupported app format/ABI');
 check(typeof m.id==='string'&&/^[a-z0-9][a-z0-9-]{0,47}$/.test(m.id),'Invalid app id');
 check(typeof m.name==='string'&&m.name.trim().length>0&&m.name.length<=64,'Invalid app name');
 check(typeof m.version==='string'&&m.version.length<=32,'Invalid version');
 check(typeof m.description==='string'&&m.description.length<=160,'Invalid description');
 check(m.transition===undefined||m.transition==='none'||m.transition==='fade','Invalid transition');
 check(m.shaderEncoding===undefined||m.shaderEncoding==='deflate','Unsupported shader encoding');
 check(m.service===undefined,'Unsupported host service');
 check(m.hostRuntime===undefined||/^module:[a-z0-9-]+\/[A-Za-z0-9-]+$/.test(m.hostRuntime),'Invalid module host runtime');
 check(m.shellContribution?.launchCommand===undefined||(m.builtin&&m.shellContribution.scene===3&&Number.isInteger(m.shellContribution.launchCommand)&&m.shellContribution.launchCommand>=1&&m.shellContribution.launchCommand<=255),'Invalid stream launch command');
 check(m.builtin===undefined||(/^[a-z0-9][a-z0-9-]{0,47}$/.test(m.builtin)&&Number.isInteger(m.shellContribution?.scene)&&m.shellContribution.scene>=2&&m.shellContribution.scene<=9),'Invalid shell contribution scene');
 // Separate package identities may share the same validated built-in behavior.
 if(m.builtin)check(!m.displayOnly&&!m.shaders&&!m.memory&&!m.computeSequence,'Invalid built-in manifest');
 require('../console/host/runtime.cjs').validateManifest(m.sdk);
 const files={},hash=crypto.createHash('sha256');hash.update(manifestAsset.bytes);
 // Optional main-compute grid; absence preserves the ABI 1 single-workgroup default.
 if(m.computeWorkgroups!==undefined){const grid=m.computeWorkgroups;check(Array.isArray(grid)&&grid.length===3&&grid.every(v=>Number.isInteger(v)&&v>=1&&v<=65535)&&grid.reduce((n,v)=>n*v,1)<=4096,'Invalid compute workgroups');}
 if(m.computeSequence!==undefined){check(m.computeSequence&&Number.isInteger(m.computeSequence.repeat)&&m.computeSequence.repeat>=1&&m.computeSequence.repeat<=512,'Invalid compute sequence repeat');check(typeof m.computeSequence.begin==='string'&&typeof m.computeSequence.after==='string','Invalid compute sequence shaders');}
 const stages=m.displayOnly===true||m.builtin?[]:[['vertex',0,m.shaders?.vertex],['fragment',4,m.shaders?.fragment],['compute',5,m.shaders?.compute]];
 if(m.computeSequence)stages.push(['computeBegin',5,m.computeSequence.begin],['computeAfter',5,m.computeSequence.after]);
 for(const [stage,model,name] of stages){
  const item=await asset(root,name,1048576);
  if(m.shaderEncoding==='deflate'){
   try{item.bytes=inflateSync(item.bytes,{maxOutputLength:1048576});}
   catch{throw Error('Invalid compressed SPIR-V '+stage);}
  }
  const b=item.bytes;
  check(b.length>=20&&b.length%4===0&&b.readUInt32LE()===0x07230203,'Invalid SPIR-V '+stage);
  let entry=false;for(let i=5;i<b.length/4;){const word=b.readUInt32LE(i*4),count=word>>>16,op=word&65535;check(count>0&&i+count<=b.length/4,'Malformed SPIR-V instruction');if(op===15&&count>=4&&b.readUInt32LE((i+1)*4)===model){const name=b.subarray((i+3)*4,(i+count)*4).toString('utf8').split('\0')[0];if(name==='main')entry=true;}i+=count;}
  check(entry,'Missing '+stage+' main entry point');
  if(validate){
   const temp=await fs.mkdtemp(path.join(require('node:os').tmpdir(),'hb-shader-'));
   try{const file=path.join(temp,'module.spv');await fs.writeFile(file,b);
    await run(require('../../sdk/tools/toolchain.cjs').tool('SPIRV_VAL','spirv-val'),['--target-env','vulkan1.0',file],{timeout:10000,maxBuffer:65536});
   }finally{await fs.rm(temp,{recursive:true,force:true});}
  }
  files[stage]=item;hash.update(stage).update(b);
 }
 if(m.displayOnly!==true&&!m.builtin){
 files.state=await asset(root,m.initialState,1024);check(files.state.bytes.length===1024,'Initial state must be 1024 bytes');hash.update(files.state.bytes);
 files.data=await asset(root,m.data,7*1024*1024);hash.update(files.data.bytes);
 }
 if(m.memory!==undefined){
  const privateLimit=require('./private-memory-profile.cjs').validate(m.memory);
  check(m.memory.encoding===undefined||m.memory.encoding==='deflate','Unsupported private memory encoding');
  const item=await asset(root,m.memory.initial,privateLimit);hash.update(item.bytes);
  const bytes=m.memory.encoding==='deflate'?inflateSync(item.bytes,{maxOutputLength:m.memory.bytes}):item.bytes;
  check(bytes.length<=m.memory.bytes,'Private GPU memory image exceeds allocation');files.memory={...item,bytes,allocationBytes:m.memory.bytes};
 }
 files.icon=await asset(root,m.icon,2*1024*1024);hash.update(files.icon.bytes);
 const icon=files.icon.bytes;
 const png=icon.subarray(0,8).equals(Buffer.from('89504e470d0a1a0a','hex'));
 const webp=icon.toString('ascii',0,4)==='RIFF'&&icon.toString('ascii',8,12)==='WEBP';
 check(png||webp,'Icon must be a PNG or WebP');
 if(png)check(icon.readUInt32BE(16)>0&&icon.readUInt32BE(16)<=4096&&icon.readUInt32BE(20)>0&&icon.readUInt32BE(20)<=4096,'Icon dimensions exceed 4096');
 if(validate)await run(require('../platform/python.cjs').command(),['-c','from PIL import Image; import sys; im=Image.open(sys.argv[1]); assert 0<im.width<=4096 and 0<im.height<=4096; im.verify()',files.icon.file],{timeout:10000,maxBuffer:65536});
 const effects={},effectData={};
 check(m.audio===undefined||(m.audio.sampleRate===48000&&m.audio.channels===2&&m.audio.format==='s16le'),'Unsupported audio format');
 for(const [id,name] of Object.entries(m.audio?.effects||{})){
  check(/^(?:[1-9]|[12][0-9]|3[01])$/.test(id),'Sound event ids must be 1..31');
  const item=await asset(root,name,131072);check(item.bytes.length%4===0,'Invalid stereo PCM frame alignment');effects[id]=item.file;effectData[id]=item.bytes;hash.update(id).update(item.bytes);
 }
 return {id:m.id,name:m.name.trim(),version:m.version,description:m.description,root,manifest:m,files,effects,effectData,fingerprint:hash.digest('hex')};
}
async function scan(root=ROOT){
 root=path.resolve(root);let entries;try{entries=await fs.readdir(root,{withFileTypes:true});}catch(error){if(error.code==='ENOENT')return {root,apps:[],rejected:[]};throw error;}
 const apps=[],rejected=[],ids=new Set();
 for(const entry of entries.sort((a,b)=>a.name.localeCompare(b.name))){
  if(entry.name.startsWith('.'))continue;
  if(!entry.isDirectory()){if(!entry.isSymbolicLink())continue;const target=await fs.realpath(path.join(root,entry.name)).catch(()=>null),modules=path.resolve(root,'../../modules');if(!target||!target.startsWith(modules+path.sep)||path.basename(target)!=='switch')continue;}
  try{check(apps.length<MAX_APPS,'Catalog limit is 32 apps');const app=await readApp(path.join(root,entry.name));check(!ids.has(app.id),'Duplicate app id');ids.add(app.id);apps.push(app);}
  catch(error){rejected.push({folder:entry.name,error:error.message});}
 }
 return {root,apps,rejected};
}
function summary(catalog){return {root:catalog.root,apps:catalog.apps.map(a=>({id:a.id,name:a.name,version:a.version,description:a.description,root:a.root,fingerprint:a.fingerprint,effects:a.effects})),rejected:catalog.rejected};}
async function raster(catalog,directory,label='catalog'){
 const input=path.join(directory,label+'.json'),output=path.join(directory,label+'.bin');
 const labels=[];
 const language=await require('../../language/node.cjs').current();
 for(const [i,a] of catalog.apps.entries()){
  const icon=path.join(directory,label+'-icon-'+i+'.png');await fs.writeFile(icon,a.files.icon.bytes);
  labels.push({name:language.t(a.name),searchName:a.name,description:language.t(a.description),icon,transition:a.manifest.transition,displayOnly:a.manifest.displayOnly===true,builtin:a.manifest.builtin,scene:a.manifest.shellContribution?.scene,launchCommand:a.manifest.shellContribution?.launchCommand});
 }
 await fs.writeFile(input,JSON.stringify(labels,null,2));
 await run(require('../platform/python.cjs').command(),[path.join(__dirname,'catalog-assets.py'),input,output],{timeout:15000,maxBuffer:65536,env:{...process.env,...(language.font?{HB_FONT:language.font}:{})}});
 const bytes=await fs.readFile(output);check(bytes.length===MAX_APPS*SLOT_BYTES,'Invalid catalog raster');return bytes;
}
module.exports={ROOT,MAX_APPS,SLOT_BYTES,ATLAS_BYTES,asset,readApp,scan,summary,raster};
