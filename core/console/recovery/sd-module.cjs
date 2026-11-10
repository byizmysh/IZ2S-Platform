'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const ID='iz-reconnect',home=path.join(__dirname,'sd-app'),digest=b=>crypto.createHash('sha256').update(b).digest('hex');
async function load(){
 const report=JSON.parse(await fs.readFile(path.join(home,'manifest.json'))),bytes=await fs.readFile(path.join(home,'launcher.izapp'));
 assert.equal(digest(bytes),report.sha256,'Rebuild the SD recovery application');for(const [file,hash]of Object.entries(report.sources))assert.equal(digest(await fs.readFile(path.join(home,file))),hash,'Rebuild the changed SD recovery application');
 assert.equal(digest(await fs.readFile(path.join(__dirname,'reconnect-app.c'))),report.sourceSha256);assert.equal(digest(await fs.readFile(path.join(__dirname,'../sd-launcher/native/native-app.h'))),report.nativeAbiSha256);
 assert.equal(require('../sd-launcher/package.cjs').decode(bytes).id,ID);return {report,bytes};
}
async function install(storage){
 const {report,bytes}=await load(),installer=require('../sd-launcher/install.cjs');await installer.ensure(storage,ID);const destination=installer.paths(ID).packagePath,rows=await storage.list(installer.paths(ID).virtualPath);
 if(rows.some(row=>row.name==='launcher.izapp'&&row.type==='file')){
  const file=await storage.openRead(destination);let hash;
  try{if(file.size===bytes.length){const h=crypto.createHash('sha256');for(let offset=0;offset<file.size;offset+=65536)h.update(await storage.read(file,offset,Math.min(65536,file.size-offset)));hash=h.digest('hex');}}
  finally{if(!storage.uncertain)await storage.close(file);}
  if(hash===report.sha256)return {id:ID,path:destination,readbackVerified:true,alreadyInstalled:true};
 }else if((await installer.inventory(storage)).length>=15)return {id:ID,installed:false,reason:'SD menu is full'};
 return installer.installBytes(storage,bytes);
}
async function ensureInstalled({directory}){
 const a=await require('../../filesystem/ouncecake/engine/session.cjs').attachGame(path.join(directory,'fs-rpc'));let storage;
 try{storage=await require('../sd-launcher/storage.cjs').create(a);const result=await install(storage);await require('../../session/ledger.cjs').saveJson(path.join(directory,'sd-recovery-install.json'),result);return result;}
 finally{if(storage&&!storage.uncertain)await a.close();}
}
module.exports={ID,load,install,ensureInstalled};
