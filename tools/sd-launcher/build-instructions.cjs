'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
 const root=path.resolve(__dirname,'../..'),folder=path.join(root,'core/console/sd-launcher/instructions'),source=path.join(folder,'runtime.c'),header=path.join(root,'core/console/sd-launcher/native/native-app.h');
 const built=await require('./build-native.cjs').build(source,path.join(root,'data/build-cache/local-instructions'),{abi:2}),code=await fs.readFile(built.file);
 const report={format:'iz2s.local-instructions-native-v1',abi:2,bytes:code.length,sha256:sha(code),sourceSha256:sha(await fs.readFile(source)),checkpointSha256:sha(await fs.readFile(path.join(folder,'checkpoint.h'))),nativeAbiSha256:sha(await fs.readFile(header)),pic:true,entryOffset:0};
 await fs.writeFile(path.join(folder,'runtime.bin'),code);await fs.writeFile(path.join(folder,'native.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));return report;
}
module.exports={main};if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});
