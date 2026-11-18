'use strict';

const assert = require('node:assert/strict');
const {createHash, randomBytes} = require('node:crypto');
const {ensure} = require('../../../core/console/sd-launcher/install.cjs');
const BASE = '/sd' + require('../../../core/module-storage.cjs').BASE;
const MAX_NRO = 128 * 1024 * 1024;
const NACP_BYTES = 0x4000;
const CHUNK = 256 * 1024;
const switch2Record = Buffer.from('4f554e300200000001000000','hex');

function switch2Program(program) {
  if(program.length<4096)return false;
  const mod=program.readUInt32LE(4);
  if(mod<128||mod+28>4096||program.toString('ascii',mod,mod+4)!=='MOD0')return false;
  const marker=program.indexOf(switch2Record,mod+28);
  return marker>=mod+28&&marker+switch2Record.length<=Math.min(4096,mod+128)&&(marker-mod)%4===0;
}

async function moduleProgram(checked, app) {
  if (app.manifest.format !== 'izlauncher.nro-app') return null;
  assert(app.manifest.abi === 1 && app.manifest.target === 'switch2' &&
    app.manifest.program === 'launcher.nro' && !checked.descriptor.dependencies?.length,
    'Invalid standalone Switch 2 application manifest');
  assert(checked.descriptor.switch.files.includes(app.prefix + app.manifest.program),
    'NRO is absent from the module file list');
  const program = app.files.find(file => file.name === app.manifest.program)?.bytes;
  assert(program, 'Missing NRO in the module');
  await metadata(program.length, (offset, length) => program.subarray(offset, offset + length));
  assert(switch2Program(program), 'Select an NRO built with libounce for Switch 2');
  return program;
}

function validate(header, size) {
  assert(header.length >= 128 && size <= MAX_NRO && header.toString('ascii', 16, 20) === 'NRO0',
    'Invalid or unsupported NRO image');
  const image = header.readUInt32LE(24);
  const mod = header.readUInt32LE(4);
  assert(image >= 4096 && image <= size && image % 4096 === 0 && mod >= 128 &&
    mod <= image - 28 && image + header.readUInt32LE(56) <= 0xffffffff, 'Invalid NRO layout');
  let end = 0;
  for (const at of [32,40,48]) {
    const offset = header.readUInt32LE(at), bytes = header.readUInt32LE(at + 4);
    assert(offset === end && bytes % 4096 === 0 && (at !== 32 || bytes >= 4096), 'Invalid NRO segment');
    end += bytes;
    assert(end <= image, 'Invalid NRO segment size');
  }
  assert.equal(end, image, 'Incomplete NRO image');
  return image;
}

function cleanText(b) {
  const end = b.indexOf(0);
  return b.subarray(0, end < 0 ? b.length : end).toString('utf8').replace(/[\x00-\x1f\x7f]/g, '').trim();
}

async function metadata(size, read) {
  const image = validate(await read(0, Math.min(128, size)), size);
  if (size < image + 56) return {};
  const assets = await read(image, 56);
  if (assets.toString('ascii', 0, 4) !== 'ASET') return {};
  const start = assets.readBigUInt64LE(24), length = assets.readBigUInt64LE(32);
  if (start < 56n || length < BigInt(NACP_BYTES) || start + length > BigInt(size - image)) return {};
  const nacp = await read(image + Number(start), NACP_BYTES);
  const version = cleanText(nacp.subarray(0x3060, 0x3070));
  const names = Array.from({length:16}, (_, i) => cleanText(nacp.subarray(i * 0x300, i * 0x300 + 0x200)));
  return {name: names[0] || names.find(Boolean), version: version || undefined};
}

function idFor(fileName) {
  assert(typeof fileName === 'string' && fileName.length <= 255 &&
    !/[\\/:\x00-\x1f\x7f]/.test(fileName) && /.+\.nro$/i.test(fileName), 'Choose one .nro file');
  const stem = fileName.slice(0,-4).normalize('NFC');
  const id = stem.toLowerCase().replace(/[^a-z0-9-]+/g, '-').replace(/^-+|-+$/g, '');
  if(id && id.length<=47 && /^[a-z0-9][a-z0-9_. -]*$/i.test(stem))return id;
  const suffix = createHash('sha256').update(stem).digest('hex').slice(0,10);
  return (id.slice(0,36).replace(/-+$/, '') || 'nro') + '-' + suffix;
}

function rowId(target) {
  let hash = 14695981039346656037n;
  for (const byte of Buffer.from('sdmc:' + target.slice(3))) hash = BigInt.asUintN(64, (hash ^ BigInt(byte)) * 1099511628211n);
  return 'nro-' + hash.toString(16).padStart(16, '0');
}

function installedPath(app) {
  const match = /^SD:\/iz2s\/modules\/([a-z0-9][a-z0-9-]{0,46})\/launcher\.nro$/.exec(app?.displayPath);
  assert(app?.format === '.nro' && match, 'Select an installed NRO.');
  const target = BASE + '/' + match[1] + '/launcher.nro';
  assert.equal(app.id, rowId(target), 'The installed NRO identity changed.');
  return target;
}

async function remove(io, app) {
  const target = installedPath(app);
  const directory = target.slice(0, target.lastIndexOf('/'));
  const current = await io.openRead(target);
  try {
    assert(current.size === app.bytes, 'The installed NRO changed. Refresh the library and retry.');
  } finally {
    await io.close(current);
  }
  await io.removeModuleDirectory(directory);
  await io.commit();
  return {id: app.id, removed: true, target: 'sd', path: directory, deletedDirectory: true};
}

const missing = error => error.code === 'ENOENT' || error.nativeResult === 0x202;
async function list(io, dir) {
  try { return await io.list(dir); } catch (error) { if (missing(error)) return []; throw error; }
}

async function inspect(io, target, fallback) {
  const file = await io.openRead(target);
  try {
    const {name, version} = await metadata(file.size, (at, n) => io.read(file, at, n));
    return {id:rowId(target), name:name || fallback, version, versionVerified:!!version,
      format:'.nro', bytes:file.size, source:'sd', displayPath:'SD:' + target.slice(3)};
  } finally { await io.close(file); }
}

async function inventory(io) {
  const result = [];
  const entries = (await list(io, BASE)).filter(row => row.type === 'dir' && /^[a-z0-9][a-z0-9-]{0,46}$/.test(row.name));
  for (const {name} of entries.slice(0, 1024)) {
    try { result.push(await inspect(io, BASE + '/' + name + '/launcher.nro', name)); }
    catch (error) { if (!missing(error) && error.code !== 'ERR_ASSERTION') throw error; }
  }
  return result;
}

async function digest(io, name) {
  const file = await io.openRead(name);
  try {
    const hash = createHash('sha256');
    for (let at = 0; at < file.size; at += CHUNK) hash.update(await io.read(file, at, Math.min(CHUNK, file.size - at)));
    return hash.digest('hex');
  } finally { await io.close(file); }
}

async function install(io, fileName, bytes) {
  const id = idFor(fileName);
  await metadata(bytes.length, (offset, length) => bytes.subarray(offset, offset + length));
  const directory = await ensure(io, id);
  const target = directory + '/launcher.nro';
  const checksum = createHash('sha256').update(bytes).digest('hex');
  let previous;
  try { previous = await inspect(io, target, id); } catch (error) { if (!missing(error) && error.code !== 'ERR_ASSERTION') throw error; }
  if (previous && previous.bytes === bytes.length && await digest(io, target) === checksum) {
    return {...previous, reused:true};
  }
  const exists = (await list(io, directory)).some(x => x.name.toLowerCase() === 'launcher.nro');
  const staging = directory + '/.launcher-nro-' + randomBytes(8).toString('hex');
  const stage = staging + '.tmp', backup = staging + '.bak';
  let staged = false;
  try {
    const file = await io.openWrite(stage, bytes.length);
    staged = true;
    try {
      for (let at = 0; at < bytes.length; at += CHUNK) await io.write(file, at, bytes.subarray(at, at + CHUNK));
      await io.flush(file);
    } finally { await io.close(file); }
    assert.equal(await digest(io, stage), checksum, 'NRO readback differs from the upload');
    if (exists) await io.rename(target, backup);
    try { await io.rename(stage, target); }
    catch (error) {
      if (exists) await io.rename(backup, target);
      throw error;
    }
    staged = false;
    await io.commit();
    if (exists) { await io.remove(backup); await io.commit(); }
    return {...await inspect(io, target, id), readbackVerified:true};
  } finally {
    if (staged) { await io.remove(stage); await io.commit(); }
  }
}

module.exports = {MAX_NRO, idFor, rowId, metadata, installedPath, inventory, install, remove, moduleProgram, switch2Program};
