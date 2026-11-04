// Explicit SD mutations. Uploads publish only after complete, flushed transfers.
const crypto = require('node:crypto'), path = require('node:path');
const {virtualPath} = require('../../core/console/host/ftp-native.cjs');
function sdPath(value) {
  if (typeof value !== 'string' || !value.startsWith('/')) throw Error('Choose an absolute SD path');
  value = virtualPath('/', value);
  if (!value.startsWith('/sd/')) throw Error('Changes are supported inside /sd only');
  return value;
}
function create({getStorage, lease, busy = () => false, now = Date.now}) {
  const uploads = new Map();
  async function cancel(token) {
    const record = uploads.get(token); if (!record) return {ok: true};
    if (record.file) {await getStorage().close(record.file); record.file = null;}
    await getStorage().remove(record.temporary, false); uploads.delete(token); return {ok: true};
  }
  async function handle(method, value) {
    const storage = getStorage(); if (!storage) throw Error('Start SD & console storage first');
    for (const [token, record] of uploads) if (record.deadline < now()) await cancel(token);
    if (method === 'sd-expire') return {ok: true};
    if (method === 'sd-upload-cancel') return cancel(value.token);
    if (method === 'sd-upload-chunk' || method === 'sd-upload-finish') {
      const record = uploads.get(value.token); if (!record || record.lease !== lease()) throw Error('Upload session expired');
      record.deadline = now() + 120000;
      if (method === 'sd-upload-chunk') {
        if (value.offset !== record.offset || typeof value.data !== 'string' || value.data.length > 43692 || !/^[A-Za-z0-9+/]*={0,2}$/.test(value.data)) throw Error('Invalid upload chunk');
        const bytes = Buffer.from(value.data, 'base64');
        if (!bytes.length || bytes.length > 32768 || record.offset + bytes.length > record.size) throw Error('Invalid upload length');
        await storage.write(record.file, record.offset, bytes); record.offset += bytes.length;
        return {bytes: record.offset};
      }
      if (record.offset !== record.size) throw Error('Upload is incomplete');
      await storage.flush(record.file); await storage.close(record.file); record.file = null;
      await storage.rename(record.temporary, record.path); uploads.delete(value.token);
      return {ok: true, path: record.path, bytes: record.size};
    }
    if (busy() || uploads.size) throw Error('Wait for active transfers or cancel them before changing files');
    if (value.lease !== lease()) throw Error('The export changed. Refresh the folder before changing files');
    const target = sdPath(value.path);
    if (method === 'sd-upload-begin') {
      if (value.lease !== lease()) throw Error('The export changed. Start the upload again');
      if (!Number.isSafeInteger(value.size) || value.size < 0) throw Error('Invalid upload size');
      const names = await storage.list(path.posix.dirname(target));
      if (names.some(entry => entry.name === path.posix.basename(target))) throw Error('A file or folder with that name already exists');
      const token = crypto.randomBytes(24).toString('hex'), temporary = path.posix.dirname(target) + '/.ns2-upload-' + token + '.part';
      const file = await storage.openWrite(temporary, value.size);
      uploads.set(token, {path: target, temporary, file, size: value.size, offset: 0, lease: lease(), deadline: now() + 120000});
      return {token, bytes: 0};
    }
    if (method === 'sd-mkdir') {
      const parts = target.slice(4).split('/'); let parent = '/sd';
      for (let i = 0; i < parts.length; i++) {
        const names = await storage.list(parent), existing = names.find(entry => entry.name === parts[i]);
        parent += '/' + parts[i];
        if (existing) {if (existing.type !== 'dir' || (!value.parents && i === parts.length - 1)) throw Error('A file or folder with that name already exists');}
        else {if (!value.parents && i !== parts.length - 1) throw Error('Parent folder does not exist'); await storage.mkdir(parent);}
      }
      return {ok: true};
    }
    if (method === 'sd-rename') {
      const to = sdPath(value.to);
      if (target === to) return {ok: true};
      if (to.startsWith(target + '/')) throw Error('A folder cannot be moved inside itself');
      const names = await storage.list(path.posix.dirname(to));
      if (names.some(entry => entry.name === path.posix.basename(to))) throw Error('A file or folder with that name already exists');
      await storage.rename(target, to); return {ok: true};
    }
    if (method === 'sd-delete') {
      if (value.confirmed !== true) throw Error('Confirm permanent deletion first');
      const stat = await storage.stat(target); await storage.remove(target, stat.type === 'dir', true); return {ok: true};
    }
    throw Error('Unknown SD action');
  }
  return {handle, count: () => uploads.size, close: async () => {for (const token of [...uploads.keys()]) await cancel(token);}};
}
module.exports = {create, sdPath};
