// Personal Dolphin integration. OunceCake remains an unmodified GPL-2.0 component.
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const ROOT = path.resolve(__dirname, '../..');
const PORT = 2122;
const read = file => fs.readFile(file, 'utf8').then(JSON.parse);
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

function titleId(value) {
  if (typeof value !== 'string' || !/^[0-9a-f]{16}$/i.test(value)) throw Error('Invalid game Title ID.');
  return value.toLowerCase();
}

async function findSession(root = ROOT, preflight = require(path.join(root, 'core/filesystem/games.cjs')).preflight, {standalone = process.env.NS2_FS_BROWSER === '1'} = {}) {
  if (standalone) {
    const config = await read(path.join(root, 'data/state/fs-rpc-browser/connection.json'));
    if (!path.isAbsolute(config.run || '') || !net.isIPv4(config.host || '')) throw Error('Choose a held FS run folder and console IPv4 address');
    const run = await fs.realpath(config.run);
    const proof = await read(path.join(run, 'switch2/fs-execution-current.json'));
    const connection = await read(path.join(run, 'connection.json'));
    const session = connection.addresses.session;
    if (!proof.verified || !proof.restored || !proof.cleanup?.deferred || !proof.retainedEndpoint?.ready ||
        proof.session !== session || proof.retainedEndpoint.session !== session) throw Error('The run folder does not contain a qualified held FS RPC session');
    const status = await preflight(run);
    if (!status.ready) throw Error(status.reason || 'Held FS RPC is not responding');
    return {id: session, session, run, host: config.host, port: PORT, identity: session + ':' + proof.retainedEndpoint.socketPath};
  }
  const attempts = path.join(root, 'data/state/attempts');
  const rows = [];
  for (const item of await fs.readdir(attempts, {withFileTypes: true})) {
    if (!item.isDirectory()) continue;
    try { rows.push(await read(path.join(attempts, item.name, 'attempt.json'))); }
    catch (error) { if (!['ENOENT', 'ENOTDIR'].includes(error.code) && !(error instanceof SyntaxError)) throw error; }
  }
  rows.sort((a, b) => String(b.created).localeCompare(String(a.created)));
  for (const row of rows) {
    if (row.status !== 'running' || !row.runDirectory) continue;
    const directory = path.resolve(row.runDirectory);
    const menuSession = path.dirname(directory) === path.join(root, 'data/sessions');
    const relative = path.relative(root, directory).split(path.sep).join('/');
    const coreSession = /^(?:engines\/[A-Za-z0-9-]+|bootstrap-chain\/runtime\/extensions\/[a-f0-9]{64}\/engines\/[A-Za-z0-9-]+)\/upstream\/runs\/switch2-[A-Za-z0-9]+$/.test(relative);
    if (!menuSession && !coreSession) continue;
    try {
      let host = row.consoleAddress;
      if (menuSession) {
        const state = await read(path.join(directory, 'fs-rpc-state.json'));
        const link = await read(path.join(directory, 'connection-state.json'));
        if (state.state !== 'ready' || link.state !== 'connected') continue;
      } else {
        const result = await read(path.join(directory, 'result.json'));
        if (!result.verified || result.rpc?.state !== 'listening') continue;
        host ||= (await read(path.join(root,'data/state/launch-configurations.json'))).profiles?.[row.configurationId]?.consoleAddress;
      }
      if (!net.isIPv4(host || '')) continue;
      const run = menuSession ? path.join(directory, 'fs-rpc') : directory;
      const proof = await read(path.join(run, 'switch2/fs-execution-current.json'));
      const session = path.basename(directory);
      if (!proof.verified || !proof.restored ||
          !proof.cleanup?.deferred || !proof.retainedEndpoint?.ready || proof.session !== session ||
          proof.retainedEndpoint.session !== session) continue;
      const status = await preflight(run);
      if (!status.ready) continue;
      return {id: row.id, session, run, host, port: PORT,
        identity: session + ':' + proof.retainedEndpoint.socketPath};
    } catch (error) {
      if (!['ENOENT', 'ENOTDIR', 'ECONNREFUSED'].includes(error.code) && !(error instanceof SyntaxError)) throw error;
    }
  }
  throw Error('No active FS RPC. Enable FS endpoint (--rpc) in IZ2S and connect your console.');
}

async function gamesFor(session, {root = ROOT, refresh = false, list = require(path.join(root, 'core/filesystem/games.cjs')).list} = {}) {
  const file = path.join(root, 'data/state/dolphin-fs-rpc/games.json');
  if (!refresh) {
    try {
      const cached = await read(file);
      if (cached.identity === session.identity && Date.now() - cached.at < 60000) return cached.games;
    } catch (error) { if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error; }
  }
  const games = (await list(session.run)).map(game => ({title: titleId(game.title), name: game.name || game.title,
    storages: game.storages || []})).sort((a, b) => a.name.localeCompare(b.name));
  await fs.mkdir(path.dirname(file), {recursive: true});
  await fs.writeFile(file, JSON.stringify({identity: session.identity, at: Date.now(), games}), {mode: 0o600});
  return games;
}

// Use the standard library FTP client to verify a real passive directory listing,
// rather than treating an open TCP port or a deployment message as readiness.
async function probeFtp(host, port = PORT) {
  const listing = await require('./ftp-client.cjs').listing(host, port);
  if (!listing.length) throw Error('FTP responded, but the game directory is empty.');
  return listing;
}

const storageTransports = new Map();
async function exportStorage(session, {signal, progress = () => {}, ready = () => {}, storageReady = () => {},
  attachGame = require('../../core/filesystem/ouncecake/engine/session.cjs').attachGame,
  createStorage = require('./storage.cjs').createStorage, moduleRoot = ROOT, probe = probeFtp, checkPort = portInUse,
  createNative = require('../../core/console/host/ftp-native.cjs').create,
  createServer = require('../../core/console/host/ftp-server.cjs').create} = {}) {
  const shouldStop = () => Boolean(signal?.aborted);
  const {withCleanup} = require('../../core/filesystem/ouncecake/engine/protocol.cjs');
  if (shouldStop()) return;
  if (await checkPort(session.host, session.port) !== false) throw Error('Console FTP port is unavailable. Disconnect the previous export.');
  let attachment, server, pumping, stopping = false;
  await withCleanup(async () => {
    progress('Opening SD and console filesystems…');
    attachment = await attachGame(session.run);
    const storage = await createStorage(attachment);
    if (shouldStop()) return;
    const moduleDirectories = storage.writableMounts?.includes('sd')
      ? await require('../../core/module-storage.cjs').prepareInstalled(storage, moduleRoot) : null;
    if (shouldStop()) return;
    await storageReady(storage);
    let native = storageTransports.get(session.identity);
    if (!native) {
      const rpc = require(path.join(session.run, 'shared/rpc.cjs'));
      native = await createNative(rpc, session.run);
      storageTransports.set(session.identity, native);
    }
    server = createServer({...native, storage, readOnly: true, greeting: 'NS2 FS RPC: SD and console filesystems, read-only'});
    await server.start(session.port);
    pumping = (async () => {while (!stopping) {await server.pump(); await delay(10);}})();
    pumping.catch(() => {});
    progress('Verifying the filesystem FTP listing…');
    const listing = await probe(session.host, session.port);
    if (shouldStop()) return;
    await ready({url: `ftp://${session.host}:${session.port}/`, listing, mounts: storage.mounts, unavailable: storage.unavailable, moduleDirectories});
    let checked = Date.now();
    while (!shouldStop()) {
      await delay(250);
      if (Date.now() - checked > 10000) {
        const current = await findSession();
        if (current.identity !== session.identity) throw Error('The FS RPC session changed');
        checked = Date.now();
      }
    }
  }, async () => {stopping = true; try {await pumping;} finally {await server?.stop();}},
    async () => {await storageReady(null); await attachment?.close();});
}

async function portInUse(host, port) {
  return new Promise(resolve => {
    const socket = net.createConnection({host, port});
    const done = value => {socket.destroy(); resolve(value);};
    socket.once('connect', () => done(true));
    socket.once('error', error => {
      // A timeout/unreachable host is not evidence of a free port.
      if (error.code === 'ECONNREFUSED') done(false);
      else {socket.destroy(); resolve(null);}
    });
    socket.setTimeout(3000, () => done(null));
  });
}
async function availablePort(host, first = PORT) {
  for (let i = 0; i < 78; i++) {
    const port = PORT + ((first - PORT + i) % 78);
    const used = await portInUse(host, port);
    if (used === false) return port;
    if (used === null) throw Error('The console is not responding over the network');
  }
  throw Error('No free console FTP port in 2122-2199');
}

async function exportGame(session, title, {signal, progress = () => {}, ready = () => {},
  attachGame, prepareGame, deployFtp, readImage, probe = probeFtp, checkPort = portInUse,
  health = async () => {
    const current = await findSession();
    if (current.identity !== session.identity) throw Error('The FS RPC session changed. Select the game again.');
  }} = {}) {
  title = titleId(title);
  const upstream = path.join(ROOT, 'core/filesystem/ouncecake/engine');
  attachGame ||= require(path.join(upstream, 'session.cjs')).attachGame;
  prepareGame ||= require(path.join(upstream, 'mounts.cjs')).prepareGame;
  deployFtp ||= require(path.join(upstream, 'ftp.cjs')).deployFtp;
  readImage ||= require(path.join(upstream, 'ftp.cjs')).readImage;
  const {withCleanup} = require(path.join(upstream, 'protocol.cjs'));
  const shouldStop = () => Boolean(signal?.aborted);
  if (shouldStop()) return;
  const occupied = await checkPort(session.host, session.port);
  if (occupied !== false) throw Error(occupied ? 'Port 2122 is already in use. Disconnect the previous game export.' : 'The console is not responding over the network. Check the connection.');
  let attachment, server;
  await withCleanup(async () => {
    progress('Connecting to the console filesystem…');
    attachment = await attachGame(session.run);
    if (shouldStop()) return;
    progress('Preparing game files. Large games may take several minutes…');
    const prepared = await prepareGame({attachment, title, shouldStop});
    if (shouldStop()) return;
    server = await deployFtp({attachment, image: await readImage(), ...prepared, port: session.port});
    if (shouldStop()) return;
    progress('Verifying the FTP directory listing…');
    const listing = await probe(session.host, session.port);
    if (shouldStop()) return;
    await ready({url: `ftp://${session.host}:${session.port}/`, listing});
    let checked = Date.now();
    while (!shouldStop()) {
      await delay(250);
      if (Date.now() - checked >= 10000) {await health(); checked = Date.now();}
    }
  }, () => server?.close(), () => attachment?.close());
}

module.exports = {ROOT, PORT, titleId, findSession, gamesFor, probeFtp, exportGame, exportStorage, availablePort};
