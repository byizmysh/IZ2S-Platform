'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
 const root=path.resolve(__dirname,'../..'),folder=path.join(root,'core/console/sd-launcher/adapters'),source=path.join(folder,'izdoom.c'),header=path.join(root,'core/console/sd-launcher/native/native-app.h');
 const build=await require('./build-native.cjs').build(source,path.join(root,'data/build-cache/sd-adapters/izdoom'),{abi:2});
 const image=await fs.readFile(build.file),report={format:'iz2s.sd-adapter-v1',id:'izdoom-prime18',abi:2,bytes:image.length,sha256:sha(image),sourceSha256:sha(await fs.readFile(source)),nativeAbiSha256:sha(await fs.readFile(header)),pic:true,entryOffset:0};
 await fs.writeFile(path.join(folder,'izdoom.bin'),image);await fs.writeFile(path.join(folder,'izdoom-native.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));return report;
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
