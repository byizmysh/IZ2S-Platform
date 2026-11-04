// Lazy promoted filesystems. SD supports explicit writes; internal mounts remain read-only.
const path = require('node:path');
const {virtualPath} = require('../../core/console/host/ftp-native.cjs');
const {success, pathRequest, resultError} = require('../../core/filesystem/ouncecake/engine/protocol.cjs');

async function createStorage(attachment) {
  const {s, root, domain, filesystem} = attachment;
  const mounts = new Map(), unavailable = [];
  for (const [name, command, id] of [['sd', 18], ['system', 11, 31], ['user', 11, 30]]) {
    try {
      let reply;
      if (command === 18) reply = await s.ipc('Open SD filesystem', root, command);
      else {
        const input = Buffer.alloc(4); input.writeUInt32LE(id);
        await s.write(s.base + 0x2000, Buffer.alloc(0x301));
        reply = await domain.send(s, 'Open ' + name + ' filesystem', root, pathRequest(command, input, s.base + 0x2000));
      }
      success(reply, 'Open ' + name);
      mounts.set(name, domain.oneHandle(reply));
    } catch (error) {unavailable.push({name, reason: error.message});}
  }
  if (!mounts.size) throw Error('No SD or console filesystem opened: ' + unavailable.map(item => item.reason).join('; '));
  let queue = Promise.resolve();
  const serial = fn => (...args) => {const task = queue.then(() => fn(...args)); queue = task.catch(() => {}); return task;};
  function resolve(value) {
    value = virtualPath('/', value);
    const [mount, ...parts] = value.slice(1).split('/');
    if (!mounts.has(mount)) throw Error('Filesystem is not available');
    return {handle: mounts.get(mount), path: '/' + parts.join('/')};
  }
  async function list(value) {
    if (value === '/') return [...mounts.keys()].map(name => ({name, type: 'dir', size: 0}));
    const mount = resolve(value), mode = Buffer.alloc(4); mode.writeUInt32LE(3);
    const directory = domain.oneHandle(success(await filesystem.pathMethod(s, mount.handle, 9, mount.path, mode), 'Open directory'));
    const entries = [];
    try {
      while (entries.length <= 200000) {
        const reply = success(await s.ipc('Read directory', directory, 0, Buffer.alloc(0),
          {receive: [{address: s.base + 0x6000, size: 32 * 0x310, mode: 0}]}), 'Read directory');
        const count = Number(reply.data.readBigUInt64LE());
        if (!Number.isInteger(count) || count < 0 || count > 32) throw Error('Invalid directory response');
        if (!count) return entries.sort((a, b) => (a.type === b.type ? 0 : a.type === 'dir' ? -1 : 1) || a.name.localeCompare(b.name));
        const bytes = await filesystem.verifiedBytes(s, s.base + 0x6000, count * 0x310, {directory: mount.path});
        for (let i = 0; i < count; i++) {
          const row = bytes.subarray(i * 0x310, (i + 1) * 0x310);
          const name = row.subarray(0, 0x301).toString('utf8').split('\0')[0];
          if (!name || /[\x00-\x1f\x7f/\\]/.test(name) || ['.', '..'].includes(name)) throw Error('Invalid directory entry');
          const size = Number(row.readBigUInt64LE(0x308));
          if (!Number.isSafeInteger(size)) throw Error('File size exceeds supported precision');
          entries.push({name, type: row[0x304] === 0 ? 'dir' : 'file', size});
        }
      }
      throw Error('Directory exceeds 200000 entries; it was not truncated');
    } finally {await s.close(directory);}
  }
  async function openRead(value) {
    const mount = resolve(value), opened = await filesystem.openFile(s, mount.handle, mount.path);
    if (opened.result) throw resultError('Open file', opened.result);
    try {
      const size = Number(await filesystem.fileSize(s, opened.handle));
      if (!Number.isSafeInteger(size) || size < 0) throw Error('File size exceeds supported precision');
      return {handle: opened.handle, size};
    } catch (error) {await s.close(opened.handle); throw error;}
  }
  function writable(value) {
    value = virtualPath('/', value);
    if (!value.startsWith('/sd/')) throw Error('Changes are supported inside /sd only');
    return resolve(value);
  }
  async function pathCall(command, input, values) {
    const {packet} = require('../../core/filesystem/ouncecake/engine/protocol.cjs');
    const bytes = Buffer.alloc(256), original = packet(command, input);
    bytes.writeUInt32LE(4 | values.length << 16); bytes.writeUInt32LE(original.readUInt32LE(4), 4);
    for (let i = 0; i < values.length; i++) {
      const text = Buffer.from(values[i]); if (text.length >= 0x301) throw Error('Path is too long');
      const address = s.base + 0x2000 + i * 0x400, buffer = Buffer.alloc(0x301); text.copy(buffer); await s.write(address, buffer);
      pathRequest(command, input, address).copy(bytes, 8 + i * 8, 8, 16);
    }
    original.copy(bytes, (8 + values.length * 8 + 15) & ~15, 16, 16 + 16 + input.length);
    return bytes;
  }
  async function mutate(value, command, input = Buffer.alloc(0), second) {
    const mount = writable(value), values = [mount.path];
    if (second) values.push(writable(second).path);
    return success(await domain.send(s, 'SD filesystem change ' + command, mount.handle, await pathCall(command, input, values)), 'SD change');
  }
  const stat = async value => {
    if (value === '/' || [...mounts.keys()].some(name => value === '/' + name)) return {type: 'dir', size: 0};
    const mount = resolve(value);
    const reply = success(await filesystem.pathMethod(s, mount.handle, 7, mount.path), 'Get entry type');
    if (reply.data.readUInt32LE() === 0) return {type: 'dir', size: 0};
    const file = await openRead(value);
    try {return {type: 'file', size: file.size};} finally {await s.close(file.handle);}
  };

  return {
    mounts: [...mounts.keys()], unavailable, writableMounts: mounts.has('sd') ? ['sd'] : [],
    list: serial(list),
    stat: serial(stat),
    openRead: serial(openRead),
    read: serial(async (file, offset, count) => {
      if (!Number.isSafeInteger(offset) || offset < 0 || !Number.isInteger(count) || count < 0) throw Error('Invalid read range');
      const size = Math.min(count, 32768, Math.max(0, file.size - offset));
      return size ? filesystem.readFile(s, file.handle, offset, size) : Buffer.alloc(0);
    }),
    close: serial(file => s.close(file.handle)),
    openWrite: serial(async (value, size = 0) => {
      if (!Number.isSafeInteger(size) || size < 0) throw Error('Invalid file size');
      const input = Buffer.alloc(16); input.writeBigUInt64LE(BigInt(size), 8);
      await mutate(value, 0, input);
      const mode = Buffer.alloc(4); mode.writeUInt32LE(2);
      try {const reply = await mutate(value, 8, mode); return {handle: domain.oneHandle(reply), size, write: true};}
      catch (error) {await mutate(value, 1).catch(() => {}); throw error;}
    }),
    write: serial(async (file, offset, bytes) => {
      if (!file.write || !Number.isSafeInteger(offset) || offset < 0 || !Buffer.isBuffer(bytes) || !bytes.length || bytes.length > 32768 || offset + bytes.length > file.size) throw Error('Invalid write range');
      const address = s.base + 0x6000; await s.write(address, bytes);
      const expected = require('node:crypto').createHash('sha256').update(bytes).digest('hex');
      if (await filesystem.nativeHash(s, address, bytes.length) !== expected) throw Error('Upload data verification failed');
      const input = Buffer.alloc(24); input.writeBigUInt64LE(BigInt(offset), 8); input.writeBigUInt64LE(BigInt(bytes.length), 16);
      success(await s.ipc('Write SD file', file.handle, 1, input, {send: [{address, size: bytes.length, mode: 1}]}), 'Write file');
    }),
    flush: serial(async file => {success(await s.ipc('Flush SD file', file.handle, 2), 'Flush file');}),
    remove: serial(async (value, directory = false, recursive = false) => {await mutate(value, directory ? (recursive ? 4 : 3) : 1);}),
    mkdir: serial(async value => {await mutate(value, 2);}),
    rename: serial(async (from, to) => {writable(from); writable(to); const type = await stat(from); await mutate(from, type.type === 'dir' ? 6 : 5, Buffer.alloc(0), to);}),
    commit: serial(async () => {const handle = mounts.get('sd'); if (!handle) throw Error('SD unavailable'); success(await s.ipc('Commit SD changes', handle, 10), 'Commit SD');}),
  };
}
module.exports = {createStorage};
