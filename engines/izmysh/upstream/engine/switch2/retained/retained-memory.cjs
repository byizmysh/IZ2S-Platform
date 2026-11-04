// Loaded by the retained runner, so all native requests use its existing session.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {lowerWindow} = require("./lower-window.cjs");
const {u64} = require("../memory/session.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const source = require('node:fs').readFileSync(__filename);
const sourceHash = hash(source);

function describe(plan) {
  return {target: plan.target, base: plan.base, entryOffset: plan.entryOffset,
    readOffset: plan.readOffset, addressBias: plan.addressBias,
    encoding: plan.encoding, header: plan.bytes.toString('hex'),
    headerFormat: plan.headerFormat, cycle: plan.cycle};
}

function stateFor(context) {
  const {report, bootstrap} = context;
  return report.retainedMemory ||= {phase: 'bootstrap', ready: false,
    bootstrapPlan: describe(bootstrap.getPlan()), operations: [], captures: []};
}

async function saveCapture(context, state, address, bytes, command) {
  const id = state.captures.length + 1;
  const outputFile = path.join(__dirname, context.label + '-retained-' + id + '.bin');
  await fs.writeFile(outputFile, bytes, {mode: 0o600});
  const entry = {id, command, address, length: bytes.length, outputFile, sha256: hash(bytes),
    first64Hex: bytes.subarray(0, 64).toString('hex')};
  state.captures.push(entry);
  await context.save();
  return entry;
}

async function bootstrapRead(context, state, address, length) {
  check(Number.isSafeInteger(address) && address >= 2 ** 32 && address + length < 2 ** 39,
    'Invalid native memory address');
  check(Number.isInteger(length) && length > 0 && length <= 0x600000,
    'Read length must be 1..6 MiB');
  const plan = state.bootstrapPlan, cycle = plan.cycle;
  const parts = [];
  try {
    for (let at = 0; at < length;) {
      const offset = address + at - plan.addressBias;
      check(offset >= 0 && Math.floor(offset / 0x4000) * 0x4000 % cycle === 0,
        'Read must start in the fixed bootstrap bucket');
      const size = Math.min(cycle, length - at);
      check(offset + size <= context.storageSize, 'Read exceeds the native storage bound');
      state.nativeRequest = {address: address + at, offset, length: size};
      await context.save();
      const reply = await context.readStorage(context.candidateHandle, size, offset, 0xa5);
      check(!reply.result && reply.bytes?.length === size, 'Native bootstrap read failed');
      parts.push(reply.bytes);
      delete state.nativeRequest;
      at += size;
    }
  } catch (error) {
    if (parts.length) await saveCapture(context, state, address, Buffer.concat(parts), 'partial-bootstrap-read');
    throw error;
  }
  return Buffer.concat(parts);
}

async function moveBootstrap(context, state, request) {
  check(!state.promoted, 'The retained channels must never use another path corruption');
  const address = Number(request.address), length = Number(request.length ?? 0x4000);
  const anchorOffset = Number(request.anchorOffset);
  check(Number.isInteger(length) && length >= 32 && length <= 0x4000 && Number.isInteger(anchorOffset),
    'The metadata probe must fit in one native block');
  const old = state.bootstrapPlan;
  const next = lowerWindow(address, context.storageSize, old.cycle, [Math.floor(old.target / 0x4000) * 0x4000]);
  let anchor;
  if (request.anchorHeader === true) {
    check(anchorOffset === 0, 'The owned entry must begin at the selected address');
    const capture = state.captures.find(item => item.address <= address && address + 0x30 <= item.address + item.length);
    check(capture, 'The owned entry has not been captured');
    const bytes = await fs.readFile(capture.outputFile);
    check(hash(bytes) === capture.sha256 && bytes.subarray(address - capture.address,
      address - capture.address + 16).toString('hex') === old.header.slice(0, 32),
    'The captured entry does not match the active header');
    anchor = next.bytes.subarray(0, 16);
  } else {
    check(typeof request.anchorHex === 'string' && /^(?:[a-fA-F0-9]{2}){16,128}$/.test(request.anchorHex),
      'Expected native reciprocal links are required');
    anchor = Buffer.from(request.anchorHex, 'hex');
  }
  check(anchorOffset >= 0 && anchorOffset + anchor.length <= length, 'Anchor is outside the probe');
  const fallbackAddress = old.addressBias + next.readOffset;
  const sources = [...context.report.memoryControl.operations, ...state.captures,
    {address: context.report.cacheAddress.target, length: 0x4000, outputFile: context.report.cacheAddress.outputFile}];
  check(sources.some(source => source.outputFile && source.address <= fallbackAddress &&
    fallbackAddress + length <= source.address + source.length),
  'A missed header would not land inside captured native memory');
  const before = await bootstrapRead(context, state, fallbackAddress, length);
  check(!before.subarray(anchorOffset, anchorOffset + anchor.length).equals(anchor),
    'The proposed metadata anchor also matches the old mapping');
  const entry = {command: request.command, address, length, oldPlan: old, proposedPlan: describe(next),
    fallbackAddress, beforeSha256: hash(before), anchorOffset, anchorHex: anchor.toString('hex'),
    casesBefore: context.report.cases.length, started: new Date().toISOString()};
  state.operations.push(entry);
  state.pending = entry;
  await context.save();
  await context.copyField(next.bytes, 'retained-metadata-' + state.operations.length, 1);
  entry.casesAfter = context.report.cases.length;
  const first = await context.readStorage(context.candidateHandle, length, next.readOffset, 0xa5);
  check(!first.result && first.bytes?.length === length, 'Metadata probe failed');
  entry.observedSha256 = hash(first.bytes);
  if (first.bytes.equals(before)) {
    entry.missed = true;
    delete state.pending;
    await context.save();
    return entry;
  }
  check(first.bytes.subarray(anchorOffset, anchorOffset + anchor.length).equals(anchor),
    'Metadata probe matches neither the native reciprocal links nor the old mapping');
  const second = await context.readStorage(context.candidateHandle, length, next.readOffset, 0x5a);
  check(!second.result && second.bytes.subarray(anchorOffset, anchorOffset + anchor.length).equals(anchor),
    'Repeated metadata link proof failed');
  state.bootstrapPlan = describe(next);
  context.report.memoryControl.plan = state.bootstrapPlan;
  context.report.memoryControl.phase = 'bootstrap-metadata';
  entry.verified = true;
  entry.capture = await saveCapture(context, state, address, second.bytes, request.command);
  delete state.pending;
  await context.save();
  return entry;
}

function cacheEntry(offset, address) {
  const bytes = Buffer.alloc(0x30);
  bytes.writeBigUInt64LE(BigInt(offset), 0);
  bytes.writeBigUInt64LE(0x4000n, 8);
  bytes.fill(1, 0x10, 0x13);
  bytes.writeBigUInt64LE(BigInt(address), 0x20);
  bytes.writeBigUInt64LE(0x4000n, 0x28);
  return bytes;
}

async function logicalRead(context, state, offset, length) {
  check(Number.isSafeInteger(offset) && offset >= 0 && offset + length <= context.storageSize &&
    Number.isInteger(length) && length > 0 && length <= 0x4000, 'Invalid retained read');
  state.nativeRequest = {operation: 'read', offset, length};
  await context.save();
  const reply = await context.readStorage(context.candidateHandle, length, offset, 0xa5);
  check(!reply.result && reply.bytes?.length === length, 'Retained native read failed');
  delete state.nativeRequest;
  return reply.bytes;
}

async function logicalWrite(context, state, offset, bytes) {
  check(Number.isSafeInteger(offset) && offset >= 0 && offset + bytes.length <= context.storageSize &&
    bytes.length > 0 && bytes.length <= 0x4000, 'Invalid retained write');
  state.nativeRequest = {operation: 'write', offset, length: bytes.length};
  await context.save();
  await context.put(context.input, bytes);
  const reply = await context.s.ipc('Write retained FS memory', context.candidateHandle, 1,
    Buffer.concat([u64(offset), u64(bytes.length)]),
    {send: [{address: context.input, size: bytes.length, mode: 1}]});
  check(!reply.result, 'Retained native write failed: ' + reply.canonicalResult.toString(16));
  delete state.nativeRequest;
}

async function bootstrapWrite(context, state, address, bytes) {
  const offset = address - state.bootstrapPlan.addressBias;
  check(Math.floor(offset / 0x4000) * 0x4000 % state.bootstrapPlan.cycle === 0 &&
    offset % 0x4000 + bytes.length <= 0x4000, 'Metadata write is outside the bootstrap bucket');
  await logicalWrite(context, state, offset, bytes);
}

async function controlRead(context, state, offset, length) {
  check(state.canonical && offset >= 0 && offset + length <= state.arrayLength,
    'Control read is outside the owned cache array');
  return logicalRead(context, state, offset, length);
}

async function controlWrite(context, state, offset, bytes) {
  check(state.canonical && offset >= 0 && offset + bytes.length <= state.arrayLength,
    'Control write is outside the owned cache array');
  await logicalWrite(context, state, offset, bytes);
}

async function setPointer(context, state, address) {
  check(state.canonical && Number.isSafeInteger(address) && address >= 2 ** 32 && address < 2 ** 39,
    'Invalid retained target address');
  const entry = await controlRead(context, state, state.dataEntryOffset, 0x30);
  check(entry.readBigUInt64LE(0) === BigInt(state.dataOffset) && entry.readBigUInt64LE(8) === 0x4000n &&
    entry.subarray(0x10, 0x13).equals(Buffer.from([1, 1, 1])) &&
    entry.readBigUInt64LE(0x20) === BigInt(state.pointer), 'The retained data descriptor changed');
  if (address !== state.pointer) {
    await controlWrite(context, state, state.dataEntryOffset + 0x20, u64(address));
    const observed = await controlRead(context, state, state.dataEntryOffset + 0x20, 8);
    check(observed.equals(u64(address)), 'Data pointer readback differs');
    state.pointer = address;
  }
}

function verifyOwner(bytes, state, storageSize) {
  check(bytes.length === 0x78 && bytes.readBigUInt64LE(0x20) === BigInt(storageSize) &&
    bytes.readBigUInt64LE(0x28) === 0x4000n && bytes.readBigUInt64LE(0x48) === BigInt(state.array) &&
    bytes.readBigUInt64LE(0x50) === BigInt(state.arrayLength) && bytes.readBigUInt64LE(0x60) === 14n,
  'The native storage owner no longer matches its array, size, block size, and shift');
}

async function install(context, state, request) {
  check(!state.promoted && !state.ready, 'Retained channels are already being installed');
  const array = Number(request.array), owner = Number(request.owner);
  check(Number.isSafeInteger(owner) && owner >= 2 ** 32 && owner < 2 ** 39 && owner % 8 === 0 &&
    array === state.bootstrapPlan.target, 'First map the bootstrap directly onto the owned array');
  const arrayLength = 0x5a0, dataBucket = 1, dataEntryOffset = dataBucket * 0x90;
  const before = await bootstrapRead(context, state, array, arrayLength);
  check(before.subarray(0, 16).toString('hex') === state.bootstrapPlan.header.slice(0, 32) &&
    before[0x10] === 0 && !before.subarray(0x18, 0x30).some(Boolean),
  'The control read does not expose its own borrowed descriptor');
  check(require("./cache-shape.cjs").unusedTail(before), 'The remaining cache entries are neither empty nor the known path fill');
  state.array = array;
  state.owner = owner;
  state.arrayLength = arrayLength;
  state.dataOffset = dataBucket * 0x4000;
  state.dataEntryOffset = dataEntryOffset;
  state.scratchOffset = arrayLength - 0x18;
  state.corruptionCount = context.report.cases.length;
  state.installation = {started: new Date().toISOString(), checks: []};
  state.phase = 'installing';
  // From this point, cleanup must handle both descriptors and never call the
  // legacy one-entry path overwrite. Incomplete installation stays retained.
  state.promoted = true;
  await context.save();
  const checkStage = async name => {
    state.installation.checks.push(name);
    await context.save();
  };
  if (before.subarray(0x30).some(Boolean)) {
    state.installation.replacedPathTailHex = before.subarray(0x30).toString('hex');
    await context.save();
    await bootstrapWrite(context, state, array + 0x30, Buffer.alloc(arrayLength - 0x30));
    check(!(await bootstrapRead(context, state, array + 0x30, arrayLength - 0x30)).some(Boolean),
      'The unused path-filled entries did not clear');
    await checkStage('cleared the recognized pathname from unused cache entries');
  }
  const scratch = array + state.scratchOffset;
  const original = before.subarray(state.scratchOffset, state.scratchOffset + 16);
  const marker = Buffer.from('52455441494e454400ff1122aa559966', 'hex');
  const secondMarker = Buffer.from('52455441494e454401ee2233bb66aa77', 'hex');
  state.scratchBeforeHex = original.toString('hex');
  await bootstrapWrite(context, state, scratch, marker);
  const dataHeader = cacheEntry(state.dataOffset, scratch);
  await bootstrapWrite(context, state, array + dataEntryOffset, dataHeader);
  const installed = await bootstrapRead(context, state, array + dataEntryOffset, 0x30);
  check(installed.equals(dataHeader), 'Data descriptor installation differs');
  check((await logicalRead(context, state, state.dataOffset, 16)).equals(marker),
    'The data channel did not read the owned scratch marker');
  await logicalWrite(context, state, state.dataOffset, secondMarker);
  check((await bootstrapRead(context, state, scratch, 16)).equals(secondMarker),
    'The control channel did not observe the data write');
  await logicalWrite(context, state, state.dataOffset, original);
  check((await bootstrapRead(context, state, scratch, 16)).equals(original),
    'The owned scratch bytes were not restored');
  state.scratchRestored = true;
  await checkStage('independent data read, write, and exact restoration');

  // Use the other bucket to change the control descriptor. Its own in-flight
  // release will then restore only the data bucket, preserving this change.
  await bootstrapWrite(context, state, array + dataEntryOffset + 0x20, u64(array));
  const releasedControl = await logicalRead(context, state, state.dataOffset, 0x30);
  check(releasedControl.subarray(0, 16).toString('hex') === state.bootstrapPlan.header.slice(0, 32),
    'Data channel did not reach the control descriptor');
  state.controlBeforeCanonicalHex = releasedControl.toString('hex');
  await context.save();
  await logicalWrite(context, state, state.dataOffset, cacheEntry(0, array));
  state.canonical = true;
  state.pointer = array;
  const control = await controlRead(context, state, 0, arrayLength);
  check(control.readBigUInt64LE(0) === 0n && control.readBigUInt64LE(8) === 0x4000n &&
    control[0x10] === 0 && control.subarray(dataEntryOffset, dataEntryOffset + 0x30)
      .equals(cacheEntry(state.dataOffset, array)), 'Canonical control and data descriptors differ');
  await checkStage('cross-channel canonical control mapping');

  await setPointer(context, state, owner);
  const ownerBefore = await logicalRead(context, state, state.dataOffset, 0x78);
  verifyOwner(ownerBefore, state, context.storageSize);
  check(ownerBefore.readInt32LE(0x58) === 10, 'Unexpected native cache bucket count');
  state.ownerBeforeHex = ownerBefore.toString('hex');
  await context.save();
  // Both fixed logical offsets now use nonnegative block indices. With count
  // zero, lookup/release retain these indices, while flush/finalize loops skip
  // synthetic buffers. No vtable, function pointer, or backing storage changes.
  await logicalWrite(context, state, state.dataOffset + 0x58, Buffer.alloc(4));
  const ownerAfter = await logicalRead(context, state, state.dataOffset, 0x78);
  verifyOwner(ownerAfter, state, context.storageSize);
  check(ownerAfter.readInt32LE(0x58) === 0, 'Cache loop disabling did not read back');
  state.zeroCountVerified = true;
  await checkStage('native count-zero lookup and release');

  await setPointer(context, state, scratch);
  check((await logicalRead(context, state, state.dataOffset, 16)).equals(original),
    'Retargeted scratch read differs after disabling cache loops');
  await logicalWrite(context, state, state.dataOffset, marker);
  check((await controlRead(context, state, state.scratchOffset, 16)).equals(marker),
    'Canonical control did not observe the retained data write');
  await logicalWrite(context, state, state.dataOffset, original);
  check((await controlRead(context, state, state.scratchOffset, 16)).equals(original),
    'Canonical scratch restoration differs');
  check(context.report.cases.length === state.corruptionCount, 'Installation repeated path corruption');
  await checkStage('canonical binary retarget, write, and restoration');
  state.installation.finished = new Date().toISOString();
  state.phase = 'retained';
  state.ready = true;
  context.report.memoryControl.phase = 'retained';
  context.report.memoryControl.steadyStateReliable = true;
  await context.save();
  return {ready: true, phase: state.phase, array, owner, dataOffset: state.dataOffset,
    corruptionCount: state.corruptionCount, installation: state.installation};
}

async function retire(context) {
  const state = stateFor(context);
  if (state.retired) return {retired: true};
  check(state.canonical && state.zeroCountVerified,
    'Incomplete retained setup requires recovery; legacy corruption cleanup is disabled');
  await setPointer(context, state, state.owner);
  const owner = await logicalRead(context, state, state.dataOffset, 0x78);
  verifyOwner(owner, state, context.storageSize);
  check(owner.readInt32LE(0x58) === 0, 'Synthetic buffers are not excluded from finalization');
  check(context.report.cases.length === state.corruptionCount, 'Retained lifecycle repeated path corruption');
  state.ready = false;
  state.retired = true;
  state.phase = 'retired';
  context.report.memoryControl.phase = 'retired';
  await context.save();
  return {retired: true, corruptionCount: state.corruptionCount};
}

function retainedInfo(context, state) {
  return {ready: state.ready, session: context.live.session, report: context.file,
    phase: state.phase, steadyStateReliable: state.ready && state.zeroCountVerified,
    readVerified: state.ready, writeVerified: state.ready, requiresCorruption: false,
    corruptionCount: state.corruptionCount, controlAddress: state.array,
    dataOffset: state.dataOffset, currentTarget: state.pointer,
    plan: {encoding: 'retained-two-bucket', target: state.pointer,
      pointerAddress: state.array + state.dataEntryOffset + 0x20}};
}

async function readMemory(context, state, address, length) {
  check(Number.isSafeInteger(address) && address >= 2 ** 32 && address + length < 2 ** 39 &&
    Number.isInteger(length) && length > 0 && length <= 0x600000, 'Invalid retained memory range');
  const parts = [];
  try {
    for (let at = 0; at < length; at += 0x4000) {
      await setPointer(context, state, address + at);
      parts.push(await logicalRead(context, state, state.dataOffset, Math.min(0x4000, length - at)));
    }
  } catch (error) {
    if (parts.length) await saveCapture(context, state, address, Buffer.concat(parts), 'partial-retained-read');
    throw error;
  }
  return Buffer.concat(parts);
}

async function writeMemory(context, state, address, bytes) {
  for (let at = 0; at < bytes.length; at += 0x4000) {
    const part = bytes.subarray(at, at + 0x4000);
    await setPointer(context, state, address + at);
    await logicalWrite(context, state, state.dataOffset, part);
    check((await logicalRead(context, state, state.dataOffset, part.length)).equals(part),
      'Retained write readback differs');
  }
}

async function access(context, state, request) {
  check(state.ready && state.canonical && state.zeroCountVerified, 'Retained memory is not ready');
  check(context.report.cases.length === state.corruptionCount, 'The corruption count changed after setup');
  const command = request.command.replace(/^retained-/, '');
  if (command === 'info') return retainedInfo(context, state);
  if (command.startsWith('root-'))
    return require("../memory/fs-root-control.cjs").dispatch(context, request);
  if (command === 'inject-holder') {
    const control = require("../ro/worker-control.cjs");
    await context.s.verifyCode(control.cancelCode, 'CancelSynchronization');
    const server = Number(request.server), parent = Number(request.parent);
    // Check the selection before the session and the receipt marker last.
    // This order distinguishes a lost wake from an already completed receive.
    const waiter = await readMemory(context, state, parent + 0x10, 0x18);
    const selected = Number(waiter.readBigUInt64LE());
    if (selected) return {injected: false, selected};
    const session = await readMemory(context, state, server, 0x88);
    check(session.readBigUInt64LE() === BigInt(request.vptr) &&
      session.readUInt32LE(0x30) === request.park && session.readUInt32LE(0x38) === 1 &&
      session.readUInt32LE(0x48) === request.receive &&
      session.readBigUInt64LE(0x60) === BigInt(request.buffer) && session.readBigUInt64LE(0x68) === 0x100n &&
      session.readBigUInt64LE(0x70) === BigInt(request.domain) &&
      session.readBigUInt64LE(0x78) === BigInt(request.metadata), 'Parked session identity changed');
    // An idle holder may move to another native waiter between our reads.
    // Retry without writing when its parent or active state has changed.
    if (session.readBigUInt64LE(0x28) !== BigInt(parent) || session.readUInt32LE(0x40) !== 1)
      return {injected: false, retry: true, parent: Number(session.readBigUInt64LE(0x28)),
        state: session.readUInt32LE(0x40)};
    const header = (await readMemory(context, state, Number(request.buffer), 4)).readUInt32LE();
    if (header !== 0xffffffff) return {injected: false, acknowledged: true, header};
    await setPointer(context, state, parent + 0x10);
    const entry = {id: state.operations.length + 1, command, server, parent,
      epoch: waiter.readBigUInt64LE(8).toString(),
      started: new Date().toISOString(), corruptionCount: state.corruptionCount};
    state.operations.push(entry);
    await context.save();
    // A normal verified write performs another FS read. That can consume the
    // wake, so cancellation must immediately follow this unverified store.
    await logicalWrite(context, state, state.dataOffset, u64(server));
    entry.cancellations = await control.cancelWorkers(context);
    entry.injected = true;
    entry.finished = new Date().toISOString();
    context.report.memoryControl.plan = retainedInfo(context, state).plan;
    await context.save();
    check(entry.cancellations.every(reply => reply.result === 0), 'Worker cancellation failed after injection');
    return entry;
  }
  check(['select', 'read', 'write'].includes(command), 'Unknown retained-memory command');
  const address = Number(request.address);
  const entry = {id: state.operations.length + 1, command, address,
    started: new Date().toISOString(), corruptionCount: state.corruptionCount};
  state.operations.push(entry);
  if (command === 'select') {
    await setPointer(context, state, address);
    entry.pointerVerified = true;
    entry.plan = retainedInfo(context, state).plan;
  } else if (command === 'read') {
    const bytes = await readMemory(context, state, address, Number(request.length));
    Object.assign(entry, await saveCapture(context, state, address, bytes, command));
  } else {
    check(typeof request.hex === 'string' && /^(?:[0-9a-fA-F]{2})+$/.test(request.hex) &&
      request.hex.length <= 0x20000 && typeof request.expectedHex === 'string' &&
      request.expectedHex.length === request.hex.length && /^[0-9a-fA-F]+$/.test(request.expectedHex),
    'Writes require 1..64 KiB and exact expected bytes');
    const bytes = Buffer.from(request.hex, 'hex');
    const before = await readMemory(context, state, address, bytes.length);
    check(before.equals(Buffer.from(request.expectedHex, 'hex')), 'The expected memory bytes differ');
    const preimage = await saveCapture(context, state, address, before, 'before-retained-write');
    Object.assign(entry, {length: bytes.length, hex: bytes.toString('hex'),
      expectedHex: before.toString('hex'), beforeFile: preimage.outputFile, pending: true});
    state.pending = entry;
    context.report.memoryControl.pending = {operation: 'retained-write', address, length: bytes.length};
    await context.save();
    try {
      await writeMemory(context, state, address, bytes);
      entry.verified = true;
    } catch (error) {
      entry.error = error.stack;
      if (await context.s.ready()) {
        await writeMemory(context, state, address, before);
        entry.restored = (await readMemory(context, state, address, before.length)).equals(before);
      }
      if (entry.restored) {
        delete entry.pending;
        delete state.pending;
        delete context.report.memoryControl.pending;
      }
      await context.save();
      throw error;
    }
    delete entry.pending;
    delete state.pending;
    delete context.report.memoryControl.pending;
  }
  check(context.report.cases.length === state.corruptionCount, 'Retained access repeated path corruption');
  entry.finished = new Date().toISOString();
  entry.requiresCorruption = false;
  context.report.memoryControl.plan = retainedInfo(context, state).plan;
  await context.save();
  return entry;
}

async function dispatch(context, request) {
  const state = stateFor(context);
  check(!state.pending, 'A previous metadata mutation is unresolved');
  if (!state.sources?.some(item => item.sha256 === sourceHash)) {
    const sourceFile = path.join(__dirname, context.label + '-implementation-' + sourceHash.slice(0, 12) + '.cjs');
    await fs.writeFile(sourceFile, source, {mode: 0o600});
    (state.sources ||= []).push({sourceFile, sha256: sourceHash});
    await context.save();
  }
  if (request.command === 'retained-info' || request.command === 'info')
    return state.ready ? retainedInfo(context, state) : state;
  if (request.command === 'retained-bootstrap-move') return moveBootstrap(context, state, request);
  if (request.command === 'retained-install') return install(context, state, request);
  if (request.command === 'retire') return retire(context);
  if (request.command === 'retained-bootstrap-read') {
    check(!state.promoted, 'Use the retained channels after installation');
    const address = Number(request.address), length = Number(request.length);
    const before = context.report.cases.length;
    const bytes = await bootstrapRead(context, state, address, length);
    check(context.report.cases.length === before, 'A fixed read performed another corruption');
    return saveCapture(context, state, address, bytes, request.command);
  }
  if (state.ready) return access(context, state, request);
  throw Error('Unknown retained-memory command');
}

module.exports = {dispatch, retire};
