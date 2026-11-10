'use strict';
// Only skip zero suffixes after clearing the entire owned allocation. A later
// catalog update must overwrite old pixels, including its zero suffix.
function prefix(bytes){let end=bytes.length;while(end&&bytes[end-1]===0)end--;return bytes.subarray(0,end);}
async function initialize(rpc,address,size,segments){
  if(!Number.isSafeInteger(size)||size<=0||size>8*1024*1024)throw Error('Invalid Civ7 storage size');
  for(const {offset,bytes}of segments)if(!Buffer.isBuffer(bytes)||!Number.isSafeInteger(offset)||offset<0||offset+bytes.length>size)throw Error('Civ7 storage segment exceeds allocation');
  await rpc.write(address,Buffer.alloc(size));let uploaded=0,logical=0;
  for(const {offset,bytes}of segments){logical+=bytes.length;const data=prefix(bytes);if(data.length){await rpc.write(address+offset,data);uploaded+=data.length;}}
  return {cleared:size,logical,uploaded,skippedZeroBytes:logical-uploaded};
}
module.exports={initialize,prefix};
