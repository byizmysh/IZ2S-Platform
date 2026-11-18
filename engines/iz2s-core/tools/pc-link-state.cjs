'use strict';
const assert = require('node:assert/strict');
const {createHash, randomUUID} = require('node:crypto');
const fs = require('node:fs/promises');

const METADATA_LIMIT = 4096;

async function readState(file, packageLimit) {
  let handle;
  try { handle=await fs.open(file,'r'); }
  catch (error) { if(error.code==='ENOENT')return {}; throw error; }
  try {
    const {size}=await handle.stat();
    assert(size>4&&size<=4+METADATA_LIMIT+packageLimit,'Invalid PC link state size.');
    const prefix=Buffer.alloc(4);
    assert.equal((await handle.read(prefix,0,4,0)).bytesRead,4,'Incomplete PC link state.');
    const length=prefix.readUInt32BE();
    assert(length>0&&length<=METADATA_LIMIT&&length<=size-4,'Invalid PC link state header.');
    const metadata=Buffer.alloc(length);
    assert.equal((await handle.read(metadata,0,length,4)).bytesRead,length,'Incomplete PC link state header.');
    const {queued,delivered,imported}=JSON.parse(metadata);
    const packageSize=size-4-length;
    if(!queued){
      assert.equal(packageSize,0,'Unexpected PC link package.');
      return {delivered,imported};
    }
    assert(packageSize>0&&packageSize<=packageLimit&&typeof queued.id==='string'&&
      typeof queued.name==='string'&&/^[a-f0-9]{64}$/.test(queued.sha256),'Invalid saved PC link package.');
    const bytes=Buffer.allocUnsafe(packageSize);
    let offset=0;
    while(offset<packageSize){
      const {bytesRead}=await handle.read(bytes,offset,packageSize-offset,4+length+offset);
      assert(bytesRead>0,'Incomplete saved PC link package.');
      offset+=bytesRead;
    }
    const digest=createHash('sha256').update(bytes).digest();
    assert.equal(digest.toString('hex'),queued.sha256,'Saved PC link package checksum differs.');
    return {queued:{id:queued.id,name:queued.name,packed:queued.packed===true,executable:queued.executable,version:queued.version,sourceSha256:queued.sourceSha256,bytes,digest},delivered,imported};
  } finally { await handle.close(); }
}

async function writeState(file, {queued, delivered, imported}, canPublish=()=>true) {
  const metadata=Buffer.from(JSON.stringify({
    queued:queued?{id:queued.id,name:queued.name,packed:queued.packed,executable:queued.executable,version:queued.version,sourceSha256:queued.sourceSha256,sha256:queued.digest.toString('hex')}:null,
    delivered,imported,
  }));
  assert(metadata.length<=METADATA_LIMIT,'PC link metadata is too large.');
  const prefix=Buffer.alloc(4);
  prefix.writeUInt32BE(metadata.length);
  const temporary=file+'.'+randomUUID()+'.tmp';
  try {
    const handle=await fs.open(temporary,'wx',0o600);
    try {
      await handle.writeFile(Buffer.concat([prefix,metadata]));
      if(queued)await handle.writeFile(queued.bytes);
      await handle.sync();
    } finally { await handle.close(); }
    assert(canPublish(),'The storage session changed before publication.');
    await fs.rename(temporary,file);
  } finally { await fs.rm(temporary,{force:true}); }
}

module.exports={readState,writeState};
