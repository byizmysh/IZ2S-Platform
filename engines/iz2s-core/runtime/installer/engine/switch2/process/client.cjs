// Serial, bounded IPC to the native RO process. Every reply carries its kernel PID.
const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("../ro/export-handle.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
const read = file => fs.readFile(file, 'utf8').then(JSON.parse);
const words = bytes => Array.from({length: Math.floor(bytes.length / 8)}, (_, i) => bytes.readBigUInt64LE(i * 8));

class Client {
  async save() {
    await fs.writeFile(this.file, JSON.stringify(this.record, null, 2) + '\n', {mode: 0o600});
  }
  async worker(request, timeoutMs = 4000) {
    check(!this.pending, 'A native request remains pending');
    check(Number.isInteger(timeoutMs) && timeoutMs >= 100 && timeoutMs <= 30000, 'Invalid worker deadline');
    this.pending = true;
    await this.memory.request({command: 'root-native-worker-prepare', ...request});
    await this.memory.request({command: 'root-native-worker-release'});
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const state = await this.memory.request({command: 'root-native-worker-state'});
      if (state.finished) {
        const reply = await this.memory.request({command: 'root-native-worker-join'});
        this.pending = false;
        return request.candidates ? reply.responses : reply.responses[0];
      }
      check(Date.now() < deadline, 'Native worker is pending; preserve evidence and recover before further IPC');
      await new Promise(resolve => setTimeout(resolve, 25));
    }
  }
  async open() {
    this.memory = await connect();
    const execution = await read(path.join(__dirname, '../ro/current-execution.json'));
    check(execution.verified && execution.session === this.memory.info.session && execution.report === this.memory.reportFile,
      'The current boot needs a verified RO process');
    this.pid = execution.pid;
    this.record = {session: execution.session, report: execution.report, roPid: execution.pid,
      payloadSha256: execution.payloadSha256, started: new Date().toISOString(), calls: []};
    this.file = path.join(__dirname, execution.session + '-process-probe-' + Date.now() + '.json');
    await this.save();
    const state = await this.memory.request({command: 'root-native-info'});
    check(!state.worker || state.worker.joined, 'Another native worker is active');
    const sm = state.services.find(s => s.name === 'pm:shell')?.sm;
    check(sm, 'The borrowed initialized FS SM client is missing');
    this.record.lookup = await this.worker({kind: 'sm-get', handle: sm, name: 'ro:dmnt'});
    await this.save();
    check(!this.record.lookup.transport && !this.record.lookup.result, 'RO service lookup failed');
    this.handle = this.record.lookup.handle;
    return this;
  }
  async fsProcess() {
    const copy = await exportHandle(this.memory, 0xffff8001, 'FS process for native RO mapping');
    this.record.fsCopy = copy;
    await this.save();
    return copy.handle;
  }
  async service(name) {
    const state = await this.memory.request({command: 'root-native-info'});
    const sm = state.services.find(s => s.name === 'pm:shell')?.sm;
    const reply = await this.worker({kind: 'sm-get', handle: sm, name});
    (this.record.services ||= []).push({name, reply}); await this.save();
    check(!reply.transport && !reply.result && reply.handle, 'Service lookup failed: ' + name);
    return reply.handle;
  }
  async request(id, handle, values) {
    check(values.length === (id === 0x524f0014 ? 8 : 4), 'The process command has an incorrect input size');
    const input = Buffer.alloc(values.length * 8);
    values.forEach((value, i) => input.writeBigUInt64LE(BigInt(value), 8 * i));
    const call = {id, handle, input: values.map(String), started: new Date().toISOString()};
    this.record.calls.push(call); await this.save();
    call.reply = await this.worker({kind: 'ro-process', handle: this.handle, processHandle: handle,
      id, hex: input.toString('hex')});
    call.finished = new Date().toISOString(); await this.save();
    check(!call.reply.transport && call.reply.kernelPid === this.pid, 'Reply did not come from the verified RO kernel PID');
    const data = Buffer.from(call.reply.data, 'hex');
    return {result: call.reply.result, data, values: words(data)};
  }
  async info(handle, address = 0n, pid = 0n) {
    const reply = await this.request(0x524f0010, handle, [pid, address, 0, 0]), v = reply.values;
    if (reply.result) return {result: reply.result, pid: null, programId: null,
      identityResults: {pid: Number(v[2] & 0xffffffffn), program: Number(v[2] >> 32n)}};
    return {result: reply.result, pid: v[0].toString(), programId: v[1].toString(16).padStart(16, '0'),
      queryResult: Number(v[3]), memory: {address: v[4].toString(), size: v[5].toString(),
        type: Number(v[6] & 0xffffffffn), attributes: Number(v[6] >> 32n),
        permissions: Number(v[7] & 0xffffffffn), ipcReferences: Number(v[7] >> 32n), deviceReferences: Number(v[8])},
      addressSpace: v[9].toString(), addressSpaceSize: v[10].toString(),
      freeAddress: v[11].toString(), roFreeAddress: v[12].toString()};
  }
  async read(handle, pid, address, length = 128) {
    const reply = await this.request(0x524f0011, handle, [pid, address, length, 0]), v = reply.values;
    return {result: reply.result, local: v[3].toString(), mapResult: Number(v[4]),
      unmapResult: Number(v[5]), length: Number(v[6]), hex: reply.result ? null : reply.data.subarray(56, 56 + length).toString('hex')};
  }
  async map(handle, pid, source, destination) {
    const reply = await this.request(0x524f0012, handle, [pid, source, destination, 0x1000]);
    return {result: reply.result, mapResult: Number(reply.values[3]),
      permissionResult: Number(reply.values[4]), rollbackResult: Number(reply.values[5])};
  }
  async unmap(handle, pid, source, destination) {
    const reply = await this.request(0x524f0013, handle, [pid, source, destination, 0x1000]);
    return {result: reply.result};
  }
  async patch(handle, pid, address, expected, replacement) {
    check(expected.length === 16 && replacement.length === 16, 'An instruction patch must contain exactly 16 bytes');
    const reply = await this.request(0x524f0014, handle, [pid, address, 16, 0, ...words(expected), ...words(replacement)]);
    const v = reply.values;
    return {result: reply.result, mapResult: Number(v[4]), permissions: Number(v[5] & 0xffffffffn),
      attributes: Number(v[5] >> 32n), expectedMatched: v[6] === 1n,
      readbackMatched: v[7] === 1n, unmapResult: Number(v[8]), local: v[9].toString()};
  }
  async close() {
    if (this.pending) { this.record.pending = true; await this.save(); return; }
    for (const handle of [this.record.fsCopy?.handle, this.handle,
      ...(this.record.services || []).map(s => s.reply.handle)].filter(Boolean))
      await this.memory.request({command: 'root-native-close-handle', handle});
    this.record.closed = new Date().toISOString(); await this.save();
  }
}
module.exports = {Client, check, read};
