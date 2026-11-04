const fs = require('node:fs/promises'), path = require('node:path');
const root = path.resolve(__dirname, '../..');
const L = require('../filesystem/layout.cjs');
const paths = require('../console/host/ftp-native.cjs');
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function create({rpc, mapped, directory, native: providedNative,
  games = require('../../tools/dolphin-fs-rpc/control.cjs'),
  createServer = api => require('../console/host/ftp-server.cjs').create(api)}) {
  const native = providedNative || await paths.create(rpc, directory);
  const server = createServer(native), base = mapped + L.OFFSET;
  let closed = false, ack = 0, generation = 0, cwd = '/', entries = [], page = 0,
    error = '', lastPublish = 0, request = null, dirty = true, mode = 'files',
    work = null, busy = false, exportState = null, lease = null, selectedTitle = null, lastGamePoll = 0, storagePath = '/';
  async function listing(target) {
    entries = (await native.storage.list(target)).sort((a, b) => (a.type === b.type ? 0 : a.type === 'dir' ? -1 : 1) || a.name.localeCompare(b.name));
    if (target === '/') entries.push({name: 'Games (FS RPC)', type: 'dir', games: true}, {name: 'SD & console storage (FS RPC)', type: 'dir', storage: true});
    cwd = target; page = 0; generation++;
  }
  function ftpState() {
    if (mode === 'files') return server.state;
    const state = exportState || {};
    return {address: state.host || '0.0.0.0', port: state.port || 2122,
      running: state.status === 'ready', activity: state.progress || 'A: select a game to start FTP',
      error: state.status === 'failed' ? state.error : ''};
  }
  async function publish() {
    const ftp = ftpState();
    await rpc.write(base + 64, L.encode({ack, generation, cwd, entries, page, ftp, error, busy, mode}));
    lastPublish = Date.now(); dirty = false;
    await fs.writeFile(path.join(directory, 'file-exchange-fs.json'), JSON.stringify({session: rpc.a.session,
      active: !closed, mode, cwd, page, entries: entries.length, busy, selectedTitle,
      gameExport: exportState, ftp, error}, null, 2));
  }
  function background(action) {
    if (work) return;
    busy = true; error = ''; dirty = true;
    work = Promise.resolve().then(action).catch(e => {error = e.message;})
      .finally(() => {busy = false; dirty = true; work = null;});
  }
  async function releaseGame() {
    if (lease) await games.request('stop', {lease, cancelDownloads: true});
    lease = null; exportState = null;
  }
  async function showGames(refresh = false) {
    const result = await games.request('list', {refresh});
    if (result.session.session !== rpc.a.session) throw Error('FS RPC belongs to another console session');
    await server.stop();
    mode = 'games'; cwd = '/games: choose a game';
    entries = result.games.map(game => ({type: 'game', name: game.name, title: game.title}));
    page = 0; generation++; exportState = {host: result.session.host, port: result.session.port, status: 'idle'};
  }
  async function startGame(title) {
    const state = await games.request('start', {title});
    if (state.session !== rpc.a.session) throw Error('Game export belongs to another console session');
    exportState = state; lease = state.lease; selectedTitle = title;
    cwd = '/games: ' + state.name; generation++;
  }
  async function browseStorage(target) {
    const result = await games.request('browse', {path: target});
    entries = result.entries; storagePath = result.cwd; cwd = '/storage' + (storagePath === '/' ? '' : storagePath);
    page = 0; generation++;
  }
  async function startStorage() {
    let state = await games.request('start-storage');
    if (state.session !== rpc.a.session) throw Error('Filesystem export belongs to another console session');
    lease = state.lease; exportState = state; selectedTitle = 'storage';
    await server.stop();
    while (state.status === 'preparing' && !closed) {
      await sleep(250); state = await games.request('status'); exportState = state; dirty = true;
      if (state.lease !== lease) throw Error('Filesystem export was replaced');
    }
    if (closed) return;
    if (state.status !== 'ready') throw Error(state.error || 'Filesystem export did not start');
    mode = 'storage'; await browseStorage('/');
  }
  try {await listing('/'); await server.start(2121); await publish();}
  catch (e) {await server.stop().catch(() => {}); throw e;}
  const pumping = (async () => {
    while (!closed) {
      try {
        if (mode === 'files') await server.pump();
        else if (Date.now() - lastGamePoll >= 500 && !work) {
          lastGamePoll = Date.now();
          const state = await games.request('status');
          if (state.session && state.session !== rpc.a.session) throw Error('FS RPC session changed');
          if (state.lease && state.lease !== lease && ['ready', 'preparing'].includes(state.status)) {
            lease = state.lease; selectedTitle = state.title;
            if (mode === 'games') cwd = '/games: ' + state.name;
          }
          if (JSON.stringify(exportState) !== JSON.stringify(state)) {exportState = state; dirty = true;}
        }
      } catch (e) {error = e.message; dirty = true;}
      await sleep(20);
    }
  })();
  return {
    async pending() {
      if (closed) return false;
      request = await rpc.read(base, 16);
      return dirty || !!request.readUInt32LE() && request.readUInt32LE() !== ack || Date.now() - lastPublish >= 400;
    },
    async afterBatch() {
      if (closed) return;
      const packet = request || await rpc.read(base, 16), seq = packet.readUInt32LE(); request = null;
      if (seq && seq !== ack) {
        error = '';
        try {
          const op = packet.readUInt32LE(4), arg = packet.readUInt32LE(8);
          if (busy) throw Error('Wait for the current operation to finish');
          if (op === 1) background(async () => {await releaseGame(); mode = 'files'; await listing('/'); await server.start(2121);});
          else if (op === 2) {
            const entry = entries[page * L.ROWS + arg];
            if (arg >= L.ROWS || !entry) throw Error('Entry no longer available');
            if (entry.games) background(() => showGames());
            else if (entry.storage) background(() => startStorage());
            else if (entry.type === 'game') background(() => startGame(entry.title));
            else if (entry.type === 'dir') {
              if (mode === 'storage') background(() => browseStorage(paths.virtualPath(storagePath, entry.name)));
              else await listing(paths.virtualPath(cwd, entry.name));
            }
          } else if (op === 3) {
            if (mode === 'storage' && storagePath !== '/') background(() => browseStorage(paths.virtualPath(storagePath, '..')));
            else if (mode !== 'files') background(async () => {await releaseGame(); mode = 'files'; await listing('/'); await server.start(2121);});
            else await listing(paths.virtualPath(cwd, '..'));
          } else if (op === 4) {
            if (mode === 'storage') background(() => browseStorage(storagePath));
            else if (mode === 'games') background(() => showGames(true)); else await listing(cwd);
          } else if (op === 5) page = Math.min(Math.max(0, Math.ceil(entries.length / L.ROWS) - 1), page + 1);
          else if (op === 6) page = Math.max(0, page - 1);
          else if (op === 7) {
            if (mode !== 'files') background(async () => {
              if (lease && ['ready', 'preparing'].includes(exportState?.status)) await releaseGame();
              else if (selectedTitle === 'storage') await startStorage(); else if (selectedTitle) await startGame(selectedTitle);
            });
            else if (server.state.running) await server.stop(); else await server.start(2121);
          } else throw Error('Unknown file command');
        } catch (e) {error = e.message;}
        ack = seq;
      }
      await publish();
    },
    async close() {
      if (closed) return; closed = true;
      await work; await pumping;
      await releaseGame(); await server.stop(); await publish();
    },
  };
}
module.exports = {create};
