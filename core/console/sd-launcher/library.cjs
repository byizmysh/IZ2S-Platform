'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),{crc32}=require('./package.cjs');
const HEADER=512,ENTRY=96,TILE=131072,MAX=16,CAPACITY=2*1024*1024+4096;
function encode(packages,tiles){
 assert(Array.isArray(packages)&&packages.length<=MAX&&Buffer.isBuffer(tiles)&&tiles.length>=packages.length*TILE);const bytes=Buffer.alloc(HEADER+packages.length*(ENTRY+TILE));bytes.write('IZSDSEL1');bytes.writeUInt32LE(1,8);bytes.writeUInt32LE(bytes.length,12);bytes.writeUInt32LE(packages.length,16);const ids=new Set();
 for(const [i,p]of packages.entries()){assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(p.id)&&!ids.has(p.id));ids.add(p.id);assert(/^[a-f0-9]{64}$/.test(p.sha256));const at=HEADER+i*ENTRY;bytes.write(p.id,at,48);Buffer.from(p.sha256,'hex').copy(bytes,at+48);const tile=tiles.subarray(i*TILE,(i+1)*TILE);bytes.writeUInt32LE(crc32(tile),at+80);tile.copy(bytes,HEADER+packages.length*ENTRY+i*TILE);}
 bytes.writeUInt32LE(crc32(bytes.subarray(HEADER,HEADER+packages.length*ENTRY)),20);return bytes;
}
function decode(bytes){
 assert(Buffer.isBuffer(bytes)&&bytes.length>=HEADER&&bytes.length<=CAPACITY&&bytes.toString('ascii',0,8)==='IZSDSEL1'&&bytes.readUInt32LE(8)===1&&bytes.readUInt32LE(12)===bytes.length&&bytes.subarray(24,HEADER).every(v=>v===0));const count=bytes.readUInt32LE(16);assert(count<=MAX&&bytes.length===HEADER+count*(ENTRY+TILE));assert.equal(crc32(bytes.subarray(HEADER,HEADER+count*ENTRY)),bytes.readUInt32LE(20));const rows=[],ids=new Set();
 for(let i=0;i<count;i++){const at=HEADER+i*ENTRY,id=bytes.subarray(at,at+48).toString().split('\0')[0];assert(/^[a-z0-9][a-z0-9-]{0,46}$/.test(id)&&!ids.has(id)&&bytes.subarray(at+id.length,at+48).every(v=>v===0)&&bytes.subarray(at+84,at+ENTRY).every(v=>v===0));ids.add(id);const tile=bytes.subarray(HEADER+count*ENTRY+i*TILE,HEADER+count*ENTRY+(i+1)*TILE);assert.equal(crc32(tile),bytes.readUInt32LE(at+80));rows.push({id,sha256:bytes.subarray(at+48,at+80).toString('hex')});}return rows;
}
async function create({root,directory,shell,supervisor,repo,clock=()=>performance.now(),raster,intervalMs=3000}){
 let last=0,stamp=null,pending=null,applied=[],error=null,rejected=[];
 async function step(state){
  if(!state.libraryStable)return;
  if(pending){if(state.library_applied!==pending.offer)return;if(state.library_error){error='Native PC catalog rejected: '+state.library_error;pending=null;return;}applied=pending.packages;pending=null;}
  if(state.library_offer!==state.library_applied||clock()-last<intervalMs&&stamp!==null)return;last=clock();
  let next,preview,bytes;try{
   repo=repo||await require('../../packages/repository.cjs').create(root,{shell,source:'current'});raster=raster||require(path.join(root,'shells',shell,'server/raster.cjs')).raster;
   const inventory=await repo.list(),eligible=inventory.packages.filter(p=>p.supported),language=await require('../../../language/node.cjs').forConsole();next=language.id+'|'+eligible.map(p=>p.id+':'+p.sha256).join('|');if(stamp===next)return;
   preview=await repo.previewApps(path.join(directory,'sd-library-preview'),{listing:inventory});const tiles=await raster(preview,directory,'sd-library');preview.packages.forEach((pkg,i)=>tiles.writeUInt32LE(pkg.sd?.compatible?1:2,i*TILE+102516));rejected=preview.rejected;bytes=encode(preview.packages,tiles);
  }catch(e){error='PC library preview: '+e.message;return;}
  // Transport uncertainty is handled by the runtime's existing native-owner
  // retention policy. Local preview errors above cannot close the SD menu.
  const offer=await supervisor.publishLibrary(bytes);pending={offer,packages:preview.packages.map(p=>({id:p.id,name:p.name,version:p.version,file:p.file,sha256:p.sha256,bytes:p.packageBytes}))};stamp=next;error=null;
 }
 return {step,get report(){return {available:applied.map(p=>({...p})),publishing:!!pending,rejected,error};}};
}
module.exports={HEADER,ENTRY,TILE,MAX,CAPACITY,encode,decode,create};
