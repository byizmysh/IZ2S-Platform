'use strict';
// IZ2S Core transport. The on-wire ABI remains compatible with NS2IZM.
const net = require('node:net');
const HELLO_BYTES = 128, MAGIC = 0x31545344, LIMIT = 0x100000;
class Connection {
  constructor(socket, timeout = 120000) {
    Object.assign(this, {socket, timeout, chunks:[], available:0});
    socket.setNoDelay(true);
    socket.on('data', chunk => {
      if (this.error) return;
      if (chunk.length + this.available > LIMIT) return this.fail(Error('Unexpected console output'));
      this.chunks.push(chunk); this.available += chunk.length; this.drain();
    });
    for (const [event, message] of [['end','Console disconnected'], ['close','Console connection closed']])
      socket.on(event, () => this.fail(Error(message)));
    socket.on('error', error => this.fail(error));
  }
  get buffer() {return Buffer.concat(this.chunks, this.available);}
  fail(error) {
    this.error ||= error;
    const pending = this.pending; this.pending = null;
    this.chunks.length = 0; this.available = 0;
    if (pending) {clearTimeout(pending.timer); pending.reject(this.error);}
    this.socket.destroy();
  }
  drain() {
    const pending = this.pending;
    if (!pending || this.available < pending.size) return;
    const result = Buffer.allocUnsafe(pending.size);
    let offset = 0;
    while (offset < result.length) {
      const first = this.chunks[0], count = Math.min(first.length, result.length - offset);
      first.copy(result, offset, 0, count); offset += count;
      if (count === first.length) this.chunks.shift(); else this.chunks[0] = first.subarray(count);
    }
    this.available -= result.length; this.pending = null; clearTimeout(pending.timer); pending.resolve(result);
  }
  read(size) {
    if (!Number.isSafeInteger(size) || size < 1 || size > LIMIT) return Promise.reject(Error('Invalid response length'));
    if (this.error) return Promise.reject(this.error);
    if (this.pending) return Promise.reject(Error('A console response is already pending'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => this.fail(Error('Console response timed out; restart the application')), this.timeout);
      this.pending = {size, resolve, reject, timer}; this.drain();
    });
  }
  write(bytes) {
    if (this.error) return Promise.reject(this.error);
    return new Promise((resolve, reject) => {
      this.socket.write(bytes, error => {
        if (error) {this.fail(error); reject(error);} else resolve();
      });
    });
  }
  close() {this.fail(Error('Connection closed'));}
}
function parseHello(bytes) {
  if (bytes.length !== HELLO_BYTES) throw Error('Incomplete console greeting');
  const values = [];
  for (let offset = 0; offset < bytes.length; offset += 8) {
    const word = bytes.readBigUInt64LE(offset);
    if (word > BigInt(Number.MAX_SAFE_INTEGER)) throw Error('Console greeting contains an imprecise address');
    values.push(Number(word));
  }
  if (values[0] !== MAGIC || values[1] !== 1 || values[2] !== 2) throw Error('Wrong launcher protocol or console type');
  const keys = ['scratch','scratchBytes','main','sdk','got','memmove','backing','controller'];
  const a = Object.fromEntries(keys.map((key, index) => [key, values[3 + index]]));
  for (const key of keys.filter(key => key !== 'scratchBytes'))
    if (a[key] < 0x1000 || a[key] >= 2 ** 39 || a[key] % 8) throw Error('Invalid fresh process address');
  if (a.scratchBytes !== 0x80000 || a.scratch + a.scratchBytes >= 2 ** 39 || a.scratch % 16 ||
      a.main % 4096 || a.sdk % 4096 || a.got !== a.main + 0x9c92f0 ||
      a.memmove !== a.sdk + 0x5872f0 || values[15] !== 3925528160)
    throw Error('Unsupported SmileBASIC SDK or bootstrap layout');
  return {...a, platform:2, sdkCrc32:values[15]};
}
function listen({host, port, onWaiting, signal, timeout = 600000}) {
  if (!net.isIPv4(host)) throw Error("Use the computer's LAN IPv4 address");
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw Error('Port must be 1024..65535');
  return new Promise((resolve, reject) => {
    const server = net.createServer(), peers = new Set();
    let settled = false, deadline;
    const finish = (error, result) => {
      if (settled) return; settled = true; clearTimeout(deadline);
      signal?.removeEventListener('abort', abort); server.close(() => {});
      for (const peer of peers) if (peer !== result?.connection) peer.close();
      peers.clear(); error ? reject(error) : resolve(result);
    };
    const abort = () => finish(Error('Console listener cancelled'));
    if (signal?.aborted) return abort();
    signal?.addEventListener('abort', abort, {once:true});
    server.on('error', error => finish(error));
    server.on('connection', socket => {
      if (settled || peers.size >= 4) return socket.destroy();
      const connection = new Connection(socket, 10000); peers.add(connection);
      connection.read(HELLO_BYTES).then(bytes => {
        if (settled) return connection.close();
        const addresses = parseHello(bytes); connection.timeout = 120000;
        finish(null, {connection, addresses});
      }).catch(() => {peers.delete(connection); connection.close();});
    });
    deadline = setTimeout(() => finish(Error('No console connected before the pairing deadline')), timeout);
    server.listen(port, host, () => {try {onWaiting?.();} catch (error) {finish(error);}});
  });
}
module.exports = {Connection, HELLO_BYTES, MAGIC, parseHello, listen};
