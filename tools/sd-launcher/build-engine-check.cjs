'use strict';
// Local-only preparation. The inspected engine is never entered by this app.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..');
async function main(image='data/build-cache/sd-image-loader-test/engine.izrel',id='sd-engine-check'){
 assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id));
 image=path.resolve(root,image);assert(image.startsWith(root+path.sep),'Use a prepared engine image inside RUN22');
 const child=await fs.readFile(image);assert(child.subarray(0,8).toString()==='IZNATRL1');
 const sha256=crypto.createHash('sha256').update(child).digest('hex'),relative='asset-'+sha256+'.izdata';
 const home=path.join(root,'data/build-cache',id),app=path.join(home,'app');await fs.mkdir(home,{recursive:true});
 const base=path.join(root,'data/build-cache/sd-demo/sd-proof-native/switch');await fs.cp(base,app,{recursive:true});
 const native=await require('./build-native.cjs').build(path.join(root,'tests/sd-launcher/cooperative-entry.c'),path.join(app,'native'),{abi:4});
 const initial=Buffer.alloc(256);initial.write(relative);initial.writeUInt32LE(1,80); // Inspect only; never call the image entry.
 await fs.writeFile(path.join(app,'native/data.bin'),initial);await fs.copyFile(image,path.join(app,'native/engine.izrel'));
 const manifest=JSON.parse(await fs.readFile(path.join(app,'app.json')));manifest.id=id;manifest.name='SD engine image check';manifest.description='Incremental engine load and release; does not enter engine code';
 manifest.native={format:'iz2s.native-app',abi:4,code:'native/native.bin',initial:'native/data.bin',dataBytes:4096,assets:[{file:'native/engine.izrel',path:relative,bytes:child.length,sha256}]};
 await fs.writeFile(path.join(app,'app.json'),JSON.stringify(manifest,null,2)+'\n');
 const result=await require('../../core/console/sd-launcher/package.cjs').build(app,path.join(home,'sd'));
 const report={format:'iz-sd-engine-check-build-v1',id,at:new Date().toISOString(),output:result.output,bundle:result.bundle,bytes:result.bytes.length,sha256:result.sha256,image:{file:image,bytes:child.length,sha256,path:relative},native,engineEntered:false,hardwareQualified:false};
 await fs.writeFile(path.join(home,'manifest.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));return report;
}
if(require.main===module)main(process.argv[2],process.argv[3]).catch(e=>{console.error(e);process.exitCode=1;});
module.exports={main};
