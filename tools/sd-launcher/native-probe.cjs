'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(session){
 assert(/^launcher-[A-Za-z0-9]+$/.test(session||''));const directory=path.join(root,'data/sessions',session),boundary=await require('../../core/console/sd-launcher/boundary.cjs').acquire(directory),nonce=require('node:crypto').randomInt(1,0xffffffff),supervisor=await require('../../core/console/sd-launcher/bootstrap.cjs').deploy({directory,probe:2,probeNonce:nonce,displayToken:boundary.token});
 let s;const end=Date.now()+15000;do{await pause(100);s=await supervisor.status();if(s.state===3||s.state===4)break;}while(Date.now()<end);
 supervisor.report.observed=s;await supervisor.save();console.log(JSON.stringify(s));assert(s.state===3&&s.probe_result===0&&s.poisoned===0,'Native SD execution failed');assert.equal(s.native_launches,1);assert.equal(s.frames,120);assert(s.native_code_reads>0);await supervisor.close();
 const a=await require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc')),storage=await require('../../core/console/sd-launcher/storage.cjs').create(a);
 const file=await storage.openRead('/sd/IZ2S/Homebrew/Modules/sd-proof-native/native-receipt.bin');assert.equal(file.size,32);const b=await storage.read(file,0,32);await storage.close(file);await a.close();
 let expected=(0x5a17c0de^0x9e3779b9)>>>0;for(let frame=0;frame<120;frame++)for(let i=0;i<257;i++)expected=(Math.imul(expected^((expected<<13)|(expected>>>19)),1664525)+1013904223)>>>0;
 const receipt={seed:b.readUInt32LE(0),frames:b.readUInt32LE(4),result:b.readUInt32LE(8),readBytes:b.readUInt32LE(12),magic:b.toString('ascii',16,24),initialized:b.readUInt32LE(24),stopped:b.readUInt32LE(28)};assert.equal(receipt.seed,0x5a17c0de);assert.equal(receipt.frames,120);assert.equal(receipt.result,expected);assert.equal(receipt.magic,'IZSDAPP1');assert.equal(receipt.stopped,1);
 await boundary.release();const result={format:'iz-native-sd-hardware-proof-v1',passed:true,session,at:new Date().toISOString(),mode:'native-thread-at-joined-render-boundary',hostModuleImageUploadBytes:0,bootstrapBytes:supervisor.report.bootstrapBytes,nativeCodeBytesRead:s.native_code_reads,receipt,workerJoined:true,mappingsRemoved:true,allocationsReleased:true};const output=path.join(directory,'sd-native-execution-'+nonce+'.json');await fs.writeFile(output,JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({...result,report:output}));
}
if(require.main===module)main(process.argv[2]).catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
