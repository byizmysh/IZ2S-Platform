'use strict';
// Immutable app/engine bytes in one owned RAM allocation. User files stay on SD.
const assert=require('node:assert/strict'),crypto=require('node:crypto');
const align=n=>Math.ceil(n/4096)*4096,sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function prepare({bytes,assets=[]}){
 const app=require('./package.cjs').decode(bytes);assert(app.native&&app.nativeAbi===4,'RAM launch requires the cooperative native ABI');
 require('./assets.cjs').verifyBinding(app,assets);
 const root='/IZ2S/Homebrew/Modules/'+app.id+'/',rows=[{path:root+'launcher.izapp',data:Buffer.from(bytes)}];
 for(const a of assets){assert(Buffer.isBuffer(a.data)&&a.data.length===a.bytes&&sha(a.data)===a.sha256,'RAM companion checksum differs');rows.push({path:root+a.path,data:Buffer.from(a.data)});}
 let at=align(rows.length*208);for(const row of rows){assert(Buffer.byteLength(row.path)<192);row.offset=at;at=align(at+row.data.length);}assert(at<=256*1024*1024,'RAM launch image exceeds its allocation budget');
 return {id:app.id,count:rows.length,bytes:at,pack(base){assert(Number.isSafeInteger(base)&&base>0&&base%4096===0&&base+at<2**39);const result=Buffer.alloc(at);rows.forEach((row,i)=>{const slot=i*208;result.write(row.path,slot,192);result.writeBigUInt64LE(BigInt(base+row.offset),slot+192);result.writeBigUInt64LE(BigInt(row.data.length),slot+200);row.data.copy(result,row.offset);});return result;}};
}
module.exports={prepare};
