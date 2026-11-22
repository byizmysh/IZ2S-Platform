'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const { pipeline } = require('node:stream/promises');
const { createInterface } = require('node:readline');
const controller = require('./controller.cjs');
const paths = require('./runtime.cjs');
const { StringDecoder } = require('node:string_decoder');
const diagnostics = require('./ui/view.js');
const { timings } = require('./journal.cjs');
const LOG_LIMIT = 24000;
const assets = Object.freeze({
  'connection.css': 'text/css; charset=utf-8',
  'messages.js': 'text/javascript; charset=utf-8',
  'view.js': 'text/javascript; charset=utf-8',
});
const ui = path.join(__dirname, 'ui');
const evidenceCache = new Map();
const pcLinkOrigin = 'http://127.0.0.1:39525';

// Check the same local destination the menu will open. In particular, the PC
// bridge can serve the application queue before its Files page is registered.
async function pcTool(view, request = fetch, options = {}) {
  if (!['apps', 'files'].includes(view)) throw Error('Unknown IZ2S PC tool');
  if(request===fetch&&!options.preview)await require('./tools/pc-link-service.cjs').ensure(options.out).catch(()=>{});
  const url = pcLinkOrigin + (view === 'files' ? '/files' : '/');
  const unavailable = {available:false};
  try {
    const response = await request(pcLinkOrigin + (view === 'files' ? '/files' : '/status'), {
      redirect:'manual', signal:AbortSignal.timeout(1200),
    });
    if (view === 'files') {
      const target = new URL(response.headers.get('location') || '');
      if (response.status !== 302 || target.protocol !== 'http:' || target.hostname !== '127.0.0.1' ||
          target.username || target.password) return unavailable;
    } else {
      if (!response.ok) return unavailable;
      const status = await response.json();
      if (typeof status?.online !== 'boolean' || typeof status.files !== 'boolean') return unavailable;
    }
    if(view==='files'&&options.open&&options.root&&!options.preview){
      const status=await require('./tools/pc-link-service.cjs').status();
      let target=url;
      if(view==='files'){
        const response=await request(url,{redirect:'manual',signal:AbortSignal.timeout(1200)});
        target=new URL(response.headers.get('location')).href;
      }
      const tab=await require('../../core/workspace.cjs').register(options.root,{
        id:'core-'+view,title:view==='files'?'Super File Bros.':'Add app',kind:view==='files'?'files':'settings',
        url:target,ownerPid:status?.service?.pid,coreTool:true,
      });
      return {available:true,url,tab};
    }
    return {available:true, url};
  } catch { return unavailable; }
}

async function insertConnection(html) {
  const connection = await fs.readFile(path.join(ui, 'connection.html'), 'utf8');
  const includes = Object.keys(assets).filter(name => !html.includes('/engines/iz2s-core/' + name)).map(name => name.endsWith('.css')
    ? '<link rel="stylesheet" href="/engines/iz2s-core/' + name + '">'
    : '<script src="/engines/iz2s-core/' + name + '" defer></script>').join('');
  return html.replace('<!-- IZ2S_CORE_ASSETS -->', () => includes)
    .replace('<!-- IZ2S_CORE_CONNECTION -->', () => connection);
}

function logPath(out, id) {
  if (typeof id !== 'string' || !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(id)) return null;
  return path.join(paths.journal(out), id + '.log');
}

async function readLog(out, id, limit = LOG_LIMIT) {
  const empty = { log: '', logUpdatedAt: null, logBytes: 0, logAttemptId: id || null,
    logTruncated: false, logTimings: [], logEvidence: diagnostics.scanner().result };
  const file = logPath(out, id);
  if (!file) return empty;
  let handle;
  try {
    handle = await fs.open(file);
    const stat = await handle.stat();
    const offset = Math.max(0, stat.size - limit);
    const buffer = Buffer.alloc(stat.size - offset);
    const { bytesRead } = await handle.read(buffer, 0, buffer.length, offset);
    let log = buffer.toString('utf8', 0, bytesRead);
    if (offset) {
      const endOfFragment = log.indexOf('\n');
      log = endOfFragment === -1 ? '' : log.slice(endOfFragment + 1);
    }
    // Parse the whole file for phase and result. The browser obtains the full
    // file through the download route when the status contains only a tail.
    const version = [stat.dev, stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
    let cached = evidenceCache.get(file);
    if (cached?.version !== version) {
      const scan = diagnostics.scanner();
      if (stat.size) {
        const lines = createInterface({ input: handle.createReadStream({
          start: 0, end: stat.size - 1, autoClose: false,
        }), crlfDelay: Infinity });
        for await (const line of lines) scan.line(line);
      }
      cached = { version, evidence: scan.result, timings: await timings(file, stat.size) };
      evidenceCache.set(file, cached);
      if (evidenceCache.size > 16) evidenceCache.delete(evidenceCache.keys().next().value);
    }
    const tailBytes = Buffer.from(log);
    const tailStart = stat.size - tailBytes.length;
    const displayedTimings = cached.timings.filter(([end]) => end > tailStart).map(([end, at]) =>
      [paths.displayedBytes(tailBytes.subarray(0, end - tailStart), out), at]);
    const displayedEvidence = {...cached.evidence, errors:cached.evidence.errors.map(line => paths.displayPath(line, out))};
    return { log:paths.displayPath(log, out), logUpdatedAt: stat.size ? stat.mtime.toISOString() : null,
      logBytes: stat.size, logAttemptId: id, logTruncated: offset > 0,
      logTimings: displayedTimings, logEvidence: displayedEvidence };
  } catch (error) {
    if (error.code !== 'ENOENT') throw error;
    return empty;
  } finally {
    await handle?.close();
  }
}

async function downloadLog(out, id, res) {
  const file = logPath(out, id);
  const handle = file ? await fs.open(file).catch(error => {
    if (error.code !== 'ENOENT') throw error;
    return null;
  }) : null;
  if (!handle) {
    res.writeHead(404, { 'content-type': 'application/json; charset=utf-8' });
    res.end(JSON.stringify({ error: 'IZ2S log not found' }));
    return;
  }
  try {
    res.writeHead(200, {
      'content-type': 'text/plain; charset=utf-8',
      'content-disposition': 'attachment; filename="iz2s-core-' + id + '.log"',
    });
    await pipeline(handle.createReadStream({ autoClose: false }), async function* (source) {
      const decoder = new StringDecoder('utf8');
      let pending = '';
      for await (const chunk of source) {
        pending += decoder.write(chunk);
        const end = pending.lastIndexOf('\n');
        if (end >= 0) { yield paths.displayPath(pending.slice(0, end + 1), out); pending = pending.slice(end + 1); }
      }
      yield paths.displayPath(pending + decoder.end(), out);
    }, res);
  } finally {
    await handle.close();
  }
}

function create(options = {}) {
  const out = options.out || controller.DEFAULT_OUT;
  const runtime = controller.create({ ...options, out });
  const sdStore = require('./sd-store.cjs').create({
    root:options.root, out, preview:options.manager?.preview===true, ...options.sdStore,
  });
  let fsProbe = null;
  async function filesystem(snapshot) {
    if (options.manager?.preview || !snapshot.busy || !snapshot.attempt?.core?.complete || snapshot.attempt.finishedAt) return {ready:false};
    const id=snapshot.attempt.id;
    if (!fsProbe || fsProbe.id!==id || Date.now()>=fsProbe.until) {
      const probe={id,until:Infinity};
      probe.task=require('../../core/filesystem/core-session.cjs').find(options.root,{out})
        .then(session=>({ready:session?.id===id,attemptId:id,source:session?.source||null}))
        .catch(()=>({ready:false,attemptId:id}))
        .finally(()=>{probe.until=Date.now()+10000;});
      fsProbe=probe;
    }
    return fsProbe.task;
  }
  return {
    ...runtime,
    history:require('./history.cjs').create({out,state:runtime.state,readLog}),
    sdStore,
    async state() {
      // Capture the attempt and ownership together, before any journal I/O.
      // Never re-read latest.json after choosing the attempt's immutable ID.
      const snapshot = await runtime.state();
      const journal = await readLog(out, snapshot.attempt?.id);
      let pcLink=null;
      if(!options.manager?.preview)pcLink=await require('./tools/pc-link-service.cjs').snapshot(out).catch(error=>({online:false,error:error.message}));
      const fsRpc=await filesystem(snapshot);
      const value = { ...snapshot, ...journal, pcLink, fsRpc };
      return { ...value, diagnostic: diagnostics.describe(value) };
    },
    downloadLog: (id, res) => downloadLog(out, id, res),
    assertAvailable(route) {
      if (runtime.running() && ['/api/start', '/api/retry', '/api/setup', '/api/civswitch',
        '/api/civswitch-ldn', '/api/connection', '/api/launch-settings', '/api/connection/restore'].includes(route)) {
        throw Error('An IZ2S attempt is already active. Stop it first.');
      }
    },
    assertCanClose() {
      if (runtime.running()) throw Error('IZ2S core is active; stop its attempt before closing the panel.');
    },
  };
}

module.exports = { create, DEFAULT_OUT: controller.DEFAULT_OUT, assets, ui, insertConnection, logPath, readLog, pcTool };
