const crypto = require('node:crypto');
const path = require('node:path');
const {once} = require('node:events');
const {streamZip} = require('./zip.cjs');
const {virtualPath} = require('../../core/console/host/ftp-native.cjs');

function create({backend, now = Date.now}) {
  const records = new Map();
  const publicRecord = record => ({id: record.id, name: record.name, status: record.status,
    bytes: record.bytes, totalBytes: record.totalBytes, files: record.files, currentFile: record.currentFile, error: record.error});
  function list() {return [...records.values()].slice(-30).map(publicRecord);}
  async function prepare(value) {
    for (const [id, record] of records) if (record.expires < now() && record.status !== 'downloading' && record.status !== 'starting') records.delete(id);
    value = virtualPath('/', value);
    const state = await backend.request('status');
    if (state.status !== 'ready' || state.mode !== 'storage') throw Error('Start SD & console storage before downloading files');
    const stat = await backend.request('stat', {path: value});
    const id = crypto.randomBytes(32).toString('hex'), folder = stat.type === 'dir';
    const base = path.posix.basename(value) || 'console-storage';
    const name = base + (folder ? '.zip' : '');
    records.set(id, {id, name, path: value, folder, lease: state.lease, status: 'queued', bytes: 0,
      totalBytes: folder ? null : stat.size, files: 0, currentFile: '', expires: now() + 300000, controller: new AbortController()});
    for (const [key, record] of records) if (records.size > 100 && !['queued', 'downloading'].includes(record.status)) records.delete(key);
    return {id, name, url: '/download/' + id + '/' + encodeURIComponent(name)};
  }
  async function send(req, res, id) {
    const record = records.get(id);
    if (!record || record.status !== 'queued' || record.expires < now()) {
      res.writeHead(410, {'content-type': 'text/plain'}); res.end('Download link expired. Start it again.'); return;
    }
    record.status = 'starting';
    const signal = record.controller.signal;
    let token, heartbeat;
    const stop = () => {if (!res.writableFinished) record.controller.abort();};
    const abort = () => res.destroy();
    signal.addEventListener('abort', abort, {once: true});
    res.once('close', stop);
    try {
      if (req.headers.range) throw Error('Download resume is not supported. Start a fresh download.');
      const retained = await backend.request('retain', {path: record.path, lease: record.lease}); token = retained.token;
      if ((retained.stat.type === 'dir') !== record.folder) throw Error('The selected entry changed. Start the download again.');
      if (signal.aborted) throw Error('Download cancelled');
      record.status = 'downloading';
      heartbeat = setInterval(() => {backend.request('download-touch', {token}).catch(() => record.controller.abort());}, 15000);
      const quoted = encodeURIComponent(record.name).replace(/['()*]/g, ch => '%' + ch.charCodeAt(0).toString(16));
      res.setHeader('content-type', record.folder ? 'application/zip' : 'application/octet-stream');
      res.setHeader('content-disposition', 'attachment; filename="' + record.name.replace(/[^A-Za-z0-9._ -]/g, '_') + '"; filename*=UTF-8\'\'' + quoted);
      res.setHeader('accept-ranges', 'none');
      async function write(bytes) {
        if (signal.aborted || res.destroyed) throw Error('Download cancelled');
        if (!res.write(bytes)) await once(res, 'drain', {signal});
      }
      async function* fileData(filename) {
        record.currentFile = filename;
        const file = await backend.request('download-open', {token, path: filename});
        try {
          if (!record.folder && file.size !== record.totalBytes) throw Error('The selected file changed. Start the download again.');
          for (let offset = 0; offset < file.size;) {
            if (signal.aborted) throw Error('Download cancelled');
            const count = Math.min(32768, file.size - offset);
            const chunk = await backend.request('download-read', {token, offset, length: count});
            const bytes = Buffer.from(chunk.data, 'base64');
            if (!bytes.length || bytes.length > count) throw Error('File data was truncated or invalid');
            offset += bytes.length; record.bytes += bytes.length; yield bytes;
          }
          record.files++;
        } finally {await backend.request('download-close', {token});}
      }
      if (record.folder) {
        async function* entries() {
          const prefix = path.posix.basename(record.path) || 'console-storage';
          const pending = [{source: record.path, name: prefix + '/', directory: true}];
          while (pending.length) {
            if (signal.aborted) throw Error('Download cancelled');
            const entry = pending.pop();
            if (entry.directory) {
              yield entry;
              const {entries: children} = await backend.request('download-list', {token, path: entry.source});
              for (const child of children.reverse()) {
                if (!child.name || /[\x00-\x1f\x7f/\\]/.test(child.name) || ['.', '..'].includes(child.name)) throw Error('Invalid archive entry name');
                const source = (entry.source === '/' ? '' : entry.source) + '/' + child.name;
                const name = entry.name + child.name + (child.type === 'dir' ? '/' : '');
                if (child.type === 'dir') pending.push({source, name, directory: true});
                else pending.push({name, data: () => fileData(source)});
              }
            } else yield entry;
          }
        }
        await streamZip(entries(), write, {signal});
      } else {
        record.totalBytes = retained.stat.size;
        res.setHeader('content-length', retained.stat.size);
        for await (const bytes of fileData(record.path)) await write(bytes);
      }
      if (signal.aborted) throw Error('Download cancelled');
      await new Promise((resolve, reject) => {res.once('error', reject); res.end(resolve);});
      record.status = 'done';
    } catch (error) {
      record.status = signal.aborted ? 'cancelled' : 'failed'; record.error = signal.aborted ? 'Download cancelled' : error.message;
      if (res.headersSent) res.destroy();
      else {res.writeHead(400, {'content-type': 'text/plain; charset=utf-8'}); res.end(record.error);}
    } finally {
      clearInterval(heartbeat); res.off('close', stop); signal.removeEventListener('abort', abort);
      if (token) await backend.request('release', {token}).catch(() => {});
    }
  }
  function cancel(id) {
    const record = records.get(id); if (!record) throw Error('Unknown download');
    if (['queued', 'starting', 'downloading'].includes(record.status)) {record.controller.abort(); if (record.status === 'queued') record.status = 'cancelled';} return {ok: true};
  }
  return {prepare, send, list, cancel, close: () => {for (const record of records.values()) cancel(record.id);}};
}
module.exports = {create};
