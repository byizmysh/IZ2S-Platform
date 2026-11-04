const http = require('node:http'),
  fs = require('node:fs/promises'),
  path = require('node:path'),
  crypto = require('node:crypto');
const { redact } = require('../core/session/ledger.cjs');
async function createWindowServer({
  root,
  manager,
  port,
  token = crypto.randomBytes(24).toString('hex'),
  stateReadTimeoutMs = 10000,
  openRpcPage = require('../core/rpc-browser.cjs').open,
  workspace = require('../core/workspace.cjs'),
}) {
  if (!Number.isFinite(stateReadTimeoutMs) || stateReadTimeoutMs <= 0)
    throw Error('Status read timeout must be positive');
  let actualPort,
    mutation = Promise.resolve(),
    mutationEpoch = 0,
    stateRead = null,
    lastSnapshot = null,
    snapshotSequence = 0;
  const stateJobs = new Set();
  const statusUnavailable = message => Object.assign(Error(message), {statusCode: 503});
  function snapshot() {
    if (!stateRead) {
      // A timed-out snapshot cannot be cancelled. Bound detached work while
      // allowing one replacement read to recover from a single stalled job.
      if (stateJobs.size >= 2)
        return Promise.reject(statusUnavailable('Status is still busy. Try again shortly.'));
      Promise.resolve().then(() => manager.refresh()).catch(() => {});
      const sequence=++snapshotSequence,epoch=mutationEpoch;
      const job = Promise.resolve().then(() => manager.snapshot());
      stateJobs.add(job);
      const release = () => { stateJobs.delete(job); };
      job.then(value=>{if(epoch===mutationEpoch&&sequence===snapshotSequence)lastSnapshot={value,at:Date.now()};release();}, release);
      let deadline;
      const pending = new Promise((resolve, reject) => {
        deadline = setTimeout(() => reject(statusUnavailable('Status read timed out. Try again shortly.')), stateReadTimeoutMs);
        job.then(resolve, reject);
      }).finally(() => {
        clearTimeout(deadline);
        if (stateRead === pending) stateRead = null;
      });
      stateRead = pending;
    }
    return stateRead;
  }
  const assets = {
    '/': ['index.html', 'text/html; charset=utf-8'],
    '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
    '/cardputer-setup.js': ['cardputer-setup.js', 'text/javascript; charset=utf-8'],
    '/style.css': ['style.css', 'text/css; charset=utf-8'],
    '/icon.svg': ['icon.svg', 'image/svg+xml'],
  };
  function send(res, status, headers, body) {
    if (res.destroyed || res.writableEnded) return;
    if (res.headersSent) { res.destroy(); return; }
    res.writeHead(status, headers);
    res.end(body);
  }
  const reply = (res, status, value) => {
    const body = JSON.stringify(value);
    send(res, status, {'content-type': 'application/json; charset=utf-8'}, body);
  };
  async function sendFile(res, file, headers) {
    const body = await fs.readFile(file);
    send(res, 200, headers, body);
  }
  function fail(res, error) {
    try {
      reply(res, error?.statusCode === 503 ? 503 : 400, {error: redact(error?.message || String(error)).slice(0, 1200)});
    } catch {
      if (!res.destroyed) res.destroy();
    }
  }
  async function readBody(req) {
    let size = 0,
      chunks = [];
    for await (const chunk of req) {
      size += chunk.length;
      if (size > 16384) throw Error('Request is too large');
      chunks.push(chunk);
    }
    return JSON.parse(Buffer.concat(chunks).toString() || '{}');
  }
  async function handleRequest(req, res) {
    res.setHeader('cache-control', 'no-store');
    res.setHeader('x-content-type-options', 'nosniff');
    res.setHeader('referrer-policy', 'no-referrer');
    res.setHeader(
      'content-security-policy',
      "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-src http://127.0.0.1:*; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'",
    );
    const origin = 'http://127.0.0.1:' + actualPort;
    try {
      if (req.headers.host !== '127.0.0.1:' + actualPort)
        return reply(res, 403, { error: 'Use the local application address' });
      const url = new URL(req.url, origin);
      if (await require('../language/server.cjs').serve(req, res)) return;
      if (req.method === 'GET' && url.pathname === '/health')
        return reply(res, 200, {
          version: 21,
          pid: process.pid,
          preview: manager.preview === true,
        });
      if (req.method === 'GET' && assets[url.pathname]) {
        const [name, type] = assets[url.pathname];
        return await sendFile(res, path.join(root, 'panel/ui', name), {'content-type': type});
      }
      if (req.headers['x-iz-key'] !== token)
        return reply(res, 403, {
          error: 'Reopen the application using its launch link',
        });
      if (req.method === 'GET' && url.pathname === '/api/extensions') {
        const inventory = await require('../core/modules.cjs').list(root);
        return reply(res, 200, {
          ...require('../core/extensions.cjs').catalog(root),
          modules: inventory.modules.map((m) => m.descriptor).filter(require('../core/packages/store-catalog.cjs').visible),
          moduleErrors: inventory.errors,
        });
      }
      if (req.method === 'GET' && url.pathname === '/api/state') {
        return reply(res, 200, await snapshot());
      }
      if (req.method === 'GET' && url.pathname === '/api/loader-source') {
        const source = await manager.source(url.searchParams.get('id'));
        return await sendFile(res, source.file, {
          'content-type': 'text/plain; charset=utf-8',
          'content-disposition': 'attachment; filename=' + source.name + '.bas',
        });
      }
      if (req.method === 'GET' && url.pathname === '/api/cardputer/firmware')
        return await sendFile(res, require('../hardware/cardputer.cjs').firmwarePath(root), {'content-type':'application/octet-stream','content-disposition':'attachment; filename="IZ2S-ADV-0.21.bin"'});
      if (req.method === 'GET' && url.pathname === '/api/cardputer/package')
        return await sendFile(res, require('../hardware/cardputer.cjs').packagePath(root), {'content-type':'application/zip','content-disposition':'attachment; filename="iz2s-cardputer-adv-sd.zip"'});
      if (req.method === 'GET' && url.pathname === '/api/input-package') {
        const file=await require('../hardware/agents.cjs').archive(root);
        return await sendFile(res, file, {'content-type':'application/gzip','content-disposition':'attachment; filename="iz2s-arm-input.tar.gz"'});
      }
      if (req.method === 'GET' && url.pathname === '/api/setup-report')
        return reply(res, 200, {
          text: await manager.setupReport(url.searchParams.get('id')),
        });
      if (req.method === 'GET' && url.pathname === '/api/log')
        return reply(res, 200, {
          events: await manager.historyLog(url.searchParams.get('id')),
        });
      if (req.method === 'GET' && url.pathname === '/api/workspace') {
        if(manager.preview)return reply(res,200,{tabs:[]});
        const current=lastSnapshot&&Date.now()-lastSnapshot.at<1500?lastSnapshot.value:await snapshot();
        return reply(res,200,{tabs:await workspace.visible(root,{active:current.active,connected:current.connected})});
      }
      if (req.method !== 'POST') return reply(res, 404, { error: 'Not found' });
      if (req.headers.origin !== origin)
        return reply(res, 403, {
          error: 'The request must come from this application',
        });
      const body = await readBody(req);
      // Emergency stop must bypass a blocked ordinary mutation queue.
      if (url.pathname === '/api/hard-stop') {
        mutationEpoch++;
        mutation = Promise.resolve();
        stateRead = null;
        lastSnapshot = null;
        const result = await manager.hardStop({
          beforeSignal: () => require('../core/host-modules.cjs').hardStop(),
        });
        stateRead = null;
        lastSnapshot = null;
        return reply(res, 200, result);
      }
      if (url.pathname === '/api/stop') {
        mutationEpoch++;
        mutation = Promise.resolve();
        stateRead = null;
        lastSnapshot = null;
        const stopped = await manager.stop({
          beforeSignal: () => require('../core/host-modules.cjs').close(),
        });
        stateRead = null;
        lastSnapshot = null;
        return reply(res, 200, stopped || { ok: true });
      }
      const ownEpoch = mutationEpoch;
      const task = async () => {
        if (ownEpoch !== mutationEpoch)
          throw Error('Request cancelled by STOP');
        if (url.pathname === '/api/rpc/open') {
          if (manager.preview) throw Error('Preview cannot open Super File Bros.');
          return openRpcPage(root, body);
        }
        if (url.pathname === '/api/dolphin/open') {
          if (manager.preview) throw Error('Preview cannot open Dolphin');
          return require('../core/dolphin-rpc.cjs').open(root, body);
        }
        if (url.pathname === '/api/folder/open') {
          if (manager.preview) throw Error('Preview cannot open local folders');
          return require('../core/desktop-folders.cjs').open(root, body);
        }
        if (url.pathname === '/api/module/settings') {
          if (manager.preview) throw Error('Preview cannot open module settings');
          const current=await snapshot();
          const result = await require('../core/module-settings.cjs').open(root, body.id);
          const module = await require('../core/modules.cjs').read(root,body.id);
          const tab = await workspace.register(root,{id:'settings-'+body.id,
            title:module.descriptor.name+' · Settings',url:result.url,moduleId:body.id,kind:'settings',ownerPid:process.pid,
            sessionId:current.connected?current.active?.id:undefined});
          return {...result,tab};
        }
        if (url.pathname === '/api/workspace/stop') {
          if(manager.preview)throw Error('Preview cannot stop modules');
          return workspace.stop(root,body.id);
        }
        if (url.pathname === '/api/workspace/attach') {
          if(manager.preview)throw Error('Preview cannot open session modules');
          return require('../core/session-workspace.cjs').attach(root,body.url);
        }
        if (url.pathname === '/api/workspace/open') {
          if(manager.preview)throw Error('Preview cannot start modules');
          if (body.id === 'files') return openRpcPage(root,{});
          const previous=(await workspace.list(root)).find(tab=>tab.id===body.id);
          if (!previous) throw Error('Unknown tool');
          if (previous.kind==='settings') {
            const current=await snapshot();
            const result=await require('../core/module-settings.cjs').open(root,previous.moduleId);
            return {ok:true,tab:await workspace.register(root,{...previous,url:result.url,automatic:false,requestId:undefined,
              ownerPid:process.pid,panelInstance:undefined,sessionId:current.connected?current.active?.id:undefined})};
          }
          if(previous.state!=='running'){
            if(previous.controlUrl)return require('../core/session-workspace.cjs').request(previous.controlUrl,'start',previous.moduleId);
            const module=await require('../core/modules.cjs').read(root,previous.moduleId);
            if(module.descriptor.switch)throw Error('Start this module on the console.');
            await require('../core/host-modules.cjs').start(root,previous.moduleId);
            const started=(await workspace.list(root)).find(tab=>tab.id===body.id);
            if(started?.state!=='running')throw Error('The module started but its interface is not ready. Open its module tab.');
            return {ok:true,tab:started};
          }
          return {ok:true,tab:await workspace.register(root,{...previous,automatic:false,requestId:undefined})};
        }
        if (url.pathname === '/api/module/start') {
          if (manager.preview) throw Error('Preview cannot execute modules');
          const current = (await manager.snapshot()).active;
          const session = current?.runDirectory
            ? { directory: current.runDirectory, id: current.id }
            : null;
          return require('../core/host-modules.cjs').start(root, body.id, {
            session,
          });
        }
        if (url.pathname === '/api/module/stop')
          return require('../core/host-modules.cjs').stop(body.id);
        if (url.pathname === '/api/install-host')
          return require('../core/modules.cjs').install(root, body.id, {source:body.source});
        if (url.pathname === '/api/engine/install')
          return manager.installEngine(body.id);
        if (url.pathname === '/api/engine/rescan')
          return manager.rescanEngine();
        if (url.pathname === '/api/launch-settings')
          return manager.configureLaunch(body);
        if (url.pathname === '/api/ns2hb/check')
          return manager.inspectNS2HB();
        if (url.pathname === '/api/cardputer/prepare') return manager.prepareCardputerSD();
        if (url.pathname === '/api/cardputer/install') return manager.installCardputerSD(body);
        if (url.pathname === '/api/input/install') return manager.installInput(body);
        if (url.pathname === '/api/hardware/open') return manager.openHardwareApp();
        if (url.pathname === '/api/check') return manager.refresh(true);
        if (url.pathname === '/api/connection')
          return manager.configureConnection(body);
        if (url.pathname === '/api/start') return manager.begin(body);
        if (url.pathname === '/api/setup') return manager.setupAction(body);
        if (url.pathname === '/api/retry') return manager.retry();
        if (url.pathname === '/api/load') {
          await manager.loadPackages(body.ids);
          return { ok: true };
        }
        if(url.pathname==='/api/unload'){
          if(manager.preview)throw Error('Preview cannot remove console modules');
          await manager.unloadPackages(body.ids);
          return {ok:true};
        }
        if (url.pathname === '/api/delete') {
          await manager.remove(body.id);
          return { ok: true };
        }
        if (url.pathname === '/api/dismiss') {
          manager.dismiss();
          return { ok: true };
        }
        throw Error('Unknown action');
      };
      const result = mutation.then(task);
      mutation = result.catch(() => {});
      const value = await result;
      stateRead = null;
      lastSnapshot = null;
      reply(res, 200, value);
    } catch (e) {
      fail(res, e);
    }
  }
  const server = http.createServer((req, res) => {
    // EventEmitter does not await async request listeners.
    void handleRequest(req, res).catch(error => fail(res, error));
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => {
      actualPort = server.address().port;
      resolve();
    });
  });
  return {
    server,
    token,
    url: 'http://127.0.0.1:' + actualPort + '/#' + token,
    close: async () => { await require('../core/module-settings.cjs').close(root); await new Promise((r) => server.close(r)); },
  };
}
module.exports = { createWindowServer };
