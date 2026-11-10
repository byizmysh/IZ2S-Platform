'use strict';
// Derive an SD-specific BASIC source from the pinned upstream bootstrap.
// The native handshake ABI stays unchanged; reserved hello word 14 identifies
// the quiet disconnect tail. Upstream source and its verification stay intact.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const MARKER=0x495a5344,SAVE_NAME='NS2SD';
function source(original){
 const text=original.toString('ascii');assert(Buffer.from(text,'ascii').equals(original),'Expected ASCII bootstrap');
 const hello='HELLO$=HELLO$+P$(0)+P$(0)+P$(0)+P$(0)+P$(3925528160)',tail='PHASE=6:PRINT "RPC STOP",R#:RETURN 1';
 assert.equal(text.split(hello).length,2,'Bootstrap greeting changed');assert.equal(text.split(tail).length,2,'Bootstrap disconnect tail changed');
 const sleep=require('../abi/thread-code.json').functions.sleep;assert.equal(sleep.bytes,'e86b1214','Requalify the BASIC idle sleep function');
 const quiet=['PHASE=6:PRINT "IZ SD OFFLINE"',"' Keep BASIC in RUN; Direct would consume pad input and play key sounds.",'WHILE 1',' R#=CALL#(&H'+sleep.offset.toString(16).toUpperCase()+',1000000000,0,0,0)','WEND'].join('\n');
 return Buffer.from(text.replace(hello,'HELLO$=HELLO$+P$(0)+P$(0)+P$(0)+P$(&H495A5344)+P$(3925528160)').replace(tail,quiet),'ascii');
}
function select({root,bootstrap,id}){
 assert.equal(id,'izmysh','The quiet SD bootstrap is currently qualified only for IZ2S Core');
 const description=JSON.parse(fs.readFileSync(path.join(root,'engines/izmysh/engine.json'))),original=fs.readFileSync(bootstrap.file);
 assert.equal(crypto.createHash('sha256').update(original).digest('hex'),description.bootstrap.sha256,'The source bootstrap differs from the pinned engine');
 const bytes=source(original),folder=path.join(root,'data/build-cache/sd-bootstrap'),file=path.join(folder,SAVE_NAME+'.bas');fs.mkdirSync(folder,{recursive:true});
 if(!fs.existsSync(file)||!fs.readFileSync(file).equals(bytes))fs.writeFileSync(file,bytes,{mode:0o600});
 return {...bootstrap,file,saveName:SAVE_NAME,quietDisconnect:true};
}
function verifyHello(bytes,addresses){
 assert(Buffer.isBuffer(bytes)&&bytes.length===128,'Missing SD bootstrap greeting');
 assert(bytes.readBigUInt64LE(112)===BigInt(MARKER),'Load and install NS2SD first: the old bootstrap returns to Direct on disconnect');
 assert(bytes.readBigUInt64LE(24)===BigInt(addresses.scratch)&&bytes.readBigUInt64LE(48)===BigInt(addresses.sdk)&&bytes.readBigUInt64LE(120)===3925528160n,'Quiet bootstrap greeting belongs to a different native process');return true;
}
module.exports={MARKER,SAVE_NAME,source,select,verifyHello};
