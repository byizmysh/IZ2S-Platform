'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const root=path.resolve(__dirname,'../..'),archive=require('../../core/archive.cjs');
const compat=require('../../engines/iz2s-core/sd-compat.cjs');
const prepare=require('../../engines/iz2s-core/tools/prepare-sd-app.cjs');
const packages=require('../../core/console/sd-launcher/package.cjs');
const assets=require('../../core/console/sd-launcher/assets.cjs');
const expected={
 'iz2s-cheese':['0.8.7','0b2d6c8fe32ab5679c9da58840a8bbcaf06caf24ca7ab95484681df6dc80a9b8','f328dcb8b2ecdb4866c38dbb4328d804af07dafc7259d74686bd17d450450426',113816],
 izpsx:['0.5.3','7257c46bb8a018849cf25d2e38591fbddb5a1083303c9ee859bf1ab2edb86a3b','4b0a995128b0cb0708aa38c800f16bbe509050c43e354a8d0ffeffb261425121',1746208],
 izps2:['0.5.0','56d2640699ebfee3cab472a87ebdb23a59ee8a0ff1e746c9d998645784e7dd6a','cb511c6a3465f95e6ee0047b4380ee7bef82e1abee671a86f3dc1fd6f9781dba',42538024],
};
async function moduleFor(id){const homes={'iz2s-cheese':'iz2s-cheese',izpsx:'IZPSX',izps2:'IZPS2'};return archive.inspect(await fs.readFile(path.join(root,'modules',homes[id],id+'.module.zip')));}
for(const [id,[version,imageSha,assetSha,assetBytes]]of Object.entries(expected))test(id+' library export preserves the published IZA and companions',async()=>{
 const checked=await moduleFor(id);assert.equal(checked.descriptor.version,version);
 const image=compat.directImage(checked.descriptor,checked.files);assert.equal(archive.sha(image.bytes),imageSha);
 const scratch=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s-release-test-'));
 try{
  const exported=await prepare.packageModule(checked,scratch);assert.equal(exported.packed,true);
  // ZIP compression varies between Node/zlib releases. Verify the exact
  // upstream executable, companion bytes and metadata inside the container.
  const members=new Map(archive.decode(exported.bytes).map(file=>[file.name,file.bytes]));
  const assetName='asset-'+assetSha+'.izdata';
  assert.deepEqual([...members.keys()].sort(),[assetName,'launcher.iza','sd-bundle.json',...(id==='izps2'?['vu-split-persist-off.flag']:[])].sort());
  assert.equal(archive.sha(members.get('launcher.iza')),imageSha);
  assert.equal(members.get(assetName).length,assetBytes);
  assert.equal(archive.sha(members.get(assetName)),assetSha);
  assert.deepEqual(JSON.parse(members.get('sd-bundle.json')),{
   format:'iz2s.sd-bundle',version:1,id,launcher:{bytes:image.bytes.length,sha256:imageSha},
   assets:[{file:assetName,path:assetName,bytes:assetBytes,sha256:assetSha}],
  });
 }
 finally{await fs.rm(scratch,{recursive:true,force:true});}
});
test('prebuilt import rejects missing, damaged and undeclared companions and mismatched metadata',async()=>{
 const {descriptor,files}=await moduleFor('iz2s-cheese');
 const asset=files.find(f=>f.name.endsWith('.izdata'));
 assert.throws(()=>compat.directImage(descriptor,files.filter(f=>f!==asset)),/SD asset/);
 assert.throws(()=>compat.directImage(descriptor,files.map(f=>f===asset?{...f,bytes:Buffer.alloc(f.bytes.length)}:f)),/checksum/);
 assert.throws(()=>compat.directImage({...descriptor,switch:{...descriptor.switch,files:descriptor.switch.files.filter(n=>n!==asset.name)}},files),/Declare/);
 assert.throws(()=>compat.directImage(descriptor,files.filter(f=>!f.name.endsWith('/sd-bundle.json'))),/companion/);
 const metadata=files.find(f=>f.name.endsWith('/sd-bundle.json')),j=JSON.parse(metadata.bytes);j.launcher.sha256='0'.repeat(64);
 assert.throws(()=>compat.directImage(descriptor,files.map(f=>f===metadata?{...f,bytes:Buffer.from(JSON.stringify(j))}:f)),/checksum/);
 const different=JSON.parse(metadata.bytes);different.assets[0].path='../escape.izdata';
 assert.throws(()=>compat.directImage(descriptor,files.map(f=>f===metadata?{...f,bytes:Buffer.from(JSON.stringify(different))}:f)),/path/);
});
test('Civ7 ZIP is a complete four-app SD tree with verified resources and original binaries',async()=>{
 const {buildSdPackage}=await import('../../engines/iz2sciv-core/tools/sd-package.mjs');
 const output=await buildSdPackage(),entries=archive.decode(output.bytes);
 const files=new Map(entries.map(f=>[f.name,f.bytes]));
 assert(entries.every(f=>f.name.startsWith('iz2s/')&&!f.name.includes('..')));
 const manifest=JSON.parse(files.get('iz2s/iz2sciv-sd-manifest.json'));
 assert.deepEqual(manifest.apps.map(a=>a.id),['iz2s-cheese','izpsx','iz-file-deck','nx2-brerun']);
 for(const row of manifest.files){const bytes=files.get(row.path);assert(bytes,row.path);assert.equal(bytes.length,row.bytes);assert.equal(archive.sha(bytes),row.sha256);}
 assert.equal(manifest.files.length+1,entries.length);
 for(const app of manifest.apps){
  if(app.id==='nx2-brerun'){
   assert.equal(app.format,'.nro');
   assert.equal(app.version,'0.6.0');
   const bytes=files.get(app.path+'launcher.nro');
   const release=JSON.parse(await fs.readFile(path.join(root,'distribution/application-origins.json'))).find(row=>row.id===app.id);
   assert.equal(bytes.length,release.bytes);
   assert.equal(archive.sha(bytes),release.sha256);
   assert.deepEqual(app.launcher,{bytes:release.bytes,sha256:release.sha256});
   assert(files.has('iz2s/licenses/applications/nx2-brerun/LICENSE'));
   const checked=archive.inspect(await fs.readFile(path.join(root,'modules/NX2BreRUN/nx2-brerun.module.zip')));
   const scratch=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s-nro-test-'));
   try{
    const exported=await prepare.packageModule(checked,scratch);
    assert.equal(exported.name,'nx2-brerun.nro');
    assert.deepEqual(exported.bytes,bytes);
   }finally{await fs.rm(scratch,{recursive:true,force:true});}
   continue;
  }
  const bytes=files.get(app.path+'launcher.iza'),header=packages.decode(bytes),bundle=JSON.parse(files.get(app.path+'sd-bundle.json'));
  assert.equal(header.id,app.id);assets.verifyBinding(header,bundle.assets);
  for(const row of bundle.assets){const data=files.get(app.path+row.path);assert.equal(data.length,row.bytes);assert.equal(archive.sha(data),row.sha256);}
  if(expected[app.id]){assert.equal(app.version,expected[app.id][0]);assert.equal(archive.sha(bytes),expected[app.id][1]);}
 }
 assert(files.has('iz2s/modules/foldrift/launcher.iza'));assert(!files.has('iz2s/modules/iz-file-deck/launcher.iza'));
 assert(!entries.some(f=>/\/(?:bios|games|saves)\/|\.(?:iso|chd|pbp|key|log)$/.test(f.name)));
 assert(!entries.some(file=>file.name.split('/').includes('izps2')||file.name.endsWith('/vu-split-persist-off.flag')));
 assert.equal(manifest.packageVariant,'four-apps');
 assert.equal(manifest.panelVersion,'0.34');
 assert.equal(manifest.launcherVersion,'0.34');
 assert(!entries.some(f=>/(?:^|\/)iz2s\.nro$|menu-vulkan|shader-cache/i.test(f.name)));
 assert(files.has('iz2s/README-IZ2SCiv.txt'));
});
