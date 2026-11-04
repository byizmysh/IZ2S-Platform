const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const crypto = require('node:crypto');
const lib = require('./library.cjs');
const {saveJson} = require('../../core/session/ledger.cjs');

function createBroker({library = lib, firstPort = 2122, persist = value => saveJson(path.join(lib.ROOT, 'data/state/dolphin-fs-rpc/export.json'), value)} = {}) {
  let state = {status: 'idle'}, task, cancellation, storage, mutations = Promise.resolve(), writes = Promise.resolve();
  const publish = update => {
    state = {...state, ...update, at: new Date().toISOString(), pid: process.pid};
    const snapshot = {...state};
    const next = writes.then(() => persist(snapshot)); writes = next.catch(() => {});
    return next;
  };
  const snapshot = () => ({...state, pid: process.pid});
  let nextPort = firstPort;
  const downloads = new Map();
  async function releaseDownload(token) {
    const record = downloads.get(token); downloads.delete(token);
    if (record?.file && storage) await storage.close(record.file);
  }
  const sd = require('./writes.cjs').create({getStorage: () => storage, lease: () => state.lease, busy: () => downloads.size > 0});
  async function stop(lease, cancelDownloads = false) {
    if (lease && state.lease !== lease) return {...snapshot(), skipped: true};
    if (sd.count() && !cancelDownloads) throw Error('An upload is in progress. Cancel it or wait for completion.');
    if (cancelDownloads) await sd.close();
    if (downloads.size && !cancelDownloads) throw Error('A download is in progress. Cancel it or wait for completion.');
    if (cancelDownloads) for (const token of [...downloads.keys()]) await releaseDownload(token);
    if (task) {await publish({status: 'stopping', progress: 'Releasing game files…'}); cancellation.abort(); await task;}
    return snapshot();
  }
  async function start(title, mode = 'game') {
    title = mode === 'storage' ? 'storage' : library.titleId(title);
    const session = {...await library.findSession()};
    if (task && state.title === title && state.identity === session.identity) return snapshot();
    const games = mode === 'storage' ? [{title, name: 'SD & console storage'}] : await library.gamesFor(session);
    const game = games.find(game => game.title === title);
    if (!game) throw Error('Game not found. Refresh the game list.');
    await stop();
    session.port = library.availablePort ? await library.availablePort(session.host, nextPort) : nextPort;
    nextPort = session.port >= 2199 ? 2122 : session.port + 1;
    const lease = crypto.randomUUID();
    cancellation = new AbortController();
    state = {lease, title, mode, name: game.name, identity: session.identity, session: session.session,
      host: session.host, port: session.port, status: 'preparing', progress: 'Preparing game files…'};
    await publish({});
    const options = {signal: cancellation.signal,
      progress: message => {void publish({progress: message}).catch(() => {});},
      ready: async result => {await publish({status: 'ready', progress: 'Read-only FTP is ready', ...result});},
      storageReady: value => {storage = value;},
    };
    task = (mode === 'storage' ? library.exportStorage(session, options) : library.exportGame(session, title, options))
      .then(() => publish({status: 'closed', progress: 'Filesystem export disconnected'}))
      .catch(error => publish({status: cancellation.signal.aborted && error.code === 'FTP_CANCELLED' ? 'closed' : 'failed', error: error.message}))
      .finally(() => {task = null; storage = null;});
    return snapshot();
  }
  async function handle(request) {
    for (const [token, record] of downloads) if (record.deadline < Date.now()) await releaseDownload(token);
    if (request.method === 'status') return {...snapshot(), downloads: downloads.size, uploads: sd.count()};
    if (request.method === 'list') {
      const session = await library.findSession();
      return {session, games: await library.gamesFor(session, {refresh: request.refresh === true})};
    }
    if (request.method?.startsWith('sd-')) {
      if (state.mode !== 'storage' || state.status !== 'ready' || !storage) throw Error('Start SD & console storage first');
      return sd.handle(request.method, request);
    }
    if (request.method === 'browse' || request.method === 'stat' || request.method === 'retain') {
      if (state.mode !== 'storage' || state.status !== 'ready' || !storage) throw Error('Start the SD & console storage export first');
      const cwd = require('../../core/console/host/ftp-native.cjs').virtualPath('/', request.path || '/');
      if (request.method === 'stat') return {path: cwd, ...await storage.stat(cwd)};
      if (request.method === 'retain') {
        if (sd.count()) throw Error('Wait for the upload to finish before downloading');
        if (state.lease !== request.lease) throw Error('The filesystem export changed. Start the download again.');
        const stat = await storage.stat(cwd), token = crypto.randomBytes(24).toString('hex');
        downloads.set(token, {root: cwd, file: null, deadline: Date.now() + 120000});
        return {token, stat, lease: state.lease};
      }
      return {cwd, entries: await storage.list(cwd), mounts: storage.mounts, unavailable: storage.unavailable, writable: cwd === '/sd' || cwd.startsWith('/sd/')};
    }
    if (request.method === 'release') {await releaseDownload(request.token); return {ok: true};}
    if (request.method?.startsWith('download-')) {
      const record = downloads.get(request.token);
      if (!record || !storage || state.status !== 'ready') throw Error('Download session is no longer available');
      record.deadline = Date.now() + 120000;
      if (request.method === 'download-touch') return {ok: true};
      if (request.method === 'download-read') {
        if (!record.file || !Number.isSafeInteger(request.offset) || request.offset < 0 ||
            !Number.isInteger(request.length) || request.length < 1 || request.length > 32768) throw Error('Invalid download read');
        return {data: (await storage.read(record.file, request.offset, request.length)).toString('base64')};
      }
      if (request.method === 'download-close') {
        const file = record.file; record.file = null; if (file) await storage.close(file); return {ok: true};
      }
      const value = require('../../core/console/host/ftp-native.cjs').virtualPath('/', request.path || '/');
      if (record.root !== '/' && value !== record.root && !value.startsWith(record.root + '/')) throw Error('Download path is outside the selected folder');
      if (request.method === 'download-list') return {entries: await storage.list(value)};
      if (request.method === 'download-open') {
        if (record.file) throw Error('A download file is already open');
        record.file = await storage.openRead(value); return {size: record.file.size};
      }
      throw Error('Unknown download command');
    }
    if (!['start', 'start-storage', 'stop'].includes(request.method)) throw Error('Unknown game service command');
    const action = mutations.then(() => request.method === 'stop' ? stop(request.lease, request.cancelDownloads === true) : start(request.title, request.method === 'start-storage' ? 'storage' : 'game'));
    mutations = action.catch(() => {});
    return action;
  }
  let requests = Promise.resolve();
  const serialHandle = request => {if (request.method === 'status') return Promise.resolve({...snapshot(), downloads: downloads.size, uploads: sd.count()}); const result = requests.then(() => handle(request)); requests = result.catch(() => {}); return result;};
  return {handle: serialHandle, snapshot, close: () => serialHandle({method: 'stop', cancelDownloads: true})};
}

async function serve() {
  const {socketPath} = require('./control.cjs');
  try {
    await require('./control.cjs').requestRaw('status', {}, 1000);
    return;
  } catch (error) {
    if (!['ENOENT', 'ECONNREFUSED'].includes(error.code)) throw error;
    if (error.code === 'ECONNREFUSED' && process.platform !== 'win32') {
      const stat = await fs.lstat(socketPath);
      if (!stat.isSocket() || stat.uid !== process.getuid()) throw Error('Invalid game service socket');
      await fs.unlink(socketPath);
    }
  }
  const previous = await require('../../core/session/ledger.cjs').readJson(path.join(lib.ROOT, 'data/state/dolphin-fs-rpc/export.json'), {});
  const broker = createBroker({firstPort: previous.port >= 2122 && previous.port < 2199 ? previous.port + 1 : 2122});
  console.log = () => {};
  const server = net.createServer(socket => {
    let input = '', received = false;
    socket.setEncoding('utf8'); socket.setTimeout(150000, () => socket.destroy());
    socket.on('error', () => {});
    socket.on('data', bytes => {
      if (received) return;
      input += bytes;
      if (input.length > 65536) {socket.destroy(); return;}
      if (!input.includes('\n')) return;
      received = true;
      Promise.resolve().then(() => broker.handle(JSON.parse(input.split('\n')[0])))
        .then(result => socket.end(JSON.stringify(result)), error => socket.end(JSON.stringify({error: error.message})));
    });
  });
  await new Promise((resolve, reject) => {server.once('error', reject); server.listen(socketPath, resolve);});
  if (process.platform !== 'win32') await fs.chmod(socketPath, 0o600);
  const expiry = setInterval(() => {if (broker.snapshot().mode === 'storage' && broker.snapshot().status === 'ready') broker.handle({method: 'sd-expire'}).catch(() => {});}, 30000); expiry.unref();
  let closing = false;
  const close = async () => {
    if (closing) return; closing = true;
    clearInterval(expiry); await broker.close(); server.close(); if (process.platform !== 'win32') await fs.unlink(socketPath).catch(() => {});
  };
  process.once('SIGTERM', () => close().catch(() => {process.exitCode = 1;}));
  process.once('SIGINT', () => close().catch(() => {process.exitCode = 1;}));
}
if (require.main === module) serve().catch(error => {if (error.code !== 'EADDRINUSE') {process.stderr.write(error.message + '\n'); process.exitCode = 1;}});
module.exports = {createBroker, serve};
