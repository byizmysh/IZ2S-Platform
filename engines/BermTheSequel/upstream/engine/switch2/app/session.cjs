// Own allocations and handles in the freshly paired application.
const fs = require('node:fs/promises');
const path = require('node:path');
const {client, live} = require('./rpc.cjs');
const transport = require('../../shared/rpc.cjs');
const functions = require("../../shared/sdk-functions.json");
const {packet, response} = require("../../shared/ipc.cjs");
const u32 = value => {const b = Buffer.alloc(4); b.writeUInt32LE(value); return b;};
const u64 = value => {const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(value)); return b;};
const check = (condition, message) => {if (!condition) throw Error(message);};

class Session {
  constructor(label, bytes = 0x10000) {
    this.label = label;
    this.handles = new Set();
    this.threads = new Set();
    this.base = 0;
    this.bytes = bytes;
    this.log = path.join(__dirname, label + '-events.jsonl');
  }
  async record(entry) {
    await fs.appendFile(this.log, JSON.stringify({at: new Date().toISOString(), ...entry}) + '\n', {mode: 0o600});
  }
  async verifyCode(expected, label = 'SDK helper') {
    const address = live.sdk + expected.offset, size = expected.bytes.length / 2;
    const region = await this.rpc.query(address);
    check(!region.result && region.permissions === 5 && region.address <= address &&
      address + size <= region.address + region.size, 'Code is not in the current executable mapping');
    const file = path.join(__dirname, 'verified-instructions.json');
    if (!this.codeCache) {
      let cached;
      try {cached = JSON.parse(await fs.readFile(file, 'utf8'));}
      catch (error) {if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error;}
      this.codeCache = cached?.session === live.session && cached.sdk === live.sdk ? cached :
        {session: live.session, sdk: live.sdk, code: {}};
    }
    if (this.codeCache.code[expected.offset] === expected.bytes) return;
    check((await this.read(address, size)).toString('hex') === expected.bytes, label + ' instructions differ');
    this.codeCache.code[expected.offset] = expected.bytes;
    await fs.writeFile(file, JSON.stringify(this.codeCache, null, 2) + '\n', {mode: 0o600});
  }
  async ready() {
    return live.platform === 2 && (await transport.request({command: 'status'})).ready;
  }
  call(name, ...args) {return this.rpc.call3(live.sdk + functions[name].offset, ...args);}
  read(address, bytes) {return this.rpc.read(address, bytes);}
  // Callers must wait for any IPC operation touching this range to finish.
  async readOwned(address, bytes) {
    check(this.base && Number.isInteger(bytes) && bytes > 0 && bytes <= 524288 &&
      Number.isSafeInteger(address) && address >= this.base && address + bytes <= this.base + this.bytes,
      'Owned read is outside the verified allocation');
    return this.rpc.read(address, bytes);
  }
  write(address, bytes) {
    if (this.bulkUploads && bytes.length >= 4096 && this.base && address >= this.base &&
        address + bytes.length <= this.base + this.bytes) return require("./bulk-transfer.cjs").write(this, address, bytes);
    return this.rpc.write(address, bytes);
  }
  async initialize() {
    check(await this.ready(), 'Switch 2 application connection is not ready');
    this.rpc = await client();
    for (const name of ['allocate', 'free', 'getService', 'send', 'close', 'memset']) {
      const expected = functions[name];
      await this.verifyCode(expected, name);
    }
    // This slot is inside this process's owned scratch, outside the RPC frames.
    const temporary = live.scratch + 0x5300;
    const region = await this.rpc.query(temporary);
    check(!region.result && region.type === 5 && region.permissions === 3 &&
      !region.attributes && !region.ipcReferences && !region.deviceReferences,
      'Allocation output is not ordinary owned memory');
    const saved = await this.read(temporary, 8);
    try {
      check(!await this.call('allocate', temporary, 4096, this.bytes), 'Allocation failed');
      this.base = Number((await this.read(temporary, 8)).readBigUInt64LE());
    } finally {await this.write(temporary, saved);}
    check(this.base > 0 && this.base % 4096 === 0, 'Invalid allocation');
    const allocated = await this.rpc.query(this.base);
    check(!allocated.result && allocated.type === 5 && allocated.permissions === 3 &&
      !allocated.attributes && Number(BigInt(allocated.address) + BigInt(allocated.size)) >= this.base + this.bytes,
      'New allocation is not ordinary owned memory');
    await this.call('memset', this.base, 0, this.bytes);
    await this.record({operation: 'initialize', session: live.session, base: this.base, bytes: this.bytes});
  }
  async openService(name) {
    check(name.length > 0 && name.length <= 8, 'Invalid service name');
    const values = this.base + 0x1000;
    const bytes = Buffer.alloc(32); bytes.write(name, 8);
    await this.write(values, bytes);
    const result = await this.call('getService', values, values + 8, name.length);
    const handle = result ? null : (await this.readOwned(values, 4)).readUInt32LE();
    await this.record({operation: 'GetService', name, result, handle});
    check(!result && handle > 0 && handle < 0xffff8000, 'Service unavailable: ' + name);
    this.handles.add(handle);
    return handle;
  }
  async ipc(label, handle, command, data = Buffer.alloc(0), options = {}) {
    await this.write(this.base, packet(command, data, options));
    const transport = await this.call('send', this.base, 4096, handle);
    await this.record({operation: label, handle, command, transport});
    check(!transport, label + ': transport 0x' + transport.toString(16));
    const raw = await this.readOwned(this.base, 256);
    const reply = response(raw);
    reply.interfaceId = raw.readUInt32LE(raw.length - reply.data.length - 4);
    for (const child of [...reply.copies, ...reply.moves]) this.handles.add(child);
    await this.record({operation: label + ' reply', result: reply.result, copies: reply.copies,
      moves: reply.moves, interfaceId: reply.interfaceId,
      data: reply.data.subarray(0, 32).toString('hex')});
    return reply;
  }
  async close(handle) {
    check(this.handles.has(handle), 'Handle is not owned');
    const result = await this.call('close', handle, 0, 0);
    await this.record({operation: 'CloseHandle', handle, result});
    check(!result, 'Owned handle did not close');
    this.handles.delete(handle);
  }
  async closeUpload() {}
  async cleanup() {
    await this.closeUpload();
    if (!await this.ready() || this.threads.size) {
      await this.record({operation: 'cleanupDeferred', base: this.base,
        handles: [...this.handles], threads: [...this.threads]});
      return;
    }
    for (const handle of [...this.handles].reverse()) await this.close(handle);
    if (this.base) {
      await this.call('free', this.base, 0, 0);
      await this.record({operation: 'Free', base: this.base});
      this.base = 0;
    }
  }
}

module.exports = {Session, live, u32, u64, check};
