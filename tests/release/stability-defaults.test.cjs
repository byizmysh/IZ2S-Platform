'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const root=path.resolve(__dirname,'../..'),archive=require('../../core/archive.cjs');
const prepare=require('../../engines/iz2s-core/tools/prepare-sd-app.cjs');
const defaults=require('../../engines/iz2s-core/tools/sd-defaults.cjs');
const flag='vu-split-persist-off.flag';
const flagSha='d5ecff7e14596fdc9c09937a62705d269ca96a7abcf4f76df65b919457ceefe9';
test('stability policy matches the console-tested flag and applies only to IZPS2',()=>{
 const files=defaults.files('izps2');assert.equal(files.length,1);assert.equal(files[0].name,flag);
 assert.equal(archive.sha(files[0].bytes),flagSha);
 for(const id of ['izpsx','iztwins2','iz2s-cheese','iz-file-deck','../izps2'])assert.deepEqual(defaults.files(id),[]);
});
test('IZPS2 module export and subsequent import retain the stability policy and tested engine',async()=>{
 const checked=archive.inspect(await fs.readFile(path.join(root,'modules/IZPS2/izps2.module.zip')));
 const scratch=await fs.mkdtemp(path.join(os.tmpdir(),'iz2s-stability-'));
 try{
  const exported=await prepare.packageModule(checked,scratch),files=new Map(archive.decode(exported.bytes).map(f=>[f.name,f.bytes]));
  assert.equal(archive.sha(files.get(flag)),flagSha);
  assert.equal(archive.sha(files.get('launcher.iza')),'56d2640699ebfee3cab472a87ebdb23a59ee8a0ff1e746c9d998645784e7dd6a');
  const input=path.join(scratch,'install.iza');await fs.writeFile(input,exported.bytes);
  const damaged=archive.encode([...files].map(([name,bytes])=>({name,bytes:name===flag?Buffer.from('changed'):bytes})));
  const bad=path.join(scratch,'damaged.iza');await fs.writeFile(bad,damaged);
  await assert.rejects(prepare.prepare(bad,scratch),/must contain only/);
  const installed=await prepare.prepare(input,scratch);
  assert.equal(archive.sha(await fs.readFile(path.join(installed.folder,flag))),flagSha);
  assert.equal(installed.assets,1);
  const asset='asset-cb511c6a3465f95e6ee0047b4380ee7bef82e1abee671a86f3dc1fd6f9781dba.izdata';
  assert.equal(archive.sha(await fs.readFile(path.join(installed.folder,asset))),asset.slice(6,70));
 }finally{await fs.rm(scratch,{recursive:true,force:true});}
});
