// Bootstrap tooling, one local client at a time. Retargeting can still need
// pathname overwrites; this is not the final retained-object memory primitive.
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const crypto = require('node:crypto');
const {check, u64} = require("./session.cjs");
const {StopRequested} = require("./run-control.cjs");
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

async function serve(context) {
  const {s, live, report, file, label, save, storageSize, copyField, readStorage,
    candidateHandle, input, put} = context;
  const runControl = context.runControl;
  const bulk = require(path.join(__dirname, '../app/bulk-transfer.cjs'));
  let activePlan = context.initialPlan, busy = false, serial = 0, allowRetarget = false, forceRetarget = false;
  const knownRanges = [[activePlan.target, activePlan.target + 0x4000]];
  let cycle = report.cacheAddress.bucketPeriod || 0x500000;
  check(runControl?.state.directory, 'The run must own its memory endpoint directory');
  check(activePlan.encoding === 'signed-one-shot', 'Expected the Switch 2 signed cache header');
  const socketPath = path.join(runControl.state.directory, 'memory.sock');
  const state = report.memoryControl = {socketPath, ready: false, session: live.session,
    phase: 'bootstrap', steadyStateReliable: false, operations: []};
  for (const name of ['fs-memory-control.cjs', 'fs-root-control.cjs']) {
    const source = await fs.readFile(path.join(__dirname, name));
    const sourceFile = path.join(__dirname, label + '-active-' + name);
    await fs.writeFile(sourceFile, source, {mode: 0o600});
    (state.sources ||= []).push({sourceFile, sha256: hash(source)});
  }
  const describePlan = plan => ({target: plan.target, base: plan.base, entryOffset: plan.entryOffset,
    readOffset: plan.readOffset, addressBias: plan.addressBias, encoding: plan.encoding,
    header: plan.bytes.toString('hex'), headerFormat: plan.headerFormat || 'original'});
  state.plan = describePlan(activePlan);

  function rememberRange(address, length) {
    knownRanges.push([address, address + length]);
    knownRanges.sort((a, b) => a[0] - b[0]);
    for (let i = 1; i < knownRanges.length;) {
      const previous = knownRanges[i - 1], current = knownRanges[i];
      if (current[0] <= previous[1]) {
        previous[1] = Math.max(previous[1], current[1]);
        knownRanges.splice(i, 1);
      } else i++;
    }
  }

  async function select(address, length) {
    check(Number.isSafeInteger(address) && address >= 0x100000000 && address + length < 2 ** 39,
      'Memory address is outside the qualified width');
    let offset = address - activePlan.addressBias;
    // A read may extend past its starting block, as the native proof showed.
    const aligned = Math.floor(offset / 0x4000) * 0x4000;
    const hits = offset >= 0 && BigInt(aligned) >= BigInt(activePlan.entryOffset) &&
      offset + length <= storageSize && aligned % cycle === 0;
    if (!hits || forceRetarget) {
      check(allowRetarget, 'Address needs a new header; retargeting remains unqualified and requires retarget:true');
      const next = require("./signed-cache-plan.cjs").plan(address, storageSize, cycle);
      check(next.readOffset + length <= storageSize, 'Memory window exceeds the storage request limit');
      state.pending = {operation: 'retarget', ...describePlan(next)};
      await save();
      for (const step of next.writes) await copyField(step.bytes, 'retarget-' + serial + '-' + step.end);
      activePlan = next;
      forceRetarget = false;
      state.plan = describePlan(next);
      delete state.pending;
      await save();
      offset = next.readOffset;
    }
    return offset;
  }

  async function readBlock(address, length) {
    const offset = await select(address, length);
    const output = s.base + 0xa00000;
    state.nativeRequest = {stage: 'poison', address, offset, length};
    await save();
    await s.call('memset', output, 0xa5, length);
    state.nativeRequest.stage = 'ipc';
    await save();
    const reply = await s.ipc('Read bounded FS memory window', candidateHandle, 0,
      Buffer.concat([u64(offset), u64(length)]), {receive: [{address: output, size: length, mode: 1}]});
    state.nativeRequest.stage = 'download';
    state.nativeRequest.result = reply.canonicalResult;
    await save();
    const bytes = length > 0x8000 ? await bulk.read(s, output, length) : await s.readOwned(output, length);
    if (reply.result) {
      const failedFile = path.join(__dirname, label + '-failed-read-' + Date.now() + '.bin');
      await fs.writeFile(failedFile, bytes, {mode: 0o600});
      state.failedRead = {address, offset, length, result: reply.canonicalResult, rawResult: reply.result,
        outputFile: failedFile, changedBytes: bytes.reduce((n, byte) => n + (byte !== 0xa5), 0)};
      await save();
      throw Object.assign(Error('FS memory read returned 0x' + reply.canonicalResult.toString(16)),
        {nativeResult: reply.canonicalResult});
    }
    delete state.nativeRequest;
    rememberRange(address, length);
    return bytes;
  }

  async function read(address, length) {
    check(Number.isInteger(length) && length > 0 && length <= 0x600000, 'Read length must be 1..6 MiB');
    // Stay below the observed large-read failure. With a qualified smaller
    // period, each chunk starts in the same cache bucket as the previous one.
    const step = cycle <= 0x40000 ? Math.floor(0x40000 / cycle) * cycle : 0x40000;
    const parts = [];
    try {
      for (let at = 0; at < length;) {
        const current = address + at;
        const offset = current - activePlan.addressBias;
        const start = Math.floor(offset / cycle) * cycle;
        const prefix = offset - start;
        // Read an overlapping qualified window when the requested address is
        // in another bucket. Slice locally instead of rebuilding the header.
        const direct = Math.floor(offset / 0x4000) * 0x4000 % cycle === 0;
        const prefixKnown = knownRanges.some(([begin, end]) => begin <= current - prefix && current <= end);
        const overlap = !forceRetarget && !direct && prefixKnown && cycle <= 0x40000 &&
          start >= 0 && BigInt(start) >= BigInt(activePlan.entryOffset) && start + cycle <= storageSize;
        const size = Math.min(overlap ? cycle - prefix : step, length - at);
        const bytes = await readBlock(overlap ? current - prefix : current, overlap ? prefix + size : size);
        parts.push(overlap ? bytes.subarray(prefix) : bytes);
        at += size;
      }
    } catch (error) {
      if (parts.length) {
        const bytes = Buffer.concat(parts);
        const outputFile = path.join(__dirname, label + '-memory-' + serial + '-partial-' + Date.now() + '.bin');
        await fs.writeFile(outputFile, bytes, {mode: 0o600});
        state.partialRead = {operation: serial, address, length: bytes.length, outputFile, sha256: hash(bytes)};
        await save();
      }
      throw error;
    }
    return Buffer.concat(parts);
  }

  async function writeAt(address, bytes) {
    const offset = await select(address, bytes.length);
    check(offset % 0x4000 !== 0 || bytes.length < 0x8000,
      'An aligned large write may bypass the controlled cache');
    await put(input, bytes);
    const reply = await s.ipc('Write verified FS memory', candidateHandle, 1,
      Buffer.concat([u64(offset), u64(bytes.length)]), {send: [{address: input, size: bytes.length, mode: 1}]});
    check(!reply.result, 'FS memory write failed: ' + reply.canonicalResult.toString(16));
  }

  async function qualifySignedHeader(address) {
    const length = 0x4000, markerOffset = 0x108;
    check(Number.isSafeInteger(address) && address % length === 0 &&
      knownRanges.some(([begin, end]) => begin <= address && address + length <= end),
      'Signed-header qualification needs a previously read, aligned native block');
    const next = require("./signed-cache-plan.cjs").plan(address, storageSize, cycle);
    const retained = JSON.parse(await fs.readFile(file)).retained;
    const controlHandle = retained.storages[0];
    check(s.handles.has(controlHandle) && controlHandle !== candidateHandle, 'Ordinary content control unavailable');
    const ordinary = await readStorage(controlHandle, length, next.readOffset);
    check(!ordinary.result, 'Ordinary signed-offset control failed');
    allowRetarget = forceRetarget = false;
    const before = await read(address, length);
    check(!before.equals(ordinary.bytes), 'Memory and ordinary content do not distinguish the mapping');
    const entry = {id: ++serial, at: new Date().toISOString(), command: 'qualify-signed-header',
      address, length, plan: describePlan(next), beforeSha256: hash(before),
      ordinarySha256: hash(ordinary.bytes), previousPlan: state.plan};
    state.operations.push(entry);
    state.currentOperation = serial;
    entry.beforeFile = path.join(__dirname, label + '-signed-before-' + serial + '.bin');
    await fs.writeFile(entry.beforeFile, before, {mode: 0o600});
    state.pending = {operation: 'qualify-signed-header', ...describePlan(next)};
    await save();
    const firstCase = report.cases.length;
    await copyField(next.writes[0].bytes, 'qualify-signed-header-' + serial);
    entry.copyAttempts = report.cases.length - firstCase;
    activePlan = next;
    state.plan = describePlan(next);
    await save();
    const first = await readStorage(candidateHandle, length, next.readOffset, 0xa5);
    const second = await readStorage(candidateHandle, length, next.readOffset, 0x5a);
    entry.readResults = [first.result, second.result];
    entry.readVerified = !first.result && !second.result && first.bytes.equals(before) && second.bytes.equals(before);
    await save();
    check(entry.readVerified, 'Signed-header reads differ from the captured native block');
    const marker = Buffer.from('5349474e45442d525700ffcc1122aa6633', 'hex');
    const expected = Buffer.from(before);
    marker.copy(expected, markerOffset);
    entry.pending = true;
    await save();
    try {
      await writeAt(address + markerOffset, marker);
      const changed = await read(address, length);
      entry.writeVerified = changed.equals(expected);
      entry.changedSha256 = hash(changed);
      check(entry.writeVerified, 'Signed-header write or surrounding bytes differ');
    } finally {
      await writeAt(address + markerOffset, before.subarray(markerOffset, markerOffset + marker.length));
      const restored = await read(address, length);
      entry.restored = restored.equals(before);
      entry.restoredSha256 = hash(restored);
      await save();
      check(entry.restored, 'Signed-header restoration differs from the entire original block');
    }
    delete entry.pending;
    delete state.pending;
    delete state.currentOperation;
    state.signedHeaderQualified = true;
    await save();
    return entry;
  }

  async function dispatch(request) {
    if (request.command === 'info' && report.retainedMemory?.promoted)
      return require("../retained/retained-memory.cjs").dispatch(context, request);
    if (request.command === 'info') return {ready: state.ready, session: live.session, report: file, plan: state.plan, bucketPeriod: cycle,
      phase: state.phase, steadyStateReliable: state.steadyStateReliable,
      lastError: state.error || null,
      readVerified: report.cacheAddress.readVerified, writeVerified: report.cacheAddress.writeVerified};
    if (request.command?.startsWith('retained-') || report.retainedMemory?.ready)
      return require("../retained/retained-memory.cjs").dispatch(context, request);
    if (request.command === 'retire') return {retiring: true};
    if (typeof request.command === 'string' && request.command.startsWith('root-'))
      return require("./fs-root-control.cjs").dispatch(context, request);
    if (request.command === 'qualify-signed-header')
      return qualifySignedHeader(Number(request.address ?? activePlan.target));
    if (request.command === 'qualify-single-bucket') {
      // This command does not change FS metadata. It verifies a prior,
      // separately checked bucket-count write against captured native bytes.
      check(cycle === 0x28000, 'The original ten-bucket period is required');
      const address = Number(request.address), expectedHex = request.expectedHex;
      check(Number.isSafeInteger(address) && typeof expectedHex === 'string' &&
        /^(?:[0-9a-fA-F]{2}){16,64}$/.test(expectedHex), 'Expected16..64 native bytes are required');
      const expected = Buffer.from(expectedHex, 'hex'), offset = address - activePlan.addressBias;
      check(knownRanges.some(([begin, end]) => begin <= address && address + expected.length <= end),
        'Qualification address has not previously been read');
      check(offset >= 0 && offset + expected.length <= storageSize &&
        Math.floor(offset / 0x4000) * 0x4000 % cycle !== 0, 'Qualification must use a formerly different bucket');
      const retained = JSON.parse(await fs.readFile(file)).retained;
      const controlHandle = retained.storages[0];
      check(s.handles.has(controlHandle) && controlHandle !== candidateHandle, 'The ordinary storage control is unavailable');
      const ordinary = await readStorage(controlHandle, expected.length, offset);
      check(!ordinary.result && !ordinary.bytes.equals(expected), 'Ordinary file content does not distinguish this address');
      const first = await readStorage(candidateHandle, expected.length, offset, 0xa5);
      const second = await readStorage(candidateHandle, expected.length, offset, 0x5a);
      const proof = state.singleBucketQualification = {address, offset, expectedHex,
        ordinaryHex: ordinary.bytes.toString('hex'), results: [first.result, second.result],
        observedHex: [first.bytes?.toString('hex'), second.bytes?.toString('hex')],
        verified: !first.result && !second.result && first.bytes.equals(expected) && second.bytes.equals(expected)};
      await save();
      check(proof.verified, 'Single-bucket behavior did not match captured native memory');
      cycle = state.bucketPeriod = 0x4000;
      await save();
      return {bucketPeriod: cycle, ...proof};
    }
    check(['select', 'read', 'write'].includes(request.command), 'Unknown memory command');
    allowRetarget = request.retarget === true;
    forceRetarget = request.forceRetarget === true;
    check(!forceRetarget || allowRetarget, 'Forced retargeting also requires retarget:true');
    const address = Number(request.address);
    const entry = {id: ++serial, at: new Date().toISOString(), command: request.command, address,
      forceRetarget};
    state.operations.push(entry);
    state.currentOperation = serial;
    await save();
    if (request.command === 'select') {
      check(allowRetarget, 'Header selection requires retarget:true');
      await select(address, 1);
      entry.plan = state.plan;
      entry.nativeAddressVerified = false;
    } else if (request.command === 'read') {
      entry.length = Number(request.length);
      await save();
      const bytes = await read(address, entry.length);
      entry.length = bytes.length;
      entry.outputFile = path.join(__dirname, label + '-memory-' + serial + '.bin');
      await fs.writeFile(entry.outputFile, bytes, {mode: 0o600});
      entry.sha256 = hash(bytes);
      entry.first64Hex = bytes.subarray(0, 64).toString('hex');
    } else {
      check(typeof request.hex === 'string' && /^(?:[0-9a-fA-F]{2})+$/.test(request.hex), 'Invalid write bytes');
      const bytes = Buffer.from(request.hex, 'hex');
      check(bytes.length <= 0x10000 && typeof request.expectedHex === 'string' &&
        request.expectedHex.length === bytes.length * 2 && /^[0-9a-fA-F]+$/.test(request.expectedHex),
        'Writes need matching expected bytes and are limited to 64 KiB');
      const before = await read(address, bytes.length);
      check(before.equals(Buffer.from(request.expectedHex, 'hex')), 'The expected memory bytes differ');
      entry.length = bytes.length;
      entry.beforeFile = path.join(__dirname, label + '-memory-' + serial + '-before.bin');
      await fs.writeFile(entry.beforeFile, before, {mode: 0o600});
      entry.hex = bytes.toString('hex');
      entry.expectedHex = before.toString('hex');
      entry.pending = true;
      await save();
      try {
        await writeAt(address, bytes);
        check((await read(address, bytes.length)).equals(bytes), 'FS memory write readback differs');
        entry.verified = true;
      } catch (error) {
        await writeAt(address, before);
        entry.restored = (await read(address, before.length)).equals(before);
        check(entry.restored, 'Failed write could not be restored');
        throw error;
      }
      delete entry.pending;
    }
    delete state.currentOperation;
    await save();
    return entry;
  }

  let finish, fail, retiring = false;
  const done = new Promise((resolve, reject) => {finish = resolve; fail = reject;});
  const server = net.createServer(socket => {
    let text = '', handled = false;
    socket.setEncoding('utf8');
    socket.on('error', () => {});
    socket.on('data', async chunk => {
      if (handled) return;
      text += chunk;
      if (text.length > 0x100000) {handled = true; socket.end('{"error":"Request too large"}\n'); return;}
      if (!text.includes('\n')) return;
      handled = true;
      if (retiring) {socket.end('{"error":"Runner is retiring"}\n'); return;}
      if (busy) {socket.end('{"error":"Another memory request is active"}\n'); return;}
      busy = true;
      try {
        const request = JSON.parse(text.slice(0, text.indexOf('\n')));
        const controlled = runControl && !['info', 'retire'].includes(request.command);
        if (controlled) await runControl.begin('memory ' + request.command);
        const result = await dispatch(request);
        if (controlled) await runControl.end('memory ' + request.command);
        busy = false;
        socket.end(JSON.stringify(result) + '\n');
        if (request.command === 'retire') {server.close(); finish();}
      } catch (error) {
        if (error instanceof StopRequested) {
          if (!socket.writableEnded) socket.end('{"stopping":true}\n');
          server.close();
          finish();
          return;
        }
        state.error = error.stack;
        const entry = state.operations.find(item => item.id === state.currentOperation);
        if (entry) entry.error = error.message;
        delete state.currentOperation;
        const unsafe = state.pending || state.operations.some(item => item.pending && !item.restored) || !await s.ready();
        await save();
        socket.end(JSON.stringify({error: error.message, nativeResult: error.nativeResult, retained: !unsafe}) + '\n');
        if (unsafe) {server.close(); fail(error);}
      } finally {
        busy = false;
        if (retiring) {server.close(); finish();}
      }
    });
  });
  server.on('error', fail);
  const stop = () => {
    retiring = true;
    if (!busy) {server.close(); finish();}
  };
  context.bootstrap = {read, writeAt, select, getPlan: () => activePlan};
  try {
    await new Promise((resolve, reject) => {server.once('error', reject); server.listen(socketPath, resolve);});
    await fs.chmod(socketPath, 0o600);
    state.ready = true;
    await save();
    console.log(JSON.stringify({stage: 'memory-ready', socketPath, file, plan: state.plan}));
    runControl?.events.on('stop', stop);
    if (runControl?.state.mode === 'stopping') stop();
    await done;
  } finally {
    runControl?.events.off('stop', stop);
    state.ready = false;
    if (server.listening) server.close();
    await fs.rm(socketPath, {force: true});
    await save();
  }
}

module.exports = {serve};
