// Streaming ZIP64, stored entries. Works without an archive library or temp data.
const crcTable = Uint32Array.from({length: 256}, (_, n) => {
  for (let i = 0; i < 8; i++) n = n & 1 ? 0xedb88320 ^ n >>> 1 : n >>> 1;
  return n >>> 0;
});
function crcUpdate(crc, bytes) {for (const byte of bytes) crc = crcTable[(crc ^ byte) & 255] ^ crc >>> 8; return crc >>> 0;}
function checkedName(name) {
  if (typeof name !== 'string' || !name || /[\x00-\x1f\x7f\\]/.test(name) || name.startsWith('/') ||
      name.split('/').some(part => part === '..' || part === '.')) throw Error('Invalid ZIP entry path');
  const bytes = Buffer.from(name); if (bytes.length > 65535) throw Error('ZIP entry name is too long'); return bytes;
}
async function streamZip(entries, write, {signal} = {}) {
  let offset = 0n; const central = [];
  const send = async bytes => {if (signal?.aborted) throw Error('Download cancelled'); await write(bytes); offset += BigInt(bytes.length);};
  for await (const entry of entries) {
    const name = checkedName(entry.name), localOffset = offset;
    const header = Buffer.alloc(30), extra = Buffer.alloc(20);
    header.writeUInt32LE(0x04034b50); header.writeUInt16LE(45, 4); header.writeUInt16LE(0x0808, 6);
    header.writeUInt16LE(0x21, 12); header.writeUInt32LE(0xffffffff, 18); header.writeUInt32LE(0xffffffff, 22);
    header.writeUInt16LE(name.length, 26); header.writeUInt16LE(extra.length, 28);
    extra.writeUInt16LE(1); extra.writeUInt16LE(16, 2);
    await send(header); await send(name); await send(extra);
    let size = 0n, crc = 0xffffffff;
    if (!entry.directory) for await (const chunk of entry.data()) {
      const bytes = Buffer.from(chunk); crc = crcUpdate(crc, bytes); size += BigInt(bytes.length); await send(bytes);
    }
    crc = (crc ^ 0xffffffff) >>> 0;
    const descriptor = Buffer.alloc(24); descriptor.writeUInt32LE(0x08074b50); descriptor.writeUInt32LE(crc, 4);
    descriptor.writeBigUInt64LE(size, 8); descriptor.writeBigUInt64LE(size, 16); await send(descriptor);
    central.push({name, localOffset, size, crc, directory: entry.directory});
  }
  const centralStart = offset;
  for (const entry of central) {
    const header = Buffer.alloc(46), extra = Buffer.alloc(28);
    header.writeUInt32LE(0x02014b50); header.writeUInt16LE(3 << 8 | 45, 4); header.writeUInt16LE(45, 6);
    header.writeUInt16LE(0x0808, 8); header.writeUInt16LE(0x21, 14); header.writeUInt32LE(entry.crc, 16);
    header.writeUInt32LE(0xffffffff, 20); header.writeUInt32LE(0xffffffff, 24);
    header.writeUInt16LE(entry.name.length, 28); header.writeUInt16LE(extra.length, 30);
    header.writeUInt32LE(((entry.directory ? 0o40755 : 0o100644) << 16 | (entry.directory ? 16 : 0)) >>> 0, 38);
    header.writeUInt32LE(0xffffffff, 42);
    extra.writeUInt16LE(1); extra.writeUInt16LE(24, 2); extra.writeBigUInt64LE(entry.size, 4);
    extra.writeBigUInt64LE(entry.size, 12); extra.writeBigUInt64LE(entry.localOffset, 20);
    await send(header); await send(entry.name); await send(extra);
  }
  const centralBytes = offset - centralStart, zip64Offset = offset;
  const end64 = Buffer.alloc(56); end64.writeUInt32LE(0x06064b50); end64.writeBigUInt64LE(44n, 4);
  end64.writeUInt16LE(45, 12); end64.writeUInt16LE(45, 14);
  end64.writeBigUInt64LE(BigInt(central.length), 24); end64.writeBigUInt64LE(BigInt(central.length), 32);
  end64.writeBigUInt64LE(centralBytes, 40); end64.writeBigUInt64LE(centralStart, 48); await send(end64);
  const locator = Buffer.alloc(20); locator.writeUInt32LE(0x07064b50); locator.writeBigUInt64LE(zip64Offset, 8); locator.writeUInt32LE(1, 16); await send(locator);
  const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50); end.writeUInt16LE(0xffff, 8); end.writeUInt16LE(0xffff, 10);
  end.writeUInt32LE(0xffffffff, 12); end.writeUInt32LE(0xffffffff, 16); await send(end);
}
module.exports = {streamZip, crcUpdate};
