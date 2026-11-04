// Non-domain CMIF packets. Addresses and handles are supplied by the live session.
function descriptor({address, size, mode = 0}) {
  const a = BigInt(address), n = BigInt(size);
  if (a < 0n || a >= 1n << 39n || n < 0n || n >= 1n << 36n || ![0, 1, 3].includes(mode)) {
    throw Error('Invalid map-alias descriptor');
  }
  const bytes = Buffer.alloc(12);
  bytes.writeUInt32LE(Number(n & 0xffffffffn));
  bytes.writeUInt32LE(Number(a & 0xffffffffn), 4);
  bytes.writeUInt32LE(Number(BigInt(mode) | (a >> 36n) << 2n |
    (n >> 32n) << 24n | ((a >> 32n) & 15n) << 28n), 8);
  return bytes;
}

function packet(command, input = Buffer.alloc(0), {pid = false, copies = [], send = [], receive = []} = {}) {
  if ([copies.length, send.length, receive.length].some(n => n > 15)) throw Error('Too many descriptors');
  const bytes = Buffer.alloc(256);
  const special = pid || copies.length > 0;
  bytes.writeUInt32LE((4 | send.length << 20 | receive.length << 24) >>> 0);
  bytes.writeUInt32LE((Math.ceil((32 + input.length) / 4) | (special ? 0x80000000 : 0)) >>> 0, 4);
  let at = 8;
  if (special) {
    bytes.writeUInt32LE((pid ? 1 : 0) | copies.length << 1, at);
    at += 4;
    if (pid) at += 8;
    for (const handle of copies) {bytes.writeUInt32LE(handle >>> 0, at); at += 4;}
  }
  for (const buffer of [...send, ...receive]) {descriptor(buffer).copy(bytes, at); at += 12;}
  at = (at + 15) & ~15;
  if (at + 16 + input.length > bytes.length) throw Error('CMIF input exceeds packet capacity');
  bytes.write('SFCI', at);
  bytes.writeUInt32LE(command, at + 8);
  input.copy(bytes, at + 16);
  return bytes;
}

function response(bytes) {
  const first = bytes.readUInt32LE(), second = bytes.readUInt32LE(4);
  let at = 8;
  const copies = [], moves = [];
  if (second >>> 31) {
    const special = bytes.readUInt32LE(at); at += 4;
    if (special & 1) at += 8;
    for (let i = 0; i < (special >>> 1 & 15); i++, at += 4) copies.push(bytes.readUInt32LE(at));
    for (let i = 0; i < (special >>> 5 & 15); i++, at += 4) moves.push(bytes.readUInt32LE(at));
  }
  at += (first >>> 16 & 15) * 8;
  at += ((first >>> 20 & 15) + (first >>> 24 & 15) + (first >>> 28 & 15)) * 12;
  at = (at + 15) & ~15;
  if (bytes.toString('ascii', at, at + 4) !== 'SFCO') throw Error('Missing SFCO reply');
  const result = bytes.readUInt32LE(at + 8);
  return {result, canonicalResult: result & 0x3fffff, copies, moves, data: bytes.subarray(at + 16)};
}

module.exports = {descriptor, packet, response};
