'use strict';
// Scalar SD paths only. The native helper owns each child handle; no host
// allocation, arbitrary IPC packet or follow-up close is exposed here.
const MAX=32768-64;
function validate(path,offset=0,length=MAX){
 if(typeof path!=='string'||!path.startsWith('/')||Buffer.byteLength(path)>768||/[\x00-\x1f\x7f\\:]/.test(path)||path!=='/'&&path.slice(1).split('/').some(part=>!part||part==='.'||part==='..')||Buffer.from(path).toString('utf8')!==path)throw Error('Invalid console SD path');
 if(!Number.isSafeInteger(offset)||offset<0||!Number.isInteger(length)||length<0||length>MAX)throw Error('Invalid console SD read range');return {path,offset,length};
}
function create(rpc){
 if(rpc?.generationGuarded!==true||typeof rpc.readFile!=='function')throw Error('A native generation-bound SD reader is required');
 return Object.freeze({async read(path,offset,length){validate(path,offset,length);const row=await rpc.readFile(path,offset,length);if(row.result!==0n)throw Object.assign(Error('SD read failed (0x'+row.nativeResult.toString(16)+')'),{code:'IZ2S_SD_READ_FAILED',nativeResult:row.nativeResult});return row;}});
}
module.exports={MAX,validate,create};
