'use strict';
// Installer preflight: runtime integrity and catalogs, no network or device I/O.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),profile=require('./profile.json');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
async function verify(){
 const checked=await require('./runtime-files.cjs').verify(root);
 const D=require('../core/descriptors.cjs'),catalog={};
 for(const kind of ['engine','shell','profile']){const scan=D.scan(root,kind);assert.deepEqual(scan.errors,[]);assert(scan.items.length);catalog[kind]=scan.items;}
 for(const id of profile.preinstalledEngines)assert(catalog.engine.some(e=>e.id===id&&e.installed!==false),'Engine unavailable: '+id);
 for(const shell of profile.shells)require('../core/session/runtime-check.cjs').verify(root,shell);
 require('../core/console/sd-launcher/check.cjs').verifyRuntime();
 const core=await require('../engines/iz2s-core/runtime.cjs').verify();
 // Bind the native menu and shaders to the sources used for this release.
 // Updating only a distribution checksum must not bless a stale executable.
 const menuBuild=JSON.parse(await fs.readFile(path.join(root,'engines/iz2s-core/runtime/menu-build.json')));
 assert.equal(menuBuild.format,'iz2s.menu-build.v1');
 for(const group of ['inputs','outputs']){
  assert(Object.keys(menuBuild[group]).length>0,'Menu build has no '+group);
  for(const [name,digest]of Object.entries(menuBuild[group])){
   assert(/^[a-f0-9]{64}$/.test(digest)&&!/[\\:\x00-\x1f]/.test(name)&&
     !name.split('/').some(part=>!part||part==='.'||part==='..'),'Invalid menu build entry');
   assert.equal(hash(await fs.readFile(path.join(root,name))),digest,'Rebuild IZ2s: '+name);
  }
 }
 assert.equal((await require('../engines/iz2s-core/tools/menu-release.cjs').release()).id,menuBuild.menuId);
 // These entry points are loaded through child processes or a separate local
 // service. Descriptor discovery and ordinary require() scans do not reach them.
 const launchResources=[
  'engines/CivSwitchLDN/bash/network-panel.sh',
  'engines/CivSwitchLDN/bash/network.sh',
  'engines/CivSwitchLDN/bash/setup.sh',
  'engines/iz2s-core/tools/pc-link.cjs',
  'engines/iz2s-core/tools/pc-link-files.cjs',
  'engines/iz2s-core/tools/pc-link-state.cjs',
  'engines/iz2s-core/tools/installed-versions.cjs',
  'engines/iz2s-core/tools/official-apps.json',
  'engines/iz2s-core/tools/pc-stream.cjs',
  'engines/iz2s-core/tools/pc-media-source.cjs',
  'engines/iz2s-core/tools/pc-media-control.cjs',
  'engines/iz2s-core/tools/pc-media-input.cjs',
  'engines/iz2s-core/tools/paired-sd.cjs',
  'engines/iz2s-core/tools/paired-sd.py',
  'engines/iz2s-core/adapters/pc-media/native.bin',
  'engines/iz2s-core/tools/prepare-sd-app.cjs',
  'engines/iz2s-core/tools/ftp-connect.cjs',
  'engines/iz2s-core/tools/nro-package.cjs',
  'engines/iz2s-core/tools/install-nro.py',
  'engines/iz2s-core/tools/deployment.py',
  'engines/iz2s-core/tools/deploy-runtime.py',
  'engines/iz2s-core/tools/install-launcher-menu.py',
 ];
 for(const file of launchResources){const stat=await fs.stat(path.join(root,file));assert(stat.isFile()&&stat.size>0,'Launch resource absent: '+file);}
 for(const owner of ['engines/CivSwitch/installer-creator','engines/CivSwitchLDN']){
  const folder=path.join(root,owner),entry=require.resolve('terser',{paths:[folder]});
  assert(entry.startsWith(path.join(folder,'node_modules')+path.sep),'Runtime minifier must be bundled with '+owner);
  assert.equal(typeof require(entry).minify,'function');
 }
 const inventory=require('../hardware/index.cjs').configurations(root);assert.deepEqual(inventory.errors,[]);
 const modules=[];
 for(const folder of profile.moduleDirectories){
  const home=path.join(root,'modules',folder),descriptor=D.moduleDescriptor(JSON.parse(await fs.readFile(path.join(home,'module.json'))));
  assert(profile.modules.includes(descriptor.id));
  const archive=require('../core/archive.cjs'),bytes=await fs.readFile(path.join(home,descriptor.id+'.module.zip')),pkg=archive.inspect(bytes);
  assert.equal(pkg.descriptor.id,descriptor.id);assert(archive.sameContents(bytes,await archive.pack(home)),'Module archive is stale: '+descriptor.id);
  for(const dep of descriptor.dependencies||[])if(dep.kind==='module')assert(profile.modules.includes(dep.id),'Module dependency absent');
  modules.push(descriptor.id);
 }
 const {buildSdApplications,APP_IDS}=await import('../engines/IzCivSwitchFS/tools/sd-apps.mjs');
 const sdKit=await buildSdApplications(root);
 assert.deepEqual(sdKit.apps.map(app=>app.id),APP_IDS);
 for(const name of ['manifest.json','manifest-sb4.json','manifest-civ.json']){
  const home=path.join(root,'hardware/devices/cardputer-adv/firmware'),m=JSON.parse(await fs.readFile(path.join(home,name))),bytes=await fs.readFile(path.join(home,m.file));assert.equal(hash(bytes),m.sha256,name);assert.equal(bytes.length,m.bytes);
 }
 const fontHome=path.join(root,'language/fonts'),fonts=JSON.parse(await fs.readFile(path.join(fontHome,'manifest.json')));
 for(const [name,digest]of Object.entries(fonts.files))assert.equal(hash(await fs.readFile(path.join(fontHome,name))),digest,'Font: '+name);
 const agents=require('../hardware/agents.cjs');await agents.files(root);await agents.installer(root,'sb4');await agents.installer(root,'civ7');
 const keyboard=path.join(root,'hardware/devices/switch1/firmware');
 for(const line of (await fs.readFile(path.join(keyboard,'SHA256SUMS.txt'),'utf8')).trim().split('\n')){const m=/^([a-f0-9]{64})\s+\*?(.+)$/.exec(line);assert(m);assert.equal(hash(await fs.readFile(path.join(keyboard,m[2]))),m[1]);}
 const report={verified:true,edition:profile.edition,files:checked.files+1,bytes:checked.bytes,engines:catalog.engine.map(e=>e.id),shells:catalog.shell.map(e=>e.id),profiles:catalog.profile.map(e=>e.id),modules,sdApplications:sdKit.apps.map(({id,version})=>({id,version})),coreFiles:core.files,launchResources:launchResources.length};
 console.log(JSON.stringify(report));return report;
}
if(require.main===module)verify().catch(e=>{console.error(e.stack);process.exitCode=1;});module.exports={verify};
