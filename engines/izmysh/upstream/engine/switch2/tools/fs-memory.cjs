#!/usr/bin/env node
// Pointer inspection over the retained two-cache FS memory endpoint.
const fs = require('node:fs/promises');
const path = require('node:path');
const {request: requestOnce} = require('./endpoint.cjs');
const rpc = require('../../shared/rpc.cjs');
const crypto = require('node:crypto');

const check = (ok, message) => { if (!ok) throw Error(message); };
const hex = value => '0x' + value.toString(16);

function address(value, length = 1) {
  check(Number.isSafeInteger(value) && value >= 2 ** 32 &&
    Number.isSafeInteger(length) && length > 0 && value + length <= 2 ** 39,
  'Expected a canonical FS address and a positive length');
  return value;
}

function encodeU64(value) {
  check(typeof value === 'bigint' || Number.isSafeInteger(value), 'Use BigInt for large integers');
  const bytes = Buffer.alloc(8);
  bytes.writeBigUInt64LE(BigInt(value));
  return bytes;
}

// Scan captured bytes only. A pointer-shaped value is not proof of a mapping.
function pointerFields(bytes, base, ranges = []) {
  address(base, bytes.length);
  const fields = [];
  for (let offset = (8 - base % 8) % 8; offset + 8 <= bytes.length; offset += 8) {
    const raw = bytes.readBigUInt64LE(offset);
    if (raw < 0x100000000n || raw >= 0x8000000000n) continue;
    const target = Number(raw);
    if (ranges.length && !ranges.some(([start, end]) => start <= target && target < end)) continue;
    fields.push({address: base + offset, offset, target});
  }
  return fields;
}

function references(bytes, base, target) {
  address(base, bytes.length);
  const needle = encodeU64(target), found = [];
  for (let offset = (8 - base % 8) % 8; offset + 8 <= bytes.length; offset += 8) {
    if (bytes.subarray(offset, offset + 8).equals(needle)) found.push(base + offset);
  }
  return found;
}

class Memory {
  constructor(read, write) {
    this.readBytes = read;
    this.writeBytes = write;
  }

  async read(at, length) {
    address(at, length);
    const bytes = await this.readBytes(at, length);
    check(Buffer.isBuffer(bytes) && bytes.length === length, 'Memory read was truncated');
    return bytes;
  }

  async u32(at) { return (await this.read(at, 4)).readUInt32LE(); }
  async u64(at) { return (await this.read(at, 8)).readBigUInt64LE(); }

  async pointer(at) {
    const value = await this.u64(at);
    if (value === 0n) return 0;
    // Do not silently strip PAC or tag bits from a value that is not a pointer.
    check(value >= 0x100000000n && value < 0x8000000000n,
      'Noncanonical pointer at ' + hex(at) + ': ' + hex(value));
    return Number(value);
  }

  // Each offset is added before dereferencing: chase(root, [0x10, 8]).
  async chase(base, offsets) {
    address(base);
    check(Array.isArray(offsets) && offsets.length <= 64, 'A chain may have at most 64 offsets');
    const trace = [];
    let target = base;
    for (const offset of offsets) {
      check(Number.isSafeInteger(offset), 'Pointer offsets must be integers');
      const field = target + offset;
      target = await this.pointer(field);
      trace.push({address: field, offset, target});
      check(target !== 0, 'Null pointer at ' + hex(field));
    }
    return {address: target, trace};
  }

  // Follow an intrusive list until null or an explicitly supplied sentinel.
  async walk(first, {nextOffset = 0, sentinel = 0, maxNodes = 128} = {}) {
    check(Number.isSafeInteger(nextOffset) && Number.isInteger(maxNodes) && maxNodes > 0 && maxNodes <= 4096,
      'Invalid list bounds');
    if (sentinel) address(sentinel);
    const nodes = [], seen = new Set();
    for (let node = first; node && node !== sentinel;) {
      address(node);
      check(!seen.has(node), 'Unexpected list cycle at ' + hex(node));
      check(nodes.length < maxNodes, 'List exceeds its node limit');
      seen.add(node);
      const next = await this.pointer(node + nextOffset);
      nodes.push({address: node, next});
      node = next;
    }
    return nodes;
  }

  async write(at, expected, replacement) {
    check(this.writeBytes && Buffer.isBuffer(expected) && Buffer.isBuffer(replacement) &&
      expected.length === replacement.length, 'Writes require equal-sized expected and replacement bytes');
    check((await this.read(at, expected.length)).equals(expected), 'Expected memory bytes differ at ' + hex(at));
    const result = await this.writeBytes(at, expected, replacement);
    check((await this.read(at, replacement.length)).equals(replacement), 'Write readback differs at ' + hex(at));
    return result;
  }

  async writeU64(at, expected, replacement) {
    return this.write(at, encodeU64(expected), encodeU64(replacement));
  }
}

async function connect(reportFile) {
  if (!reportFile) {
    const current = JSON.parse(await fs.readFile(path.join(__dirname, '../current-acquisition.json')));
    reportFile = current.report;
  }
  reportFile = path.resolve(reportFile);
  const report = JSON.parse(await fs.readFile(reportFile));
  const boot = {...rpc.a, ...await rpc.request({command: 'status'})};
  check(boot.ready && boot.platform === 2 && boot.session === report.session && report.retainedMemory?.ready &&
    report.retainedMemory.zeroCountVerified && !report.retainedMemory.pending,
  'A ready retained endpoint on the current boot is required');
  const socketPath = report.memoryControl.socketPath;
  let queue = Promise.resolve();
  const request = command => {
    // A full CodeFS file can require hundreds of bounded console reads inside
    // one serialized controller request. Keep its host deadline above that
    // expected transfer time without changing ordinary request deadlines.
    const timeoutMs = command.command === 'root-native-file-capture' ? 15 * 60 * 1000 : 60000;
    const result = queue.then(() => requestOnce(socketPath, command, timeoutMs));
    queue = result.catch(() => {});
    return result;
  };
  const info = await request({command: 'retained-info'});
  check(info.ready && info.steadyStateReliable && info.requiresCorruption === false &&
    info.session === report.session && info.report === reportFile && info.corruptionCount === report.cases.length,
  'The endpoint does not confirm the retained two-cache interface');

  let memory;
  const read = async (at, length) => {
    check(length <= 0x600000, 'One read is limited to 6 MiB');
    const result = await request({command: 'retained-read', address: at, length});
    check(result.address === at && result.length === length && result.requiresCorruption === false &&
      result.corruptionCount === info.corruptionCount, 'Memory response does not match its request');
    const bytes = await fs.readFile(result.outputFile);
    check(bytes.length === length && crypto.createHash('sha256').update(bytes).digest('hex') === result.sha256,
      'Saved capture length or hash differs');
    memory.captures.push(result);
    return bytes;
  };
  const write = async (at, expected, replacement) => {
    check(replacement.length <= 0x10000, 'One write is limited to 64 KiB');
    const result = await request({command: 'retained-write', address: at,
      expectedHex: expected.toString('hex'), hex: replacement.toString('hex')});
    check(result.verified && result.requiresCorruption === false && result.corruptionCount === info.corruptionCount,
      'The endpoint did not verify the retained write');
    return result;
  };
  memory = new Memory(read, write);
  Object.assign(memory, {info, report, reportFile, request, captures: []});
  return memory;
}

module.exports = {Memory, connect, pointerFields, references, encodeU64};
