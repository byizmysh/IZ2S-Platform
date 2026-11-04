// Application status only: no addresses, native calls or executable payloads.
const crypto = require('node:crypto');
const BYTES = 4096, SLOT = 2048, MAGIC = 0x31525a49, VERSION = 1;
const hash = bytes => crypto.createHash('sha256').update(bytes.subarray(0,24)).update(bytes.subarray(56)).digest();
const hex = (value, size, name) => {
  if (typeof value !== 'string' || !new RegExp(`^[a-f0-9]{${size*2}}$`).test(value)) throw Error('Invalid ' + name);
  return Buffer.from(value, 'hex');
};
function integer(value, name, max = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > max) throw Error('Invalid ' + name);
  return value;
}
function slot(status, sequence) {
  const out = Buffer.alloc(SLOT);
  out.writeUInt32LE(MAGIC); out.writeUInt16LE(VERSION,4); out.writeUInt16LE(64,6);
  out.writeBigUInt64LE(BigInt(integer(sequence,'sequence')),8); out.writeUInt32LE(256,16);
  hex(status.session,16,'session').copy(out,64); hex(status.process,16,'process').copy(out,80);
  for (const [field, offset] of [['heartbeat',96],['completed',104],['pending',112],['usedBytes',224]])
    out.writeBigUInt64LE(BigInt(integer(status[field] ?? 0,field)),offset);
  const operation = status.operation ?? 0;
  out.writeUInt32LE(integer(operation,'operation',3),120);
  const active = status.active ?? '';
  if (typeof active !== 'string' || !/^[a-zA-Z0-9_.-]{0,63}$/.test(active)) throw Error('Invalid active application');
  out.write(active,124,'ascii');
  out.writeUInt32LE(integer(status.catalogRevision ?? 0,'catalogRevision',0xffffffff),188);
  hex(status.catalogHash ?? '0'.repeat(64),32,'catalogHash').copy(out,192);
  out.writeUInt32LE(integer(status.appCount ?? 0,'appCount',15),232);
  hash(out).copy(out,24);
  return out;
}
function decodeSlot(bytes) {
  if (bytes.readUInt32LE() !== MAGIC || bytes.readUInt16LE(4) !== VERSION || bytes.readUInt16LE(6) !== 64 || bytes.readUInt32LE(16) !== 256 || bytes.readUInt32LE(20) !== 0) throw Error('Invalid passport header');
  if (!crypto.timingSafeEqual(hash(bytes),bytes.subarray(24,56))) throw Error('Invalid passport checksum');
  for (const [start,end] of [[56,64],[236,SLOT]]) if (bytes.subarray(start,end).some(x=>x!==0)) throw Error('Invalid passport reserved bytes');
  const read = offset => integer(Number(bytes.readBigUInt64LE(offset)),'passport counter');
  const text = bytes.subarray(124,188), end = text.indexOf(0);
  if (end < 0 || text.subarray(end).some(x=>x!==0) || !/^[a-zA-Z0-9_.-]*$/.test(text.subarray(0,end).toString('ascii')) || text.subarray(0,end).some(x=>x>127)) throw Error('Invalid passport application');
  const row = {sequence:read(8),session:bytes.subarray(64,80).toString('hex'),process:bytes.subarray(80,96).toString('hex'),
    heartbeat:read(96),completed:read(104),pending:read(112),operation:integer(bytes.readUInt32LE(120),'operation',3),active:text.subarray(0,end).toString('ascii'),
    catalogRevision:bytes.readUInt32LE(188),catalogHash:bytes.subarray(192,224).toString('hex'),usedBytes:read(224),appCount:integer(bytes.readUInt32LE(232),'appCount',15)};
  if (!row.sequence) throw Error('Invalid passport generation');
  if ((row.operation===0 && row.pending!==0) || (row.operation!==0 && row.pending===0) || (row.pending && row.pending<=row.completed)) throw Error('Invalid pending operation');
  return row;
}
function decode(book) {
  if (!Buffer.isBuffer(book) || book.length !== BYTES) throw Error('Passport must be exactly 4096 bytes');
  const valid=[];
  for (let i=0;i<2;i++) {try {valid.push(decodeSlot(book.subarray(i*SLOT,(i+1)*SLOT)));} catch {}}
  if (!valid.length) throw Error('Both passport copies are invalid');
  if (valid.length===2 && (valid[0].session!==valid[1].session || valid[0].process!==valid[1].process)) throw Error('Passport copies identify different processes');
  valid.sort((a,b)=>b.sequence-a.sequence);
  if(valid.length===2 && valid[0].sequence===valid[1].sequence) throw Error('Ambiguous passport generation');
  return valid[0];
}
function update(book,status) {
  if (!Buffer.isBuffer(book) || book.length !== BYTES) throw Error('Passport must be exactly 4096 bytes');
  let old;
  try {old=decode(book);} catch(error) {if(book.some(x=>x!==0)) throw error;}
  if(old && (old.session!==status.session || old.process!==status.process)) throw Error('Cannot replace a live process identity');
  const sequence=(old?.sequence ?? 0)+1;
  const next=slot(status,sequence); decodeSlot(next);
  // Publish into the other slot; a torn copy leaves the preceding valid slot.
  next.copy(book,(sequence%2)*SLOT);
  return decode(book);
}
module.exports={BYTES,SLOT,MAGIC,VERSION,decode,update};
