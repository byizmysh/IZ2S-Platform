// Read-only filesystem methods using this session's owned buffers.
const crypto = require('node:crypto');
const {live, u32, u64, check} = require("./session.cjs");
const domain = require("./domain.cjs");
const hashCode = require("./hash-code.json");
const {packet} = require("../../shared/ipc.cjs");

function pathPacket(command, input, address) {
  check([7, 8, 9, 11, 12, 14].includes(command), 'Only read-only path methods are supported');
  const a = BigInt(address);
  check(a > 0n && a < 1n << 39n, 'Invalid path buffer address');
  const bytes = packet(command, input);
  // One X descriptor exactly fills the existing eight-byte header padding.
  bytes.writeUInt32LE(4 | 1 << 16);
  bytes.writeUInt32LE(Number(0x301n << 16n | ((a >> 36n) & 7n) << 6n |
    ((a >> 32n) & 15n) << 12n), 8);
  bytes.writeUInt32LE(Number(a & 0xffffffffn), 12);
  return bytes;
}

function domainPathPacket(objectId, command, input, address, context = 0) {
  const descriptor = pathPacket(command, input, address).subarray(8, 16);
  const bytes = domain.wire.domain(objectId, command, input, {context});
  bytes.writeUInt32LE((context ? 6 : 4) | 1 << 16);
  descriptor.copy(bytes, 8);
  return bytes;
}

async function pathMethod(s, handle, command, filename, input = Buffer.alloc(0)) {
  const text = Buffer.from(filename, 'utf8');
  check(filename.startsWith('/') && !filename.includes('\0') && text.length < 0x301,
    'Invalid filesystem path');
  const buffer = Buffer.alloc(0x304);
  text.copy(buffer);
  await s.write(s.base + 0x2000, buffer);
  return domain.send(s, 'Filesystem path ' + command + ' ' + filename, handle,
    pathPacket(command, input, s.base + 0x2000));
}

async function openOwn(s, owner) {
  const reply = await domain.send(s, 'OpenOwnDataFileSystem', owner.handle,
    domain.wire.domain(owner.root, 2), {domain: true});
  check(!reply.result && reply.objects.length === 1, 'Owned data filesystem did not open');
  return reply.objects[0];
}

async function openFile(s, handle, filename) {
  const reply = await pathMethod(s, handle, 8, filename, u32(1));
  return reply.result ? {result: reply.result, handle: null} :
    {result: 0, handle: domain.oneHandle(reply)};
}

async function openDomainFile(s, owner, fileSystemId, filename) {
  const text = Buffer.from(filename, 'utf8');
  check(filename.startsWith('/') && !filename.includes('\0') && text.length < 0x301,
    'Invalid filesystem path');
  const buffer = Buffer.alloc(0x304);
  text.copy(buffer);
  await s.write(s.base + 0x9000, buffer);
  const reply = await domain.send(s, 'Open owned domain file', owner.handle,
    domainPathPacket(fileSystemId, 8, u32(1), s.base + 0x9000), {domain: true});
  check(!reply.result && reply.objects.length === 1, 'Owned domain file did not open');
  return reply.objects[0];
}

async function readFile(s, handle, offset, size, buffer = s.base + 0x6000) {
  check(!s.threads.size && Number.isSafeInteger(offset) && offset >= 0 &&
    Number.isInteger(size) && size > 0 && size <= (s.bulkDownloads ? 0x400000 : 0x40000) &&
    buffer >= s.base + 0x3000 && buffer + size <= s.base + s.bytes,
    'File read exceeds idle owned memory');
  const reply = await s.ipc('ReadFile', handle, 0,
    Buffer.concat([u64(0), u64(offset), u64(size)]),
    {receive: [{address: buffer, size, mode: 1}]});
  check(!reply.result, 'File read failed: ' + reply.result);
  const count = Number(reply.data.readBigUInt64LE());
  check(count >= 0 && count <= size, 'Invalid returned file byte count');
  return verifiedBytes(s, buffer, count, {handle, offset});
}

async function readStorage(s, handle, offset, size, buffer = s.base + 0x6000) {
  check(!s.threads.size && Number.isSafeInteger(offset) && offset >= 0 &&
    Number.isInteger(size) && size > 0 && size <= (s.bulkDownloads ? 0x400000 : 0x40000) &&
    buffer >= s.base + 0x3000 && buffer + size <= s.base + s.bytes,
    'Storage read exceeds idle owned memory');
  const reply = await s.ipc('ReadStorage', handle, 0, Buffer.concat([u64(offset), u64(size)]),
    {receive: [{address: buffer, size, mode: 1}]});
  check(!reply.result, 'Storage read failed: ' + reply.result);
  return verifiedBytes(s, buffer, size, {handle, offset});
}

async function nativeHash(s, buffer, count) {
  check(Number.isSafeInteger(buffer) && Number.isInteger(count) && count >= 0 &&
    buffer >= s.base + 0x3000 && buffer + count <= s.base + s.bytes,
    'Hash input exceeds the owned allocation');
  if (!s.hashCodeVerified) {
    await s.verifyCode(hashCode, 'Native SHA-256');
    s.hashCodeVerified = true;
  }
  await s.rpc.call4(live.sdk + hashCode.offset, s.base + 0x1800, 32, buffer, count);
  return (await s.read(s.base + 0x1800, 32)).toString('hex');
}

async function verifiedBytes(s, buffer, count, origin) {
  const expected = await nativeHash(s, buffer, count);
  const bytes = count ? (s.bulkDownloads ? await require("./bulk-transfer.cjs").read(s, buffer, count) :
    await s.read(buffer, count)) : Buffer.alloc(0);
  const actual = crypto.createHash('sha256').update(bytes).digest('hex');
  check(actual === expected, 'File bytes differ from the console-computed hash');
  await s.record({operation: 'fileReadVerified', ...origin, count, sha256: actual});
  return bytes;
}

async function fileSize(s, handle) {
  const reply = await s.ipc('FileGetSize', handle, 4);
  check(!reply.result, 'File size failed');
  return reply.data.readBigUInt64LE();
}

async function listDirectory(s, handle, filename = '/', maxEntries = 128) {
  const opened = await pathMethod(s, handle, 9, filename, u32(3));
  const directory = domain.oneHandle(opened);
  const entries = [];
  try {
    while (entries.length < maxEntries) {
      const reply = await s.ipc('ReadDirectory', directory, 0, Buffer.alloc(0),
        {receive: [{address: s.base + 0x6000, size: 4 * 0x310, mode: 0}]});
      check(!reply.result, 'Directory read failed');
      const count = Number(reply.data.readBigUInt64LE());
      check(count >= 0 && count <= 4, 'Invalid directory entry count');
      if (!count) return entries;
      const bytes = await s.read(s.base + 0x6000, count * 0x310);
      for (let i = 0; i < count; i++) {
        const entry = bytes.subarray(i * 0x310, (i + 1) * 0x310);
        const name = entry.subarray(0, 0x301).toString('utf8').split('\0')[0];
        check(name && !name.includes('/') && !name.includes('\\') && !['.', '..'].includes(name),
          'Unexpected directory entry name');
        entries.push({name, type: entry[0x304], size: entry.readBigUInt64LE(0x308).toString()});
      }
    }
    throw Error('Directory exceeds the ' + maxEntries + '-entry probe limit');
  } finally {await s.close(directory);}
}

module.exports = {pathPacket, domainPathPacket, pathMethod, openOwn, openFile, openDomainFile,
  readFile, readStorage, fileSize, listRoot: listDirectory, listDirectory, nativeHash, verifiedBytes};
