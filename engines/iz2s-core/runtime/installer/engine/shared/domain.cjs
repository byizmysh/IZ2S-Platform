// CMIF domain/control framing. This module never contacts a console.
const ipc = require('./ipc.cjs');

function dataStart(bytes) {
  const first = bytes.readUInt32LE();
  const second = bytes.readUInt32LE(4);
  let at = 8;
  if (second >>> 31) {
    const special = bytes.readUInt32LE(at);
    at += 4 + (special & 1 ? 8 : 0);
    at += 4 * ((special >>> 1 & 15) + (special >>> 5 & 15));
  }
  at += (first >>> 16 & 15) * 8;
  at += ((first >>> 20 & 15) + (first >>> 24 & 15) + (first >>> 28 & 15)) * 12;
  return (at + 15) & ~15;
}

function checkId(id) {
  if (!Number.isInteger(id) || id <= 0 || id > 0xffffffff) throw Error('Invalid domain object ID');
}

function checkContext(context) {
  if (!Number.isInteger(context) || context < 0 || context > 0xffffffff) throw Error('Invalid context');
}

function control(command, input = Buffer.alloc(0), context = 0) {
  checkContext(context);
  const bytes = ipc.packet(command, input);
  bytes.writeUInt32LE(context ? 7 : 5);
  const at = dataStart(bytes);
  bytes.writeUInt32LE(context ? 1 : 0, at + 4);
  bytes.writeUInt32LE(context, at + 12);
  return bytes;
}

function domain(objectId, command, input = Buffer.alloc(0), options = {}) {
  checkId(objectId);
  const {context = 0, ...buffers} = options;
  checkContext(context);
  const bytes = ipc.packet(command, input, buffers);
  const at = dataStart(bytes);
  if (at + 32 + input.length > bytes.length) throw Error('Domain request is too large');
  bytes.copy(bytes, at + 16, at, at + 16 + input.length);
  bytes.fill(0, at, at + 16);
  bytes[at] = 1;
  bytes.writeUInt16LE(16 + input.length, at + 2);
  bytes.writeUInt32LE(objectId, at + 4);
  bytes.writeUInt32LE(context, at + 12);
  bytes.writeUInt32LE(context ? 1 : 0, at + 20);
  bytes.writeUInt32LE((bytes.readUInt32LE() & 0xffff0000 | (context ? 6 : 4)) >>> 0);
  bytes.writeUInt32LE((bytes.readUInt32LE(4) + 4) >>> 0, 4);
  return bytes;
}

function closeObject(objectId, context = 0) {
  checkId(objectId);
  checkContext(context);
  const bytes = Buffer.alloc(256);
  bytes.writeUInt32LE(context ? 6 : 4);
  bytes.writeUInt32LE(8, 4);
  bytes[16] = 2;
  bytes.writeUInt32LE(objectId, 20);
  bytes.writeUInt32LE(context, 28);
  return bytes;
}

function response(bytes, {domain = false, dataSize = 0} = {}) {
  if (!domain) return {...ipc.response(bytes), objects: []};
  const at = dataStart(bytes);
  const count = bytes.readUInt32LE(at);
  const objectStart = at + 32 + dataSize;
  const result = bytes.readUInt32LE(at + 24);
  if (!Number.isInteger(dataSize) || dataSize < 0 || objectStart > bytes.length ||
      (!result && count > (bytes.length - objectStart) / 4)) throw Error('Invalid domain reply extent');
  const flat = Buffer.from(bytes);
  bytes.copy(flat, at, at + 16);
  const parsed = ipc.response(flat);
  const objects = [];
  if (!parsed.result) {
    for (let i = 0; i < count; i++) objects.push(bytes.readUInt32LE(objectStart + 4 * i));
  }
  return {...parsed, data: parsed.data.subarray(0, dataSize), objects};
}

module.exports = {dataStart, control, domain, closeObject, response};
