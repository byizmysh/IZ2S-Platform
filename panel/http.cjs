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
  coreOut,
  coreSdStore,
  openRpcPage = require('../core/rpc-browser.cjs').open,
  workspace = require('../core/workspace.cjs'),
}) {
  if (!Number.isFinite(stateReadTimeoutMs) || stateReadTimeoutMs <= 0)
    throw Error('Status read timeout must be positive');
  let exportingApp = false, closing = false, shutdownHook;
  let launcherFilesBusy = false;
  async function changeLauncherFiles(action) {
    if (launcherFilesBusy) throw Error('Wait for the current file connection action to finish.');
    launcherFilesBusy = true;
    try { return await action(); }
    finally { launcherFilesBusy = false; }
  }
  let actualPort,
    mutation = Promise.resolve(),
    mutationEpoch = 0,
    stateRead = null,
    lastSnapshot = null,
    snapshotSequence = 0,
    startupRead = null,
    startupComplete = false;
  const stateJobs = new Set();
  const statusUnavailable = message => Object.assign(Error(message), {statusCode: 503});
  function readSnapshot() {
    Promise.resolve().then(() => manager.refresh()).catch(() => {});
    const sequence=++snapshotSequence,epoch=mutationEpoch;
    const job = Promise.resolve().then(async () => {
      const value=await manager.snapshot(),coreRows=await coreController.history.list();
      const rows=new Map((value.history||[]).map(row=>[row.id,row]));
      for(const row of coreRows)rows.set(row.id,row);
      return {...value,history:[...rows.values()].sort((a,b)=>String(b.created||'').localeCompare(String(a.created||'')))};
    });
    stateJobs.add(job);
    const release = () => { stateJobs.delete(job); };
    job.then(value=>{if(epoch===mutationEpoch&&sequence===snapshotSequence)lastSnapshot={value,at:Date.now()};release();}, release);
    return job;
  }
  function snapshot({startup = false} = {}) {
    // The first inventory verifies every installed archive. On a cold PC this
    // exceeds the ordinary status deadline. Keep one shared read alive instead
    // of starting another inventory whenever a window retries or is reopened.
    if (startup && !startupComplete) {
      if (!startupRead) {
        const job = [...stateJobs].at(-1) || readSnapshot();
        startupRead = job.then(value => {
          startupComplete = true;
          return value;
        }).finally(() => { startupRead = null; });
      }
      return startupRead;
    }
    if (!stateRead) {
      // A timed-out snapshot cannot be cancelled. Bound detached work while
      // allowing one replacement read to recover from a single stalled job.
      if (!startupRead && stateJobs.size >= 2)
        return Promise.reject(statusUnavailable('Status is still busy. Try again shortly.'));
      const job = startupRead || readSnapshot();
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
    '/reconnect.svg': ['reconnect.svg', 'image/svg+xml'],
    '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
    '/blitbun-capture.js': ['blitbun-capture.js', 'text/javascript; charset=utf-8'],
    '/storage.js': ['storage.js', 'text/javascript; charset=utf-8'],
    '/startup.js': ['startup.js', 'text/javascript; charset=utf-8'],
    '/cardputer-setup.js': ['cardputer-setup.js', 'text/javascript; charset=utf-8'],
    '/iz2sciv.js': ['iz2sciv.js', 'text/javascript; charset=utf-8'],
    '/civswitch.js': ['civswitch.js', 'text/javascript; charset=utf-8'],
    '/style.css': ['style.css', 'text/css; charset=utf-8'],
    '/icon.svg': ['icon.svg', 'image/svg+xml'],
  };
  const blitBrowser = require('./blitbun-browser.cjs').create({root,out:coreOut});
  const blitAssets = {
    '/blitbun-codec.js':['pc-codec.js','text/javascript; charset=utf-8'],
    '/blitbun-codec.wasm':['frame-codec.wasm','application/wasm'],
    '/blitbun-pcm.js':['pcm-capture-worklet.js','text/javascript; charset=utf-8'],
  };
  const corePanel = require('../engines/iz2s-core/panel.cjs');
  const coreController = corePanel.create({root, manager, out: coreOut, sdStore:coreSdStore});
  const withCoreSd = async value => {
    const coreSd=await coreController.sdStore.status();
    return coreSd.selected?{...value,coreSd}:value;
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
  function fail(res, error, coreRequest = false) {
    try {
      reply(res, error?.statusCode === 503 ? 503 : 400, {error: (coreRequest ? redact(error?.message || String(error)) : require('../language/error-text.js').canonical(redact(error?.message || String(error)))).slice(0,1200)});
    } catch {
      if (!res.destroyed) res.destroy();
    }
  }
  async function readBody(req, limit = 16384) {
    let size = 0,
      chunks = [];
    for await (const chunk of req) {
      size += chunk.length;
      if (size > limit) throw Error('Request is too large');
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
          version: 22,
          edition: '0.3.1',
          release: '0.3.1',
          coreApi: '/api/iz2s-core',
          pid: process.pid,
          preview: manager.preview === true,
          closing,
          handoverProtocol: 1,
        });
      if (req.method === 'GET' && url.pathname === '/') {
        const html = await fs.readFile(path.join(root, 'panel/ui/panel.html'), 'utf8');
        return send(res, 200, {'content-type': 'text/html; charset=utf-8'}, await corePanel.insertConnection(html));
      }
      const coreAsset = url.pathname.match(/^\/engines\/iz2s-core\/([a-z.]+)$/)?.[1];
      if (req.method === 'GET' && coreAsset && Object.hasOwn(corePanel.assets, coreAsset)) {
        return await sendFile(res, path.join(corePanel.ui, coreAsset), {'content-type': corePanel.assets[coreAsset]});
      }
      if (req.method === 'GET' && assets[url.pathname]) {
        const [name, type] = assets[url.pathname];
        return await sendFile(res, path.join(root, 'panel/ui', name), {'content-type': type});
      }
      if (req.method === 'GET' && url.pathname === '/blitbun-encoder.js') {
        return await sendFile(res,path.join(root,'panel/ui/blitbun-encoder.js'),{
          'content-type':'text/javascript; charset=utf-8',
          'content-security-policy':"default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; connect-src 'self'",
        });
      }
      if (req.method === 'GET' && Object.hasOwn(blitAssets,url.pathname)) {
        const [file,type] = blitAssets[url.pathname];
        return await sendFile(res,path.join(root,'modules/BlitBun/server/lib',file),{'content-type':type});
      }
      if (req.headers['x-iz-key'] !== token)
        return reply(res, 403, {
          error: 'Reopen the application using its launch link',
        });
      if (req.method === 'GET' && url.pathname === '/api/iz2sciv/sd-package') {
        // Local download is also available in preview; it never contacts a console.
        const {buildSdPackage} = await import('../engines/IzCivSwitchFS/tools/sd-package.mjs');
        const archive = await buildSdPackage();
        return send(res,200,{'content-type':'application/zip',
          'content-disposition':'attachment; filename="'+archive.filename+'"',
          'content-length':archive.bytes.length},archive.bytes);
      }
      if (req.method === 'GET' && url.pathname === '/api/apps/download') {
        if (closing) return reply(res,503,{error:'The panel is shutting down.'});
        const id = url.searchParams.get('id') || '';
        if (!/^[a-z0-9][a-z0-9-]{0,46}$/.test(id))
          return reply(res,400,{error:'Choose an application from the library.'});
        if (exportingApp) return reply(res,503,{error:'An app download is already being prepared.'});
        exportingApp = true;
        try {
          const module = await require('../core/modules.cjs').read(root,id,{pinned:false});
          if (!module.descriptor.switch || !require('../core/packages/store-catalog.cjs').visible(module.descriptor))
            return reply(res,400,{error:'That application is unavailable for download.'});
          const app = await require('../engines/iz2s-core/tools/prepare-sd-app.cjs').packageModule(module);
          return send(res,200,{
            'content-type':app.packed?'application/zip':'application/octet-stream',
            'content-disposition':'attachment; filename="'+app.name+'"',
            'content-length':app.bytes.length,
          },app.bytes);
        } finally { exportingApp = false; }
      }
      if (req.method === 'GET' && url.pathname === '/api/extensions') {
        const inventory = await require('../core/modules.cjs').list(root);
        return reply(res, 200, {
          ...require('../core/extensions.cjs').catalog(root),
          modules: inventory.modules.map((m) => m.descriptor).filter(require('../core/packages/store-catalog.cjs').visible),
          moduleErrors: inventory.errors,
        });
      }
      if (req.method === 'GET' && url.pathname === '/api/state') {
        return reply(res, 200, await withCoreSd(await snapshot()));
      }
      if (req.method === 'GET' && url.pathname === '/api/iz2s-core/sd/volumes')
        return reply(res, 200, await coreController.sdStore.volumes());
      if (req.method === 'GET' && url.pathname === '/api/iz2s-core/pc-tool') {
        const view = url.searchParams.get('view'), open = url.searchParams.get('open') === '1';
        const readTool = () => corePanel.pcTool(view, fetch, {preview:manager.preview,root,out:coreOut,open});
        const tool = view === 'files' && open && !manager.preview ? await changeLauncherFiles(readTool) : await readTool();
        return reply(res, 200, tool);
      }
      if(req.method==='GET'&&url.pathname==='/api/iz2s-core/ftp/status'){
        const files=require('../engines/iz2s-core/tools/pc-link-files.cjs');
        return reply(res,200,manager.preview?{enabled:false,online:false}:await files.ftpStatus());
      }
      if(req.method==='GET'&&url.pathname==='/api/iz2s-core')return reply(res,200,await coreController.state());
      if (req.method === 'GET' && url.pathname === '/api/iz2s-core/log')
        return await coreController.downloadLog(url.searchParams.get('id'), res);
      if (req.method === 'GET' && url.pathname === '/api/startup') {
        return reply(res, 200, await withCoreSd(await snapshot({startup: true})));
      }
      if (req.method === 'GET' && url.pathname === '/api/loader-source') {
        const source = await manager.source(url.searchParams.get('id'));
        return await sendFile(res, source.file, {
          'content-type': 'text/plain; charset=utf-8',
          'content-disposition': 'attachment; filename=' + source.name + '.bas',
        });
      }
      if (req.method === 'GET' && url.pathname === '/api/switch-keyboard/firmware')
        return await sendFile(res, require('../hardware/switch-keyboard.cjs').firmwarePath(root), {'content-type':'application/octet-stream','content-disposition':'attachment; filename="iz2skeyboard.nro"'});
      if (req.method === 'GET' && url.pathname === '/api/switch-keyboard/package') {
        const archive = require('../hardware/switch-keyboard.cjs').packagePath(root);
        return await sendFile(res, archive, {'content-type':'application/zip','content-disposition':`attachment; filename="${path.basename(archive)}"`});
      }
      if (req.method === 'GET' && url.pathname === '/api/cardputer/firmware') {
        const firmware=require('../hardware/cardputer.cjs'),profile=url.searchParams.get('profile')||'sb4';
        return await sendFile(res, firmware.firmwarePath(root,profile), {'content-type':'application/octet-stream','content-disposition':'attachment; filename="'+firmware.profileFile(profile)+'"'});
      }
      if (req.method === 'GET' && url.pathname === '/api/cardputer/package')
        return await sendFile(res, require('../hardware/cardputer.cjs').packagePath(root), {'content-type':'application/zip','content-disposition':'attachment; filename="iz2s-cardputer-adv-sd.zip"'});
      if (req.method === 'GET' && url.pathname === '/api/input-package') {
        if(url.searchParams.get('format')==='installer'){
          const file=await require('../hardware/agents.cjs').installer(root,url.searchParams.get('profile')||'sb4');
          return await sendFile(res,file,{'content-type':'application/octet-stream','content-disposition':'attachment; filename="'+path.basename(file)+'"'});
        }
        const file=await require('../hardware/agents.cjs').archive(root);
        return await sendFile(res, file, {'content-type':'application/gzip','content-disposition':'attachment; filename="iz2s-arm-input.tar.gz"'});
      }
      if (req.method === 'GET' && url.pathname === '/api/setup-report')
        return reply(res, 200, {
          text: await manager.setupReport(url.searchParams.get('id')),
        });
      if (req.method === 'GET' && url.pathname === '/api/log') {
        const id=url.searchParams.get('id'),coreLog=await coreController.history.log(id);
        return reply(res,200,coreLog||{events:await manager.historyLog(id)});
      }
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
      if (closing) return reply(res,503,{error:'The panel is shutting down.'});
      if (url.pathname === '/api/platform/shutdown') {
        if (manager.preview || !shutdownHook) return reply(res,503,{error:'This panel cannot be handed over yet.'});
        if (exportingApp) return reply(res,503,{error:'An application download is still active.'});
        closing = true;
        try {
          await mutation;
          coreController.assertCanClose();
          await require('../core/session/panel-handover.cjs').waitForIdle({root,port:actualPort,pid:process.pid,timeout:0});
        } catch (error) { closing = false; throw error; }
        reply(res,200,{ok:true});
        setImmediate(async () => {
          try { if (!(await shutdownHook())?.closed) closing = false; }
          catch (error) { closing = false; console.error(error); }
        });
        return;
      }
      if (url.pathname === '/api/iz2s-core/pc/install-nro') {
        if (manager.preview) return reply(res,403,{error:'Preview cannot install console apps'});
        if (req.headers['content-type'] !== 'application/octet-stream')
          return reply(res,400,{error:'Send one NRO file.'});
        const result = await require('../engines/iz2s-core/tools/pc-link-service.cjs').installNro(
          coreOut, req, url.searchParams.get('name')
        );
        return reply(res,200,result);
      }
      if (url.pathname === '/api/iz2s-core/pc/install') {
        if (manager.preview) return reply(res,403,{error:'Preview cannot install console apps'});
        if (req.headers['content-type'] !== 'application/octet-stream')
          return reply(res,400,{error:'Send one application file.'});
        const result = await require('../engines/iz2s-core/tools/pc-link-service.cjs').install(coreOut,req);
        return reply(res,200,result);
      }
      if (url.pathname === '/api/iz2s-core/sd/import') {
        if (manager.preview) return reply(res,403,{error:'Preview cannot access the IZ2S SD store'});
        if (req.headers['content-type'] !== 'application/octet-stream')
          return reply(res,400,{error:'Send one application file.'});
        return reply(res,200,await coreController.sdStore.importApp(req,url.searchParams.get('format'),url.searchParams.get('selection'),url.searchParams.get('name')));
      }
      if(url.pathname==='/api/iz2s-core/pc/refresh'){
        require('../engines/iz2s-core/tools/pc-link-service.cjs').invalidateInventory();
        return reply(res,200,{ok:true});
      }
      const browserMedia=url.pathname.match(/^\/api\/iz2s-core\/media\/browser\/(begin|frame|audio|stop)$/)?.[1];
      if(browserMedia){
        if(manager.preview)return reply(res,403,{error:'Preview cannot share a PC source.'});
        if(browserMedia==='begin')return reply(res,200,await blitBrowser.begin());
        if(browserMedia==='stop')return reply(res,200,await blitBrowser.stop((await readBody(req)).id));
        return reply(res,200,await blitBrowser.relay(req,browserMedia));
      }
      const body = await readBody(req, ['/api/civswitch-ldn','/api/iz2sciv'].includes(url.pathname) ? 131072 : 16384);
      if(url.pathname==='/api/iz2s-core/pc/connection'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot connect to the console.'});
        const link=require('../engines/iz2s-core/tools/pc-link-service.cjs');
        return reply(res,200,await link.setEnabled(coreOut,body.enabled));
      }
      if(url.pathname==='/api/iz2s-core/ftp/close'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot connect to the console.'});
        const files=require('../engines/iz2s-core/tools/pc-link-files.cjs');
        const result=await changeLauncherFiles(async()=>{
          if(await files.closeDirectFtp())return workspace.stop(root,'ftp-files');
          return {ok:true};
        });
        return reply(res,200,result);
      }
      if(url.pathname==='/api/iz2s-core/ftp/open'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot connect to the console.'});
        const files = require('../engines/iz2s-core/tools/pc-link-files.cjs');
        return reply(res,200,await changeLauncherFiles(() => files.openFtp(root,{host:body.host,hosts:()=>require('../engines/iz2s-core/tools/pc-link-service.cjs').consoleAddresses(coreOut)},{panelUrl:origin})));
      }
      if(url.pathname.startsWith('/api/iz2s-core/media/')){
        if(manager.preview)return reply(res,403,{error:'Preview cannot share a PC source.'});
        const media=require('../engines/iz2s-core/tools/pc-media-control.cjs');
        if(url.pathname==='/api/iz2s-core/media/blitbun-profile')return reply(res,200,await blitBrowser.profile(body.width));
        if(url.pathname==='/api/iz2s-core/media/windows')return reply(res,200,{windows:await media.windows()});
        const action=url.pathname.match(/^\/api\/iz2s-core\/media\/(start|stop)$/)?.[1];
        if(action){
          const execute=()=>media.requestMedia(body.id,action,coreOut,body.window);
          return reply(res,200,await (body.id==='blitbun'?blitBrowser.native(execute):execute()));
        }
      }
      if(url.pathname==='/api/iz2s-core/pc/install-library'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot install console apps'});
        const id = body.id;
        if(typeof id !== 'string' || !/^[a-z0-9][a-z0-9-]{0,46}$/.test(id))
          return reply(res,400,{error:'Choose an application from the library.'});
        const archive = require('../core/archive.cjs');
        const module = await require('../core/modules.cjs').read(root, id, {pinned:false});
        if(!module.descriptor.switch || !require('../core/packages/store-catalog.cjs').visible(module.descriptor))
          return reply(res,400,{error:'That application is unavailable for installation.'});
        const nro = await require('../engines/iz2s-core/tools/nro-package.cjs').moduleProgram(module, archive.switchApp(module));
        const bytes = nro || await fs.readFile(module.packagePath);
        if (!nro && archive.sha(bytes) !== module.sha256)
          return reply(res,409,{error:'The PC package changed. Refresh the library and retry.'});
        const upload = require('node:stream').Readable.from([bytes]);
        upload.headers = {'content-length': String(bytes.length)};
        const pcLink = require('../engines/iz2s-core/tools/pc-link-service.cjs');
        return reply(res,200,nro ? await pcLink.installNro(coreOut,upload,id+'.nro') : await pcLink.install(coreOut,upload));
      }
      if(url.pathname==='/api/iz2s-core/pc/remove'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot remove console apps'});
        return reply(res,200,await require('../engines/iz2s-core/tools/pc-link-service.cjs').remove(coreOut,body.id,body.format));
      }
      if(url.pathname==='/api/iz2s-core/pc/menu-update'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot update IZ2S'});
        return reply(res,200,await require('../engines/iz2s-core/tools/pc-link-service.cjs').menuUpdate(coreOut,{retry:body.retry===true}));
      }
      if(url.pathname==='/api/iz2s-core/pc/reset'){
        if(manager.preview)return reply(res,403,{error:'Preview cannot reset PC Link'});
        return reply(res,200,await coreController.reset());
      }
      if(url.pathname==='/api/iz2s-core/setup')return reply(res,200,await coreController.setup(body));
      if(url.pathname==='/api/iz2s-core/start')return reply(res,200,await coreController.start(body));
      if(url.pathname==='/api/iz2s-core/stop')return reply(res,200,await coreController.stop());
      coreController.assertAvailable(url.pathname);
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
      if(url.pathname==='/api/cardputer/cancel'){
        const cancelled=await manager.cancelCardputerSD(body);
        stateRead=null;lastSnapshot=null;
        return reply(res,200,cancelled);
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
        if (url.pathname === '/api/iz2s-core/session/reset') {
          if (manager.preview) throw Error('Session reset is unavailable in Preview.');
          return changeLauncherFiles(async () => {
            const current = await snapshot();
            if (current.active || coreController.running()) throw Error('Stop the current operation first.');
            const pcLink = require('../engines/iz2s-core/tools/pc-link-service.cjs');
            const link = await pcLink.status();
            if (link?.fileTransfers) throw Error('Wait for file transfers to finish before resetting the session.');
            await require('../engines/iz2s-core/tools/pc-link-files.cjs').closeDirectFtp();
            await pcLink.reset(coreOut);
            await workspace.clearLauncherFiles(root);
            return {ok:true};
          });
        }
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
            sessionId:undefined});
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
              ownerPid:process.pid,panelInstance:undefined,sessionId:undefined})};
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
        if (url.pathname === '/api/switch-keyboard/prepare') return manager.prepareSwitchKeyboardSD();
        if (url.pathname === '/api/switch-keyboard/install') return manager.installSwitchKeyboard();
        if (url.pathname === '/api/cardputer/confirm-usb') return manager.confirmCardputerUsb();
        if (url.pathname === '/api/cardputer/prepare') return manager.prepareCardputerSD(body);
        if (url.pathname === '/api/cardputer/install') return manager.installCardputerSD(body);
        if (url.pathname === '/api/input/install') return manager.installInput(body);
        if (url.pathname === '/api/hardware/open') return manager.openHardwareApp();
        if (url.pathname === '/api/check') return manager.refresh(true);
        if (url.pathname === '/api/connection')
          return manager.configureConnection(body);
        if(url.pathname==='/api/connection/restore')return manager.restoreConnection();
        if (url.pathname === '/api/start') return manager.begin(body);
        if (url.pathname === '/api/setup') return manager.setupAction(body);
        if (url.pathname === '/api/iz2sciv') return manager.iz2scivAction(body);
        if (url.pathname === '/api/civswitch') return manager.civswitchAction(body);
        if (url.pathname === '/api/civswitch-ldn') return manager.civswitchLdnAction(body);
        if (url.pathname === '/api/retry') return manager.retry();
        if (url.pathname === '/api/iz2s-core/sd/select') return coreController.sdStore.select(body.target);
        if (body.store === 'core-sd') {
          if (url.pathname === '/api/load') return coreController.sdStore.load(body.ids,body.selection);
          if (url.pathname === '/api/unload') return coreController.sdStore.unload(body.ids,body.selection);
          if (url.pathname === '/api/sd/refresh') return coreController.sdStore.refresh(body.selection);
          throw Error('Unknown IZ2S SD operation');
        }
        if (url.pathname === '/api/load') {
          await manager.loadPackages(body.ids,{target:body.target,targets:body.targets});
          return { ok: true };
        }
        if(url.pathname==='/api/unload'){
          if(manager.preview)throw Error('Preview cannot remove console modules');
          await manager.unloadPackages(body.ids,body.target);
          return {ok:true};
        }
        if(url.pathname==='/api/sd/refresh')return manager.refreshSdInventory();
        if (url.pathname === '/api/delete') {
          if(!await coreController.history.remove(body.id))await manager.remove(body.id);
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
      fail(res, e, /^\/api\/iz2s-core(?:[/?]|$)/.test(req.url));
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
    setShutdown: hook => { shutdownHook = hook; },
    url: 'http://127.0.0.1:' + actualPort + '/#' + token,
    close: async () => {
      coreController.assertCanClose();
      await blitBrowser.close();await require('../core/module-settings.cjs').close(root);
      await new Promise(r => {
        server.close(r);
        // Handover has rejected active transfers and blocked new mutations.
        // Read-only UI polling must not retain a closing panel indefinitely.
        if (closing) server.closeAllConnections();
      });
    },
  };
}
module.exports = { createWindowServer };
