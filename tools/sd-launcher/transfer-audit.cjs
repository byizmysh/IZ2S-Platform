'use strict';
// Exercise the production installer with actual local source files and an SD
// memory model. Count verified payload bytes, never predict console timings.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..');
async function main(){
 const parent=path.join(root,'data/build-cache/sd-transfer-audit');await fs.mkdir(parent,{recursive:true});const directory=await fs.mkdtemp(path.join(parent,'case-'));
 try{
  const data=Buffer.alloc(64*1024*1024,71),sha256=crypto.createHash('sha256').update(data).digest('hex'),asset={file:path.join(directory,'engine.bin'),path:'asset-'+sha256+'.izdata',bytes:data.length,sha256};await fs.writeFile(asset.file,data);
  const spv=Buffer.alloc(20);spv.writeUInt32LE(0x07230203);const files={state:{bytes:Buffer.alloc(1024)},data:{bytes:Buffer.from([1])},vertex:{bytes:spv},fragment:{bytes:spv},compute:{bytes:spv},nativeCode:{bytes:Buffer.from('5f2403d500008052c0035fd6','hex')}};
  const launcher=require('../../core/console/sd-launcher/package.cjs').encode({id:'transfer-proof',name:'Transfer proof',manifest:{native:{format:'iz2s.native-app',abi:2,dataBytes:4096,assets:[{...asset,file:'native/engine.bin'}]}},files},{tile:Buffer.alloc(131072),pad:spv});
  const rows=[];for(const digestReadback of [false,true]){
   const model=require('../../tests/sd-launcher/storage-model.cjs').memoryStorage({digestReadback}),result=await require('../../core/console/sd-launcher/install.cjs').installBytes(model.storage,launcher,{assets:[asset]});
   const bytes=kind=>model.events.filter(e=>e.kind===kind).reduce((n,e)=>n+e.bytes,0),digests=model.events.filter(e=>e.kind==='readHash').length;
   rows.push({mode:digestReadback?'console-digests':'legacy-full-download',writtenBytes:bytes('write'),cardReadbackBytes:bytes('read')+bytes('readHash'),downloadedFileBytes:bytes('read'),digestPayloadBytes:digests*32,verified:result.readbackVerified&&result.assets.every(a=>a.readbackVerified),openHandles:model.open.size,assetSha256:crypto.createHash('sha256').update(model.files.get(result.assets[0].path)).digest('hex')});
  }
  const report={format:'iz-sd-transfer-payload-audit-v1',at:new Date().toISOString(),sourceBytes:data.length,consoleCommands:0,physicalSpeedMeasured:false,measurement:'Production installer, real local bytes and SHA256, SD memory model. Payload counts exclude RPC framing, metadata and filesystem timings.',rows};await fs.writeFile(path.join(root,'data/diagnostics/sd-transfer-payload-audit.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report));return report;
 }finally{await fs.rm(directory,{recursive:true,force:true});}
}
module.exports={main};if(require.main===module)main().catch(error=>{console.error(error);process.exitCode=1;});
