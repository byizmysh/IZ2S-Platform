const http = require('node:http'), fs = require('node:fs/promises'), path = require('node:path'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const {saveJson, readJson, redact} = require('../../core/session/ledger.cjs');
const control = require('../dolphin-fs-rpc/control.cjs');
const managers = require('./file-managers.cjs');
const configFile = path.join(root, 'data/state/fs-rpc-browser/connection.json');
async function create({port = 0, backend = control, openManager = managers.open, discover = require('./discovery.cjs').discover, token = crypto.randomBytes(32).toString('hex'), panelUrl, settingsFile = configFile, legacyInterfaces = []} = {}) {
  if (!/^[a-f0-9]{64}$/.test(token)) throw Error('Invalid local access key');
  const downloads = require('./downloads.cjs').create({backend});
  const parentOrigin = panelUrl ? require('../../core/workspace.cjs').localUrl(panelUrl).origin : await require('../../core/panel-embedding.cjs').origin(root);
  let shutdownTask;
  let origin;
  const reply = (res, code, value) => {res.writeHead(code, {'content-type': 'application/json; charset=utf-8'}); res.end(JSON.stringify(value));};
  const assets = {'/': ['index.html', 'text/html'], '/app.js': ['app.js', 'text/javascript'], '/style.css': ['style.css', 'text/css']};
  const server = http.createServer(async (req, res) => {
    res.setHeader('cache-control', 'no-store'); res.setHeader('x-content-type-options', 'nosniff'); res.setHeader('referrer-policy', 'no-referrer');
    res.setHeader('content-security-policy', "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors " + (parentOrigin || "'none'") + "; base-uri 'none'; form-action 'none'");
    try {
      if (req.headers.host !== new URL(origin).host) return reply(res, 403, {error: 'Use the local tool address'});
      const url = new URL(req.url, origin);
      if (await require('../../language/server.cjs').serve(req, res)) return;
      if (req.method === 'GET' && assets[url.pathname]) {
        const [name, type] = assets[url.pathname]; res.writeHead(200, {'content-type': type + '; charset=utf-8'});
        return res.end(await fs.readFile(path.join(__dirname, 'ui', name)));
      }
      const download = /^\/download\/([a-f0-9]{64})\/[^/]+$/.exec(url.pathname);
      if (req.method === 'GET' && download) return await downloads.send(req, res, download[1]);
      if (req.headers['x-fs-key'] !== token) return reply(res, 403, {error: 'Reopen the tool using its launch link'});
      if (req.method === 'GET' && url.pathname === '/api/settings') return reply(res, 200, {
        settings: await readJson(settingsFile, {}), platform: process.platform, fileManagers: managers.presets(), version: require('./brand.cjs').version, name: require('./brand.cjs').name,
        application: 'iz2s-files', workspaceTabs:1, pid:process.pid, panelUrl: panelUrl ?? (await readJson(path.join(root,'data/state/window.json'), {})).url ?? ''});
      if (req.method === 'GET' && url.pathname === '/api/downloads') return reply(res, 200, downloads.list());
      if (req.method === 'GET' && url.pathname === '/api/status') {
        let session;
        try {session = await discover({root});}
        catch (error) {return reply(res, 200, {status:'unavailable', error:redact(error.message), sessionAvailable:false});}
        const status = await backend.request('status');
        if (session.nativeSession && status.session && status.session !== session.nativeSession)
          return reply(res, 200, {status:'idle', sessionAvailable:true, sessionId:session.id, progress:'Choose a new export for the current console session.'});
        return reply(res, 200, {...status, sessionAvailable:true, sessionId:session.id});
      }
      if (req.method !== 'POST') return reply(res, 404, {error: 'Not found'});
      if (req.headers.origin !== origin) return reply(res, 403, {error: 'The request must come from this tool'});
      let size = 0, body = '';
      for await (const chunk of req) {size += chunk.length; if (size > 65536) throw Error('Request is too large'); body += chunk;}
      const data = JSON.parse(body || '{}');
      let result;
      if (url.pathname === '/api/shutdown') {
        if (!shutdownTask) shutdownTask = Promise.resolve().then(async () => {
          downloads.close();
          await backend.request('stop',{cancelDownloads:true});
          for(const old of legacyInterfaces){
            if(!Number.isInteger(old.pid)||old.pid<=1||old.pid===process.pid)continue;
            const url=require('../../core/workspace.cjs').localUrl(old.url);
            const response=await fetch(url.origin+'/api/settings',{headers:{'x-fs-key':url.hash.slice(1)},signal:AbortSignal.timeout(1500)}).catch(()=>null);
            if(response?.ok && (await response.json()).application==='iz2s-files'){
              try{process.kill(old.pid,'SIGTERM');}catch(error){if(error.code!=='ESRCH')throw error;}
            }
          }
          return {ok:true};
        }).catch(error => {shutdownTask=null;throw error;});
        result = await shutdownTask;
        res.once('finish', () => {server.closeIdleConnections?.(); server.close();});
      } else if (url.pathname === '/api/detect') {
        const previous = await readJson(settingsFile, {});
        const detected = await discover({root, settings: previous});
        await saveJson(settingsFile, {...previous, run:detected.run, host:detected.host});
        result = {...await backend.request('list'), connection:detected};
      } else if (url.pathname === '/api/connect') {
        throw Error('Choose the engine and connection in IZ2S Platform, then reopen Super File Bros.');
      } else if (url.pathname === '/api/list') result = await backend.request('list', {refresh: true});
      else if (url.pathname === '/api/start') result = await backend.request(data.mode === 'storage' ? 'start-storage' : 'start', {title: data.title});
      else if (url.pathname === '/api/stop') result = await backend.request('stop', {lease: data.lease});
      else if (url.pathname === '/api/download') result = await downloads.prepare(data.path);
      else if (url.pathname === '/api/download/cancel') result = downloads.cancel(data.id);
      else if (['/api/sd/mkdir', '/api/sd/rename', '/api/sd/delete', '/api/sd/upload-begin', '/api/sd/upload-chunk', '/api/sd/upload-finish', '/api/sd/upload-cancel'].includes(url.pathname)) result = await backend.request('sd-' + url.pathname.slice('/api/sd/'.length), data);
      else if (url.pathname === '/api/browse') result = await backend.request('browse', {path: data.path});
      else if (url.pathname === '/api/open') {
        const state = await backend.request('status');
        if (state.status !== 'ready') throw Error('Start a filesystem export first');
        const settings = await readJson(settingsFile, {});
        result = await openManager({url: state.url, manager: data.manager, executable: data.executable || ''});
        await saveJson(settingsFile, {...settings, manager: data.manager, executable: data.executable || ''});
      } else return reply(res, 404, {error: 'Unknown action'});
      reply(res, 200, result);
    } catch (error) {reply(res, 400, {error: redact(error.message).slice(0, 1200)});}
  });
  await new Promise((resolve, reject) => {server.once('error', reject); server.listen(port, '127.0.0.1', resolve);});
  origin = 'http://127.0.0.1:' + server.address().port;
  return {server, url: origin + '/#' + token, token, close: () => {downloads.close(); return new Promise(resolve => server.close(resolve));}};
}
module.exports = {create, configFile};
