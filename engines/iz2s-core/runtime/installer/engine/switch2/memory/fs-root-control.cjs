// Ordinary IPC on a dedicated owned root, serialized by the memory controller.
const path = require('node:path');
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const {check, u64} = require("./session.cjs");
const source = require('node:fs').readFileSync(__filename);
const sourceHash = crypto.createHash('sha256').update(source).digest('hex');

function pathRequest(command, input, address, length) {
  const {packet} = require("../../shared/ipc.cjs");
  const bytes = packet(command, input), pointer = BigInt(address);
  bytes.writeUInt32LE(4 | (1 << 16));
  bytes.writeUInt32LE(Number((BigInt(length) << 16n) | ((pointer >> 36n) & 7n) << 6n |
    ((pointer >> 32n) & 15n) << 12n), 8);
  bytes.writeUInt32LE(Number(pointer & 0xffffffffn), 12);
  return bytes;
}

async function dispatch(context, request) {
  if (request.command.startsWith('root-native-'))
    return require("../ro/control.cjs").dispatch(context, request);
  const {s, live, report, save, label} = context;
  const domain = require(path.join(__dirname, '../app/domain.cjs'));
  const state = report.ownedRoot ||= {session: live.session, clones: [], storages: [], probes: []};
  if (!state.implementations?.some(item => item.sha256 === sourceHash)) {
    const sourceFile = path.join(__dirname, label + '-root-control-' + sourceHash.slice(0, 12) + '.cjs');
    await fs.writeFile(sourceFile, source, {mode: 0o600});
    (state.implementations ||= state.implementation ? [state.implementation] : []).push({sourceFile, sha256: sourceHash});
    state.implementation = {sourceFile, sha256: sourceHash};
    await save();
  }
  if (request.command === 'root-info') return state;
  check(await s.ready(), 'The native bridge is not ready');

  if (request.command === 'root-capture-syscalls') {
    const offset = 0x158000;
    const bytes = await s.read(live.sdk + offset, 4096);
    const outputFile = path.join(__dirname, label + '-current-sdk-syscalls.bin');
    await fs.writeFile(outputFile, bytes, {mode: 0o600});
    state.syscalls = {offset, outputFile, sha256: crypto.createHash('sha256').update(bytes).digest('hex')};
    await save();
    return state.syscalls;
  }

  if (request.command === 'root-open') {
    if (state.clones.length) return state;
    const handle = await s.openService('fsp-srv');
    state.clones.push(handle);
    state.initialized = false;
    state.created = new Date().toISOString();
    await save();
    if (request.initialize === false) return state;
  }

  check(state.clones.length && state.clones.every(handle => s.handles.has(handle)),
    'No dedicated root is retained');
  const root = state.clones[0];
  if (request.command === 'root-initialize' || request.command === 'root-open') {
    check(!state.initialized, 'The dedicated root is already initialized');
    const initialized = await s.ipc('Initialize dedicated PID-test root', root, 1, u64(0), {pid: true});
    check(!initialized.result, 'Dedicated root initialization failed');
    state.initialized = true;
    state.initializedAt = new Date().toISOString();
    await save();
    return state;
  }
  if (request.command === 'root-clones') {
    const count = Number(request.count);
    check(Number.isInteger(count) && count >= 1 && count <= 16, 'Root clone count must be 1..16');
    const before = state.clones.length;
    while (state.clones.length < count) {
      const reply = await domain.send(s, 'Clone dedicated PID-test root', root, domain.wire.control(2));
      state.clones.push(domain.oneHandle(reply));
      await save();
    }
    while (state.clones.length > count) {
      await s.close(state.clones.at(-1));
      state.clones.pop();
      await save();
    }
    const change = {at: new Date().toISOString(), before, after: count};
    (state.referenceChanges ||= []).push(change);
    await save();
    return {...change, handles: state.clones};
  }

  if (request.command === 'root-probe-bis') {
    check(state.initialized, 'Initialize the dedicated root before probing BIS');
    const id = Number(request.id);
    check(Number.isInteger(id) && id >= 0 && id <= 255, 'BIS identifier must be 0..255');
    const existing = state.storages.find(item => item.id === id && s.handles.has(item.handle));
    if (existing) {
      const size = (await domain.storageSize(s, existing.handle)).toString();
      return {at: new Date().toISOString(), id, result: 0, opened: true, reused: true,
        handle: existing.handle, size};
    }
    const input = Buffer.alloc(4); input.writeUInt32LE(id);
    const reply = await s.ipc('Dedicated root OpenBisStorage ' + id, root, 12, input);
    const probe = {at: new Date().toISOString(), id, result: reply.canonicalResult, opened: !reply.result};
    state.probes.push(probe);
    await save();
    if (!reply.result) {
      probe.handle = domain.oneHandle(reply);
      const storage = {id, handle: probe.handle};
      state.storages.push(storage);
      await save();
      storage.size = (await domain.storageSize(s, storage.handle)).toString();
      probe.size = storage.size;
      await save();
    }
    return probe;
  }

  if (request.command === 'root-read-bis') {
    const id = Number(request.id), offset = Number(request.offset ?? 0), length = Number(request.length ?? 4096);
    const storage = state.storages.find(item => item.id === id && s.handles.has(item.handle));
    check(storage && Number.isSafeInteger(offset) && offset >= 0 && Number.isInteger(length) && length > 0 &&
      length <= 0x4000 && BigInt(offset + length) <= BigInt(storage.size), 'Invalid retained BIS read');
    const reply = await context.readStorage(storage.handle, length, offset, 0xa5);
    check(!reply.result && reply.bytes?.length === length, 'BIS read failed');
    const outputFile = path.join(__dirname, label + '-bis-' + id + '-' + offset + '-' + length + '.bin');
    await fs.writeFile(outputFile, reply.bytes, {mode: 0o600});
    const read = {at: new Date().toISOString(), id, offset, length, outputFile,
      sha256: crypto.createHash('sha256').update(reply.bytes).digest('hex')};
    (state.reads ||= []).push(read);
    await save();
    return read;
  }

  if (request.command === 'root-probe-bis-filesystem') {
    check(state.initialized, 'Initialize the dedicated root before probing BIS');
    const id = Number(request.id);
    check([30, 31].includes(id), 'This read-only probe supports USER and SYSTEM');
    const existing = state.filesystems?.find(item => item.id === id && s.handles.has(item.handle));
    const probe = {at: new Date().toISOString(), kind: 'filesystem', id};
    if (existing) Object.assign(probe, {result: 0, opened: true, reused: true, handle: existing.handle});
    else {
      const input = Buffer.alloc(4); input.writeUInt32LE(id);
      await context.put(context.input, Buffer.alloc(0x301));
      const reply = await domain.send(s, 'Dedicated root OpenBisFileSystem ' + id, root,
        pathRequest(11, input, context.input, 0x301));
      Object.assign(probe, {result: reply.canonicalResult, opened: !reply.result});
      if (!reply.result) {
        probe.handle = domain.oneHandle(reply);
        (state.filesystems ||= []).push({id, handle: probe.handle});
      }
    }
    state.probes.push(probe);
    await save();
    if (probe.opened) {
      const rootPath = Buffer.alloc(0x301); rootPath.write('/');
      await context.put(context.input, rootPath);
      const entry = await domain.send(s, 'BIS root GetEntryType ' + id, probe.handle,
        pathRequest(7, Buffer.alloc(0), context.input, 0x301));
      probe.entryResult = entry.canonicalResult;
      if (!entry.result) probe.entryType = entry.data.readUInt32LE();
      await save();
    }
    return probe;
  }
  throw Error('Unknown dedicated-root command');
}

module.exports = {dispatch};
