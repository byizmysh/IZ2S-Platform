// Ordinary client IPC, serialized with the existing retained memory runner.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const {check} = require("../memory/session.cjs");
const source = require('node:fs').readFileSync(__filename);
const sha256 = crypto.createHash('sha256').update(source).digest('hex');

async function dispatch(context, request) {
  const {s, live, report, save} = context;
  const domain = require(path.join(__dirname, '../app/domain.cjs'));
  const state = report.nativeRo ||= {session: live.session, notifiers: [], events: [], calls: []};
  if (!state.sources?.some(item => item.sha256 === sha256)) {
    const sourceFile = path.join(__dirname, 'control-' + sha256.slice(0, 12) + '.cjs');
    await fs.writeFile(sourceFile, source, {mode: 0o600});
    (state.sources ||= []).push({sha256, sourceFile});
    await save();
  }
  const command = request.command.slice('root-native-'.length);
  if (command === 'info') return state;
  check(await s.ready(), 'Current native bridge is not ready');
  if (command === 'pm-exceptions') {
    const handle = state.services.find(item => item.name === 'pm:dmnt')?.handle;
    check(s.handles.has(handle), 'The owned PM monitor is missing');
    const output = s.base + 0x4000, size = 0x200;
    await s.write(output, Buffer.alloc(size));
    const reply = await s.ipc('RO process exception check', handle, 0, Buffer.alloc(0),
      {receive: [{address: output, size}]});
    check(!reply.result, 'PM exception enumeration failed');
    const count = reply.data.readUInt32LE();
    check(count <= size / 8, 'PM exception count exceeds the output');
    const bytes = await s.readOwned(output, size), pids = [];
    for (let i = 0; i < count; i++) pids.push(bytes.readBigUInt64LE(i * 8).toString());
    const result = {at: new Date().toISOString(), count, pids};
    (state.exceptionChecks ||= []).push(result); await save();
    return result;
  }
  if (command === 'event-signal' || command === 'event-clear') {
    const handle = Number(request.handle);
    check(state.events.some(e => e.handle === handle) && s.handles.has(handle), 'Event operation requires an owned exported handle');
    const bytes = command === 'event-signal' ? '210200d4c0035fd6' : '410200d4c0035fd6';
    state.eventCode ||= {};
    if (!state.eventCode[command]) {
      const offset = 0x1582fc, code = await s.read(live.sdk + offset, 0x120);
      const found = code.indexOf(Buffer.from(bytes, 'hex'));
      check(found >= 0 && found % 4 === 0 && found === code.lastIndexOf(Buffer.from(bytes, 'hex')),
        'The native event syscall wrapper is not unique');
      state.eventCode[command] = {offset: offset + found, bytes};
    }
    await s.verifyCode(state.eventCode[command], command);
    const result = await s.rpc.call3(live.sdk + state.eventCode[command].offset, handle, 0, 0);
    const entry = {at: new Date().toISOString(), operation: command, handle, result};
    state.calls.push(entry); await save();
    return entry;
  }
  if (command.startsWith('code-') || command.startsWith('file-') || command.startsWith('donor-'))
    return require("./files.cjs").dispatch(context, request, state, domain);
  if (command === 'browser-cache-capture') {
    const serviceHandle = name => state.services?.find(item => item.name === name &&
      s.handles.has(item.handle))?.handle;
    return require('./browser-cache-readonly.cjs').capture(context,
      {session: live.session, rootHandle: report.ownedRoot?.clones?.[0],
        rootMode: 'non-domain', rootInitialized: report.ownedRoot?.initialized === true,
        nsVmHandle: serviceHandle('ns:vm'),
        timeStaticHandle: serviceHandle('time:su') ?? serviceHandle('time:s')},
      {filesystem: require('../app/filesystem.cjs')});
  }
  if (command === 'browser-systemdata-capture')
    return require('./browser-systemdata-readonly.cjs').capture(context,
      {session: live.session, dataId: request.dataId},
      {filesystem: require('../app/filesystem.cjs')});
  if (command === 'ncm-list-system')
    return require("./ncm.cjs").listSystem(context, request, state);
  if (command.startsWith('worker-'))
    return require("./worker-control.cjs").dispatch(context, request, state, domain);
  if (command.startsWith('process-test-'))
    return require("./process-test.cjs").dispatch(context, request, state);
  if (command === 'locator-open') {
    const source = Number(request.handle);
    check([...(report.ownedRoot?.clones || []), ...state.notifiers].includes(source) && s.handles.has(source),
      'Only our root or notifier can be located through a temporary domain');
    const cloned = await domain.send(s, 'Clone owned object for location', source, domain.wire.control(2));
    const handle = domain.oneHandle(cloned);
    const entry = {at: new Date().toISOString(), handle, source, locator: true};
    (state.domainCopies ||= []).push(entry); await save();
    try {
      const converted = await domain.send(s, 'Convert locator clone to domain', handle, domain.wire.control(0));
      check(!converted.result, 'Locator domain conversion failed');
      entry.root = converted.data.readUInt32LE();
      check(entry.root > 0 && entry.root <= 0x4000, 'Locator ID exceeds the native domain capacity');
      await save();
      return entry;
    } catch (error) {
      await s.close(handle);
      state.domainCopies = state.domainCopies.filter(item => item.handle !== handle); await save();
      throw error;
    }
  }
  if (command === 'domain-open') {
    if (!state.inspectionDomain) {
      state.inspectionDomain = await domain.openDomain(s, 'RO pivot: owned inspection domain');
      await save();
    }
    return state.inspectionDomain;
  }
  if (command === 'domain-copy') {
    const handle = Number(request.handle), object = Number(request.object);
    check(state.inspectionDomain?.handle === handle && s.handles.has(handle),
      'Copy requires the owned inspection domain');
    const input = Buffer.alloc(4); input.writeUInt32LE(object);
    const reply = await domain.send(s, 'RO pivot: copy owned domain object', handle, domain.wire.control(1, input));
    const copy = domain.oneHandle(reply);
    const entry = {at: new Date().toISOString(), handle: copy, domainHandle: handle, object};
    (state.domainCopies ||= []).push(entry);
    await save();
    return entry;
  }
  if (command === 'notifier-open') {
    if (state.notifiers.length) return state;
    const root = report.ownedRoot?.clones[0];
    check(report.ownedRoot?.initialized && s.handles.has(root), 'Dedicated root is not retained');
    const reply = await s.ipc('RO pivot: OpenSystemDataUpdateEventNotifier', root, 510);
    const entry = {at: new Date().toISOString(), operation: command, result: reply.canonicalResult};
    state.calls.push(entry);
    await save();
    check(!reply.result, 'System-data notifier creation failed: ' + reply.canonicalResult);
    state.notifiers.push(domain.oneHandle(reply));
    await save();
    return state;
  }
  if (command === 'notifier-clones') {
    const count = Number(request.count);
    check(Number.isInteger(count) && count >= 1 && count <= 12 && state.notifiers.length,
      'Expected an existing notifier and clone count 1..12');
    while (state.notifiers.length < count) {
      const reply = await domain.send(s, 'RO pivot: clone notifier', state.notifiers[0], domain.wire.control(2));
      state.notifiers.push(domain.oneHandle(reply));
      await save();
    }
    while (state.notifiers.length > count) {
      await s.close(state.notifiers.at(-1));
      state.notifiers.pop();
      await save();
    }
    return {handles: state.notifiers};
  }
  if (command === 'notifier-event') {
    check(state.notifiers.length && s.handles.has(state.notifiers[0]), 'Notifier is not retained');
    const reply = await s.ipc('RO pivot: raw GetEventHandle', state.notifiers[0], 0);
    check(!reply.result && reply.copies.length === 1 && !reply.moves.length, 'Expected one copied handle');
    const entry = {at: new Date().toISOString(), handle: reply.copies[0]};
    state.events.push(entry);
    await save();
    return entry;
  }
  if (command === 'ipc') {
    const handle = Number(request.handle), id = Number(request.id);
    check(s.handles.has(handle) && Number.isInteger(id) && id >= 0 && id <= 0xffffffff,
      'IPC requires a retained handle and valid command');
    check(typeof request.hex === 'string' && /^(?:[0-9a-fA-F]{2})*$/.test(request.hex) &&
      request.hex.length <= 384, 'IPC input must be at most 192 hex-encoded bytes');
    const reply = await s.ipc('RO pivot: client command ' + id, handle, id, Buffer.from(request.hex, 'hex'));
    const entry = {at: new Date().toISOString(), operation: command, handle, id,
      result: reply.result, canonicalResult: reply.canonicalResult, copies: reply.copies,
      moves: reply.moves, hex: reply.data.toString('hex')};
    state.calls.push(entry);
    await save();
    return entry;
  }
  if (command === 'sm-service') {
    const handle = Number(request.handle), name = request.name;
    check(s.handles.has(handle) && typeof name === 'string' && /^[a-z0-9:-]{1,8}$/.test(name),
      'SM lookup requires an owned handle and service name');
    const bytes = Buffer.alloc(256);
    bytes.writeUInt32LE(0x11); bytes.writeUInt32LE(2, 4); bytes.write(name, 8);
    await s.write(s.base, bytes);
    const transport = await s.call('send', s.base, 4096, handle);
    const entry = {at: new Date().toISOString(), operation: command, handle, name, transport};
    state.calls.push(entry);
    await save();
    if (transport) return entry;
    const raw = await s.readOwned(s.base, 256);
    entry.raw = raw.subarray(0, 64).toString('hex');
    await save();
    const first = raw.readUInt32LE(), second = raw.readUInt32LE(4);
    check((first >>> 16) === 0 && (second & 0x3ff) >= 1, 'Unexpected TIPC response descriptors');
    let at = 8;
    const copies = [], moves = [];
    if (second >>> 31) {
      const special = raw.readUInt32LE(at); at += 4;
      check(!(special & 1), 'Unexpected TIPC reply PID');
      for (let i = 0; i < (special >>> 1 & 15); i++, at += 4) copies.push(raw.readUInt32LE(at));
      for (let i = 0; i < (special >>> 5 & 15); i++, at += 4) moves.push(raw.readUInt32LE(at));
    }
    entry.result = raw.readUInt32LE(at);
    entry.canonicalResult = entry.result & 0x3fffff;
    entry.copies = copies; entry.moves = moves;
    for (const child of [...copies, ...moves]) if (child) s.handles.add(child);
    if (!entry.result) {
      check(!copies.length && moves.length === 1, 'SM did not return one moved service handle');
      (state.services ||= []).push({name, handle: moves[0], sm: handle});
    }
    await save();
    return entry;
  }
  if (command === 'close-handle') {
    const handle = Number(request.handle);
    check([...state.events, ...(state.domainCopies || []), ...(state.services || [])].some(item => item.handle === handle),
      'Only exported or copied local handles may be closed here');
    await s.close(handle);
    state.events = state.events.filter(item => item.handle !== handle);
    if (state.domainCopies) state.domainCopies = state.domainCopies.filter(item => item.handle !== handle);
    if (state.services) state.services = state.services.filter(item => item.handle !== handle);
    await save();
    return {closed: handle};
  }
  if (command === 'thread-id' || command === 'wait-zero' || command === 'process-info') {
    const handle = Number(request.handle);
    check(s.handles.has(handle), 'Syscall requires a retained local handle');
    const offset = command === 'thread-id' ? 0x15847c : command === 'wait-zero' ? 0x1583fc : 0x1584ac;
    const expected = command === 'thread-id' ? 'e00f1ff8a10400d4e20340f9410000f9ff430091c0035fd6' :
      command === 'wait-zero' ? 'e00f1ff8010300d4e20340f9410000b9ff430091c0035fd6' :
      'e00f1ff8210500d4e20340f9410000f9ff430091c0035fd6';
    await s.verifyCode({offset, bytes: expected}, command);
    const output = s.base + 0x1000;
    const input = Buffer.alloc(16); input.writeUInt32LE(handle, 8);
    await s.write(output, input);
    let result;
    if (command === 'thread-id') result = await s.rpc.call3(live.sdk + offset, output, handle, 0);
    else if (command === 'wait-zero') result = await s.rpc.call4(live.sdk + offset, output, output + 8, 1, 0);
    else {
      const type = Number(request.type);
      check(Number.isInteger(type) && type >= 0 && type <= 31, 'Invalid process-info type');
      result = await s.rpc.call4(live.sdk + offset, output, type, handle, 0);
    }
    const value = (await s.readOwned(output, 8)).readBigUInt64LE().toString();
    const entry = {at: new Date().toISOString(), operation: command, handle, result, value};
    state.calls.push(entry);
    await save();
    return entry;
  }
  throw Error('Unknown native RO command: ' + command);
}

module.exports = {dispatch};
