'use strict';

const fs = require('node:fs/promises');
const { constants } = require('node:fs');
const path = require('node:path');

const NRO_HEADER_BYTES = 0x80;
const ASET_HEADER_BYTES = 0x38;
const NACP_BYTES = 0x4000;
const NACP_LANGUAGE_BYTES = 0x300;
const MAX_ICON_BYTES = 1024 * 1024;

async function read(file, position, length) {
  const bytes = Buffer.alloc(length);
  let offset = 0;
  while (offset < length) {
    const result = await file.read(bytes, offset, length - offset, position + offset);
    if (!result.bytesRead) throw Error('NRO changed or ended during metadata read');
    offset += result.bytesRead;
  }
  return bytes;
}

function assetRange(header, field, available, limit) {
  const offset = header.readBigUInt64LE(field);
  const length = header.readBigUInt64LE(field + 8);
  if (offset < BigInt(ASET_HEADER_BYTES) || length === 0n ||
      length > BigInt(limit) || offset + length > BigInt(available)) return null;
  return [Number(offset), Number(length)];
}

function text(bytes, maxBytes) {
  const terminator = bytes.indexOf(0);
  const raw = terminator < 0 ? bytes : bytes.subarray(0, terminator);
  const clean = raw.toString('utf8').replace(/[\p{Cc}\p{Cf}]+/gu, ' ').trim();
  let result = '';
  let size = 0;
  for (const character of clean) {
    size += Buffer.byteLength(character);
    if (size > maxBytes) break;
    result += character;
  }
  return result.trimEnd();
}

function readNacp(bytes) {
  const version = text(bytes.subarray(0x3060, 0x3070), 16);
  // US English is the first entry. Fall back to the first available title.
  for (let language = 0; language < 16; language++) {
    const offset = language * NACP_LANGUAGE_BYTES;
    const name = text(bytes.subarray(offset, offset + 0x200), 63);
    if (name) {
      return {
        name,
        author: text(bytes.subarray(offset + 0x200, offset + NACP_LANGUAGE_BYTES), 96),
        version,
      };
    }
  }
  return {version};
}

async function readMetadata(filename) {
  const flags = constants.O_RDONLY | (constants.O_NONBLOCK || 0) | (constants.O_NOFOLLOW || 0);
  const file = await fs.open(filename, flags);
  try {
    const stat = await file.stat();
    const {size, mtimeMs, ctimeMs} = stat;
    if (!stat.isFile() || size < NRO_HEADER_BYTES || !Number.isSafeInteger(size))
      throw Error('Invalid NRO file');
    const header = await read(file, 0, NRO_HEADER_BYTES);
    const imageBytes = header.readUInt32LE(0x18);
    if (header.toString('ascii', 0x10, 0x14) !== 'NRO0' ||
        header.readUInt32LE(0x14) !== 0 || imageBytes < NRO_HEADER_BYTES || imageBytes > size)
      throw Error('Invalid NRO header');

    const fallback = text(Buffer.from(path.basename(filename).replace(/\.nro$/i, '')), 63) || 'Homebrew';
    let metadata = {}, icon = null;
    if (size - imageBytes >= ASET_HEADER_BYTES) {
      const assets = await read(file, imageBytes, ASET_HEADER_BYTES);
      if (assets.toString('ascii', 0, 4) === 'ASET' && assets.readUInt32LE(4) === 0) {
        const nacp = assetRange(assets, 0x18, size - imageBytes, NACP_BYTES);
        const artwork = assetRange(assets, 0x08, size - imageBytes, MAX_ICON_BYTES);
        if (nacp && nacp[1] === NACP_BYTES)
          metadata = readNacp(await read(file, imageBytes + nacp[0], nacp[1]));
        if (artwork) icon = await read(file, imageBytes + artwork[0], artwork[1]);
      }
    }
    const after = await file.stat();
    if (after.size !== size || after.mtimeMs !== mtimeMs || after.ctimeMs !== ctimeMs)
      throw Error('NRO changed during metadata read');
    return {name: metadata.name || fallback, author: metadata.author || '', version: metadata.version || '', icon};
  } finally {
    await file.close();
  }
}

module.exports = {readMetadata};
