'use strict';
const fs=require('node:fs/promises'),sync=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),vm=require('node:vm'),Module=require('node:module'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),profile=require('./profile.json'),hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
async function verify({release=process.argv.includes('--release')}={}){
 const files=[];
 async function walk(dir){for(const e of await fs.readdir(dir,{withFileTypes:true})){
  if(release&&(e.name==='__pycache__'||e.name.endsWith('.pyc')))throw Error('Python build cache included: '+path.relative(root,path.join(dir,e.name)));
  if(['data','dist','.git','bootstrap-chain','.versions','__pycache__','node_modules'].includes(e.name))continue;
  const file=path.join(dir,e.name);if(e.isSymbolicLink())throw Error('Release source must not contain symlinks: '+file);if(e.isDirectory())await walk(file);else files.push(file);
 }}await walk(root);
 for(const file of files){
  const bytes=await fs.readFile(file),text=bytes.toString('utf8'),relative=path.relative(root,file);
  if(/-----BEGIN (?:OPENSSH |RSA |EC |DSA )?PRIVATE KEY-----|gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}/.test(text))throw Error('Credential-like content: '+relative);
  if(release&&text.includes('/home/'+'k2/'))throw Error('Workstation path: '+relative);
  require('./text-policy.cjs').check(relative,text);
  if(/\.(?:cjs|js)$/.test(file))new vm.Script(Module.wrap(text.replace(/^#![^\n]*\n/,'')),{filename:relative});
  if(file.endsWith('.json'))JSON.parse(text);
 }
 for(const shell of profile.shells)require('../core/session/runtime-check.cjs').verify(root,shell);
 const D=require('../core/descriptors.cjs');
 for(const kind of ['engine','shell','profile']){const scan=D.scan(root,kind);assert.deepEqual(scan.errors,[]);assert(scan.items.length);}
 const engines=require('../engines/index.cjs').create(root).list();
 for(const id of profile.preinstalledEngines){const engine=engines.find(e=>e.id===id);assert(engine&&engine.installed!==false,id+' unavailable');}
 const iz=require('../engines/izmysh/engine.json'),berm=require('../engines/BermTheSequel/engine.json');
 assert.equal(iz.consolePort,berm.consolePort);assert.equal(iz.bootstrap.sha256,berm.bootstrap.sha256);assert.equal(iz.bootstrap.saveName,'NS2IZM');assert.equal(berm.bootstrap.saveName,'NS2B');
 require('../engines/izmysh/runtime/contract.cjs').verify();require('../engines/BermTheSequel/upstream/engine/check-runtime.cjs').verify();
 const archive=require('../core/archive.cjs');
 for(const folder of profile.moduleDirectories){
  const home=path.join(root,'modules',folder),descriptor=D.moduleDescriptor(JSON.parse(await fs.readFile(path.join(home,'module.json'))));
  assert(profile.modules.includes(descriptor.id));
  const packed=await archive.pack(home),pkg=archive.inspect(packed);
  assert.equal(pkg.descriptor.id,descriptor.id);assert(!pkg.files.some(f=>f.name.startsWith('data/')));
  for(const dependency of descriptor.dependencies||[])if(dependency.kind==='module')assert(profile.modules.includes(dependency.id),'Missing dependency '+dependency.id);
 }
 const firmwareRoot=path.join(root,'hardware/devices/cardputer-adv/firmware'),manifest=JSON.parse(await fs.readFile(path.join(firmwareRoot,'manifest.json')));
 assert.equal(hash(await fs.readFile(path.join(firmwareRoot,manifest.file))),manifest.sha256);
 for(const [name,digest]of Object.entries(manifest.sources))assert.equal(hash(await fs.readFile(path.join(root,'hardware/agents/cardputer-adv',name))),digest,name);
 const endpoint=require('../hardware/devices/cardputer-adv/settings.cjs').endpoint;
 assert.equal(endpoint('192.168.1.50'),'http://192.168.1.50');assert.equal(endpoint('iz2s-adv.local'),'http://iz2s-adv.local');assert.throws(()=>endpoint('http://host/path'));
 const fonts=JSON.parse(await fs.readFile(path.join(root,'language/fonts/manifest.json')));
 for(const [name,digest] of Object.entries(fonts.files))assert.equal(hash(await fs.readFile(path.join(root,'language/fonts',name))),digest,'Bundled font: '+name);
 const arm=JSON.parse(await fs.readFile(path.join(root,'hardware/agents/arm-input/checksums.json')));
 for(const [name,digest]of Object.entries(arm.files))assert.equal(hash(await fs.readFile(path.join(root,'hardware/agents/arm-input',name))),digest,name);
 const shaders=files.filter(f=>f.endsWith('.spv'));
 const val=spawnSync('spirv-val',['--version'],{encoding:'utf8'});
 for(const shader of shaders){assert.equal((await fs.readFile(shader)).readUInt32LE(),0x07230203);if(val.status===0){const result=spawnSync('spirv-val',['--target-env','vulkan1.0',shader],{encoding:'utf8'});assert.equal(result.status,0,result.stderr);}}
 if(release){
  assert(!sync.existsSync(path.join(root,'data')),'Private root data included');
  assert(!sync.existsSync(path.join(root,'bootstrap-chain')),'Session engine snapshots included');
  const names=(await fs.readdir(path.join(root,'modules'),{withFileTypes:true})).filter(e=>e.isDirectory()).map(e=>e.name).sort();assert.deepEqual(names,[...profile.moduleDirectories].sort());
  for(const folder of profile.moduleDirectories)assert(!sync.existsSync(path.join(root,'modules',folder,'data')),'Private module data included');
 }
 const manifestFile=path.join(root,'SOURCE-MANIFEST.json');
 if(sync.existsSync(manifestFile))for(const [name,entry]of Object.entries(JSON.parse(await fs.readFile(manifestFile)).files))assert.equal(hash(await fs.readFile(path.join(root,name))),entry.sha256,'Release checksum: '+name);
 console.log(JSON.stringify({verified:true,version:profile.version,files:files.length,shells:profile.shells,modules:profile.modules,preinstalledEngines:profile.preinstalledEngines,shaders:shaders.length,shaderValidator:val.status===0,advFirmware:manifest.version}));
}
if(require.main===module)verify().catch(error=>{console.error(error.stack);process.exitCode=1;});module.exports={verify};
