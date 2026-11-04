'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const C=require('../../core/services/app-catalog.cjs'),L=require('../../shells/izlauncher/resident/layout.cjs');
const ROOT=path.resolve(__dirname,'../..');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function menuRoot(shell='izlauncher',root=ROOT){
 if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Unsupported resident shell');
 return path.join(root,'shells',shell,'app');
}
function trim(b){let n=b.length;while(n>4&&b[n-1]===0)n--;return b.subarray(0,Math.min(b.length,Math.ceil(n/4)*4));}
const {candidate}=require('../../shells/izlauncher/resident/compatibility.cjs');
async function resolve(source,cache){
 source=path.resolve(source);
 if(!source.endsWith('.izm'))return C.readApp(source);
 const bytes=await fs.readFile(source),pkg=require('../../core/archive.cjs').inspect(bytes);
 if(!pkg.descriptor.switch?.entry)throw Error('Capsule has no GPU app');
 const dir=path.join(cache,pkg.sha256);
 if(!await fs.access(path.join(dir,'module.json')).then(()=>true,()=>false))await require('../../core/archive.cjs').unpack(bytes,dir);
 return C.readApp(path.dirname(path.join(dir,pkg.descriptor.switch.entry)));
}
async function prepare(output,sources,{budgetBytes=256*1024*1024,qualifyFiniteAudio=false,shell='izlauncher'}={}){
 const menuDirectory=menuRoot(shell),server=path.join(menuDirectory,'../server');
 output=path.resolve(output);await fs.mkdir(output,{recursive:true});
 await fs.rm(path.join(output,'ready.json'),{force:true});
 const cache=path.join(ROOT,'data/resident-build/capsules'),apps=[];
 for(const source of sources){const app=await resolve(source,cache),classification=candidate(app);if(!classification.compatible){if(!qualifyFiniteAudio)throw Error(app.id+': '+classification.reason);app.residentPolicy=require('../../shells/izlauncher/resident/services.cjs').policy(app);}apps.push(app);}
 if(apps.filter(app=>app.residentPolicy?.service).length>1)throw Error('Only one finite audio app may own the resident output');
 if(new Set(apps.map(a=>a.id)).size!==apps.length)throw Error('Duplicate resident app identities');
 const menu=await C.readApp(menuDirectory);
 const menuState=Buffer.alloc(L.STATE_BYTES);(await require(path.join(server,'font-data.cjs')).load()).copy(menuState,1024);
 const raster=shell==='izpslauncher'?require(path.join(server,'raster.cjs')).raster:C.raster;
 const atlas=await raster({apps},output,'resident-catalog');atlas.copy(menuState,1024+C.ATLAS_BYTES);
 const type=await require(path.join(server,'assets.cjs')).loadTypeAtlas();type.bytes.copy(menuState,1024+type.metadata.offset);
 if(shell==='izpslauncher')require(path.join(server,'clock.cjs')).header().copy(menuState,1024+0x50000);
 menu.files.state.bytes.copy(menuState,0);menuState.writeUInt32LE(apps.length,112);menuState.writeUInt32LE(apps.length,1020);
 menuState.writeUInt32LE(0x495a4335,0x70001c);
 const all=[menu,...apps],specs=[],inventory=[];let used=L.INPUT_BYTES+(L.TABLE+L.MAX_SLOTS*L.STRIDE)*4;
 for(const [slot,app] of all.entries()){
  const dir=path.join(output,'slot-'+slot);await fs.mkdir(dir,{recursive:true});
  const state=slot===0?menuState:Buffer.concat([app.files.state.bytes,app.files.data.bytes]);
  const initialState=trim(state),privateBytes=app.files.memory?.allocationBytes||4,initialPrivate=trim(app.files.memory?.bytes||Buffer.alloc(4));
  used+=L.STATE_BYTES+privateBytes+(slot===0?L.STATE_BYTES:initialState.length)+(initialPrivate.length>32*1048576?Math.ceil(initialPrivate.length/4096)*4096:initialPrivate.length);
  if(used>budgetBytes)throw Error('Resident memory budget exceeded before allocation: '+used+' > '+budgetBytes);
  await fs.writeFile(path.join(dir,'initial-state.bin'),initialState);await fs.writeFile(path.join(dir,'initial-private.bin'),initialPrivate);
  for(const [stage,name] of [['vertex','app.vert.spv'],['fragment','app.frag.spv'],['compute','app.comp.spv'],['computeBegin','begin.comp.spv'],['computeAfter','after.comp.spv']])
   if(app.files[stage])await fs.writeFile(path.join(dir,name),app.files[stage].bytes);
  const repeat=Buffer.alloc(4);repeat.writeUInt32LE(app.manifest.computeSequence?.repeat||1);await fs.writeFile(path.join(dir,'repeat.bin'),repeat);
  specs.push({...app.residentPolicy,privateBytes,initialStateBytes:initialState.length,initialPrivateBytes:initialPrivate.length,
   grid:app.manifest.computeWorkgroups||[1,1,1],sequence:!!app.manifest.computeSequence});
  inventory.push({...app.residentPolicy,slot,id:app.id,version:app.version,fingerprint:app.fingerprint,grid:specs.at(-1).grid,sequence:app.manifest.computeSequence||null,
   privateBytes,initialStateBytes:initialState.length,initialPrivateBytes:initialPrivate.length,initialStateSHA256:sha(initialState),initialPrivateSHA256:sha(initialPrivate),
   shaderSHA256:Object.fromEntries(['vertex','fragment','compute','computeBegin','computeAfter'].filter(s=>app.files[s]).map(s=>[s,sha(app.files[s].bytes)]))});
 }
 await fs.writeFile(path.join(output,'resident.bin'),L.make(specs));
 const shaders=await require('../../shells/izlauncher/resident/build.cjs').build(path.join(output,'shaders'));
 const manifest={format:'izlauncher.resident-preparation',version:1,shell,slots:inventory,estimatedBufferBytes:used,budgetBytes,
  restartOnEntry:true,shaders,consoleExecuted:false,audioProviderReady:false,finiteAudioLifetimeQualification:qualifyFiniteAudio,
  qualification:'Desktop preparation only. No app without a hostRuntime is presumed hardware-qualified merely from its manifest.'};
 await fs.writeFile(path.join(output,'ready.json'),JSON.stringify(manifest,null,2)+'\n');return manifest;
}
module.exports={prepare,resolve,candidate,trim,menuRoot};
if(require.main===module)prepare(process.argv[2],process.argv.slice(3)).then(r=>console.log(JSON.stringify(r,null,2))).catch(e=>{console.error(e);process.exitCode=1;});
