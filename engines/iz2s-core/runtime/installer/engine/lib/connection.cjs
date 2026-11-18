const net = require('node:net');

const HELLO_BYTES = 128;
const MAGIC = 0x31545344;

// TCP may split a record anywhere. A timeout ends the connection; requests
// cannot be replayed because a native operation might already have completed.
class Connection {
  constructor(socket, timeout = 120000) {
    this.socket = socket;
    this.timeout = timeout;
    this.buffer = Buffer.alloc(0);
    socket.setNoDelay(true);
    socket.on('data', bytes => {
      this.buffer = Buffer.concat([this.buffer, bytes]);
      if (this.buffer.length > 0x100000) this.fail(Error('Unexpected console output'));
      this.drain();
    });
    socket.on('error', error => this.fail(error));
    socket.on('end', () => this.fail(Error('Console disconnected')));
    socket.on('close', () => this.fail(Error('Console connection closed')));
  }

  fail(error) {
    this.error ||= error;
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(this.error);
      this.pending = null;
    }
    this.socket.destroy();
  }

  drain() {
    const pending = this.pending;
    if (!pending || this.buffer.length < pending.size) return;
    const bytes = this.buffer.subarray(0, pending.size);
    this.buffer = this.buffer.subarray(pending.size);
    clearTimeout(pending.timer);
    this.pending = null;
    pending.resolve(bytes);
  }

  read(size) {
    if (!Number.isSafeInteger(size) || size < 1 || size > 0x100000)
      return Promise.reject(Error('Invalid response length'));
    if (this.error) return Promise.reject(this.error);
    if (this.pending) return Promise.reject(Error('A console response is already pending'));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => this.fail(Error('Console response timed out; restart the application')), this.timeout);
      this.pending = {size, resolve, reject, timer};
      this.drain();
    });
  }

  write(bytes) {
    if (this.error) return Promise.reject(this.error);
    return new Promise((resolve, reject) => {
      this.socket.write(bytes, error => {
        if (error) {this.fail(error); reject(error);}
        else resolve();
      });
    });
  }

  close() { this.fail(Error('Connection closed')); }
}

function parseHello(bytes) {
  if (bytes.length !== HELLO_BYTES) throw Error('Incomplete console greeting');
  const words = Array.from({length: 16}, (_, index) => bytes.readBigUInt64LE(index * 8));
  if (words.some(value => value > BigInt(Number.MAX_SAFE_INTEGER)))
    throw Error('Console greeting contains an imprecise address');
  const values = words.map(Number);
  if (values[0] !== MAGIC || values[1] !== 1 || values[2] !== 2)
    throw Error('Wrong launcher protocol or console type');
  const names = ['scratch', 'scratchBytes', 'main', 'sdk', 'got', 'memmove', 'backing', 'controller'];
  const addresses = Object.fromEntries(names.map((name, index) => [name, values[index + 3]]));
  const {scratch, scratchBytes, main, sdk, got, memmove, backing, controller} = addresses;
  for (const address of [scratch, main, sdk, got, memmove, backing, controller]) {
    if (address < 0x1000 || address >= 2 ** 39 || address % 8)
      throw Error('Invalid fresh process address');
  }
  if (scratchBytes !== 0x80000 || scratch + scratchBytes >= 2 ** 39 ||
      scratch % 16 || main % 4096 || sdk % 4096 ||
      got !== main + 0x9c92f0 || memmove !== sdk + 0x5872f0 ||
      values[15] !== 3925528160)
    throw Error('Unsupported SmileBASIC SDK or bootstrap layout');
  return {...addresses, platform: 2, sdkCrc32: values[15]};
}

async function listen({host, port, onWaiting, timeout = 10 * 60 * 1000}) {
  if (!net.isIPv4(host)) throw Error('Use the computer\'s LAN IPv4 address');
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw Error('Port must be 1024..65535');
  const pending = new Set();
  const server = net.createServer();
  let timer, finished = false;
  return new Promise((resolve, reject) => {
    const stop = () => {finished = true; clearTimeout(timer); server.close();};
    const fail = error => {
      if (finished) return;
      stop();
      for (const connection of pending) connection.close();
      reject(error);
    };
    server.on('error', fail);
    server.on('connection', socket => {
      if (finished || pending.size >= 4) {socket.destroy(); return;}
      const connection = new Connection(socket, 10000);
      pending.add(connection);
      connection.read(HELLO_BYTES).then(bytes => {
        if (finished) throw Error('Pairing has ended');
        const addresses = parseHello(bytes);
        pending.delete(connection);
        stop();
        for (const other of pending) other.close();
        connection.timeout = 120000;
        resolve({connection, addresses});
      }).catch(() => {pending.delete(connection); connection.close();});
    });
    timer = setTimeout(() => fail(Error('No console connected before the pairing deadline')), timeout);
    server.listen(port, host, () => {
      try {onWaiting?.();}
      catch (error) {fail(error);}
    });
  });
}

module.exports = {Connection, HELLO_BYTES, MAGIC, parseHello, listen};
