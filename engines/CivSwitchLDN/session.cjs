'use strict';
const fs = require('node:fs/promises'), path = require('node:path'), crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const {readJson, saveJson, redact, createLedger} = require('../../core/session/ledger.cjs');
const integration = require('./integration.cjs');
const {localIPv4} = require('../CivSwitch/session.cjs');
const PORT = 9026;
const discovery = require('../CivSwitch/runtime/discovery.cjs');
function create({root, settings, connections, configuration, preview = false, launch = spawn,
  inspect = integration.inspect, now = Date.now, pause = ms => new Promise(resolve => setTimeout(resolve, ms)),
  platform=process.platform, discoverNetwork=discovery.discover, protocolProbe=discovery.probe, loaderWaitMs = 120000, discoverLoader = require('./loader.js').discoverLoader}) {
  const p = integration.paths(root), file = path.join(root, 'data/state/civswitchldn.json'), ledger = createLedger(root);
  let saved = {phase: 'idle', host: '', hostMode:'auto', phy: '', hostMac: '', chatDelay: 10, handshakeScale: 1, index: 0, total: 19};
  let task = null, child = null, interrupted = false, ready = false, verifiedHost = '', setup = null, checking = null;
  let checkedAt = 0, posting = false, logQueue = Promise.resolve(), lastAttempt = null;
  const events = [];
  let restoredAttempt=false, search=null;
  const loaded = readJson(file, {}).then(value => {
    saved = {...saved, ...value};
    // Older panels required a LAN address before LDN even started. Do not
    // carry that unverified address into the automatic Wi-Fi loader search.
    if(!Object.hasOwn(value,'hostMode')){saved.host='';saved.hostMode='auto';}
    if (saved.phase === 'ready') saved.phase = 'entered';
    if (['sending', 'restoring', 'checking', 'installing', 'preparing', 'stopping'].includes(saved.phase)) {
      saved.phase = 'interrupted';
      saved.error = 'The previous operation was interrupted. Check Wi-Fi and the game loader before starting a fresh lobby.';
    }
    restoredAttempt=!!saved.error;
  });
  const persist = () => saveJson(file, saved);
  function log(message) {
    const entry = {at: new Date().toISOString(), type: 'civswitch-ldn', message: redact(message).slice(-1600)};
    events.push(entry); if (events.length > 250) events.shift();
    if (saved.id) logQueue = logQueue.then(() => ledger.append(saved.id, entry.type, entry.message)).catch(() => {});
  }
  async function refresh(force = false) {
    await loaded;
    // Imports, helper capabilities and installed packages are setup state, not
    // live transport telemetry. Actions refresh them before any side effect.
    if (task || !force && setup && now() - checkedAt < 300000) return {ok: true};
    if (!checking) checking = Promise.resolve().then(() => inspect(root)).then(async result => {
      setup = result; checkedAt = now();
      // Linux can renumber a radio after reconnecting it. Recover only when
      // there is one unambiguous adapter, and never while an operation owns it.
      if (!task && !result.radios.includes(saved.phy) && result.radios.length === 1) {
        saved.phy = result.radios[0];
        await persist();
      }
      return {ok: true};
    }).finally(() => {checking = null;});
    return checking;
  }
  function target(body) {
    const host = body.automatic===true ? '' : body.host?.trim() || saved.host || '';
    if (host && !localIPv4(host)) throw Error('Enter the Switch IPv4 address in your local network');
    return host;
  }
  function configure(body) {
    if(body.networkMode!==undefined){if(!['lan','wifi'].includes(body.networkMode))throw Error('Choose LAN or Wi-Fi');saved.networkMode=body.networkMode;}
    if (body.host !== undefined) {
      const host=body.host.trim();
      if (host && !localIPv4(host)) throw Error('Enter the Switch IPv4 address in your local network');
      if (saved.host !== host) {ready = false; verifiedHost = '';}
      saved.host=host;saved.hostMode=host?'manual':'auto';
    }
    if (body.phy !== undefined && body.phy !== '') {
      if (!/^phy\d+$/.test(body.phy) || !setup.radios.includes(body.phy)) throw Error('Choose an available Wi-Fi radio');
      saved.phy = body.phy;
    }
    if (body.hostMac !== undefined) {
      if (body.hostMac && !/^(?:[a-f0-9]{2}:){5}[a-f0-9]{2}$/i.test(body.hostMac)) throw Error('Enter a valid LDN host MAC or leave it empty');
      saved.hostMac = body.hostMac.toLowerCase();
    }
    if (body.chatDelay !== undefined) {
      const value = Number(body.chatDelay);
      if (!Number.isInteger(value) || value < 10 || value > 5000) throw Error('Chat delay must be between 10 and 5000 ms');
      saved.chatDelay = value;
    }
    if (body.handshakeScale !== undefined) {
      const value = Number(body.handshakeScale);
      if (!Number.isFinite(value) || value < 1 || value > 10) throw Error('Handshake scale must be between 1 and 10');
      saved.handshakeScale = value;
    }
  }
  function run(program, args, {timeoutMs = 10 * 60 * 1000} = {}) {
    return new Promise((resolve, reject) => {
      let output = '', buffer = '', finished = false;
      const handle = launch(program, args, {cwd: p.folder, stdio: ['ignore', 'pipe', 'pipe'],
        env: {...process.env, ...configuration?.env, NODE_PATH: path.join(p.folder, 'node_modules')}});
      child = handle;
      // SIGTERM lets the network wrapper restore the radio. Never use a timer
      // to declare a still-running restoration complete or replay input.
      const timer = setTimeout(() => {log('Operation timed out; waiting for Wi-Fi restoration.'); handle.kill('SIGTERM');}, timeoutMs);
      function finish(error) {
        if (finished) return; finished = true; clearTimeout(timer);
        if (child === handle) child = null;
        error ? reject(error) : resolve(output);
      }
      handle.on('error', finish);
      for (const stream of [handle.stdout, handle.stderr]) stream.on('data', chunk => {
        output = (output + chunk).slice(-1024 * 1024); buffer += chunk;
        const lines = buffer.split(/[\r\n]+/); buffer = lines.pop().slice(-4096);
        for (const line of lines) if (line) {
          log(line);
          const total = /CIV7_LDN_PAYLOAD_LINES=(\d+)/.exec(line), progress = /Sending payload: \d+% \((\d+)\/(\d+) messages\)/.exec(line);
          if (total) saved.total = Number(total[1]);
          if (progress) {saved.index = Number(progress[1]); saved.total = Number(progress[2]);}
          if(line==='CIV7_LDN_NETWORK_RESTORED')saved.networkRestored=true;
          if (line.startsWith('[net] Restoring')) saved.phase = 'restoring';
        }
      });
      handle.on('close', (code, signal) => {
        if (buffer) log(buffer);
        finish(code === 0 && !interrupted ? null : Error(interrupted ? 'Operation stopped. Verify Wi-Fi restoration before retrying.' :
          output.slice(-1200) || 'CIV7 LAN process failed: ' + (signal || code)));
      });
    });
  }
  async function verifyLoader(host) {
    saved.phase = 'checking'; ready = false; await persist();
    const result = platform==='linux'&&setup.dependenciesReady ? await run(p.python, ['-u', path.join(root, 'engines/CivSwitch/probe.py'), host, String(PORT)]) :
      await protocolProbe(host,{signal:search?.signal,timeout:6000}) ? 'CIVSWITCH_LOADER_VERIFIED' : 'CIVSWITCH_LOADER_TIMEOUT';
    if (!result.includes('CIVSWITCH_LOADER_VERIFIED')) throw Error('The game loader did not confirm TCP 9026');
    ready = true; verifiedHost = host; saved.phase = 'ready'; log('Remote JS Loader verified on TCP 9026.');
  }
  async function waitForLoader(host, {once=false,automatic=false}={}) {
    saved.phase = 'checking'; await persist();
    const deadline = now() + (once?6000:loaderWaitMs);
    for (let attempt = 0;; attempt++) {
      if (interrupted) throw Error('CIV7 LAN stopped');
      if(!host){
        const found=automatic ? await discoverNetwork({mode:saved.networkMode||'auto',known:discovery.knownHosts(connections.snapshot(),saved.host),signal:search.signal,timeout:6000}) : await discoverLoader(saved.phy,Date.now()+Math.min(5000,Math.max(0,deadline-now())));
        if(found.length>1)throw Error('Several game loaders found. Enter the IP shown on your Switch.');
        if(interrupted)throw Error('CIV7 LAN stopped');
        if(found.length===1){host=found[0];saved.host=host;log('Game loader found at '+host+':'+PORT);await persist();}
        else if(once||now()>=deadline||attempt>=60)throw Error('No game loader found. Open Load Game, wait for Awaiting payload, and check LAN or Wi-Fi. You can enter the IP shown on Switch manually.');
      }
      if(host){
        try {await verifyLoader(host); return host;}
        catch (error) {
          if (interrupted || once || now() >= deadline || attempt >= 60 || !/CIVSWITCH_LOADER_(NOT_RUNNING|TIMEOUT|NETWORK_ERROR)/.test(error.message)) throw error;
        }
      }
      await pause(Math.min(2000, Math.max(0, deadline - now())));
    }
  }
  async function install(host) {
    if (!ready || verifiedHost !== host) throw Error('Verify the game loader at this address before installing the save');
    saved.phase = 'installing'; await persist();await require('../CivSwitch/save-installation.cjs').begin(root,'civswitch-ldn');
    const result = await run(p.python, ['-u', path.join(p.folder, 'installer/install.py'), host, String(PORT), path.join(p.folder, 'payloads/core.js')]);
    if (!result.includes('INSTALL DONE')) throw Error('Save installation was not confirmed');
    saved.phase = 'installed'; await require('../CivSwitch/save-installation.cjs').confirm(root,'civswitch-ldn'); ready = false; verifiedHost = ''; log('INSTALL DONE. Restart the game and open Load Game.');
  }
  async function action(body) {
    if (posting) throw Error('Wait for the current operation to finish');
    posting = true;
    try {
      await loaded;
      if (preview) throw Error('Preview cannot prepare dependencies or send console commands');
      if (task) throw Error('Wait for the current operation to finish');
      const command = body.action;
      if(command==='discard'){
        saved={phase:'idle',host:saved.host,hostMode:saved.hostMode,networkMode:saved.networkMode,phy:saved.phy,hostMac:saved.hostMac,chatDelay:saved.chatDelay,handshakeScale:saved.handshakeScale,index:0,total:19};
        restoredAttempt=false;ready=false;verifiedHost='';lastAttempt=null;events.length=0;await persist();return {ok:true};
      }
      await refresh(true);
      if (command === 'import-keys') {
        await integration.importKeys(root, body.content); await refresh(true); return {ok: true};
      }
      if (command === 'refresh') return {ok: true, prepared: !!(setup.supported && setup.keysReady && setup.dependenciesReady && setup.capabilitiesReady && setup.networkManager && setup.radios.includes(saved.phy))};
      if (command === 'configure') {configure(body); await persist(); return {ok: true};}
      if (!['prepare', 'start', 'check-loader', 'install'].includes(command)) throw Error('Unknown CIV7 LAN action');
      if (!setup.supported && command!=='check-loader') throw Error('First setup over LDN requires native Linux. Launch can use LAN or Wi-Fi.');
      configure(body);
      const host = command === 'prepare' ? '' : target(body);
      if (!['prepare','check-loader'].includes(command) && !setup.dependenciesReady) throw Error('Prepare PC dependencies in step 1');
      if (command === 'start') {
        if (body.confirm !== true) throw Error('Open a fresh Civilization VII wireless lobby and confirm it before starting');
        if (!setup.keysReady) throw Error('Add your own prod.keys in step 1 before starting');
        if (!setup.capabilitiesReady || !setup.networkManager || !setup.radios.includes(saved.phy)) throw Error('Complete the Wi-Fi radio setup and network permissions in step 1');
        if (saved.phase !== 'idle' && body.freshLobby !== true) throw Error('Confirm a fresh lobby before sending the payload again');
      }
      if ((command === 'install' || command === 'start' && body.installAfterSuccess === true) && body.deleteSingleSaves !== true)
        throw Error('Save installation deletes ALL single-player saves. Back up needed saves and confirm deletion first.');
      if (command === 'install' && (!host || !ready || verifiedHost !== host)) throw Error('Verify the game loader at this address before installing the save');
      saved.id = crypto.randomUUID(); saved.host = host || saved.host;
      saved.phase = command === 'prepare' ? 'preparing' : command === 'start' ? 'sending' : command === 'install' ? 'installing' : 'checking';
      delete saved.error; delete saved.retryStep; restoredAttempt=false; interrupted = false; search=new AbortController();
      if (command === 'start') {saved.index = 0; saved.networkRestored=false; ready = false; verifiedHost = '';}
      await persist();
      const attempt = {id: saved.id, upstream: true, loaderId: 'civswitch-ldn', kind: 'civswitch-ldn', status: 'starting', created: new Date().toISOString()};
      await ledger.write(attempt);
      task = Promise.resolve().then(async () => {
        if (interrupted) throw Error('CIV7 LAN stopped');
        if (command === 'prepare') {
          const output = await run(process.execPath, [path.join(p.folder, 'setup.cjs')]);
          if (!output.includes('CIV7_LDN_DEPENDENCIES_READY')) throw Error('Dependency preparation was not confirmed');
          saved.phase = 'idle'; checkedAt = -Infinity;
        } else if (command === 'check-loader') await waitForLoader(host,{once:true,automatic:body.automatic===true});
        else if (command === 'install') await install(host);
        else {
          const output = await run(process.execPath, [path.join(p.folder, 'panel-runner.cjs'), saved.phy, saved.hostMac, String(saved.chatDelay), String(saved.handshakeScale)]);
          if (!output.includes('CIV7_LDN_DELIVERY_COMPLETE') || !output.includes('CIV7_LDN_NETWORK_RESTORED')) throw Error('Delivery or Wi-Fi restoration was not confirmed. Check the game before starting over.');
          saved.index = saved.total; saved.phase = 'entered'; await persist();
          const resolvedHost=await waitForLoader(host,{automatic:true});
          if (body.installAfterSuccess === true) {if (interrupted) throw Error('CIV7 LAN stopped'); await install(resolvedHost);}
        }
        attempt.status = 'complete';
      }).catch(error => {
        ready = false; verifiedHost = '';
        saved.phase = command === 'check-loader' || saved.index === saved.total && command === 'start' ? 'entered' : 'interrupted';
        saved.error = /CIVSWITCH_LOADER_(NOT_RUNNING|TIMEOUT|NETWORK_ERROR)/.test(error.message) ?
          'The game loader is not responding on TCP 9026. Check the console IP and network; do not repeat delivery just to check it.' : redact(error.message);
        if(command==='start'&&saved.index===0&&/LDN connection timed out/.test(error.message))saved.error=saved.networkRestored?'The LDN lobby was found, but the connection timed out before any commands were sent. Wi-Fi was restored. Reopen the Wireless lobby before another attempt.':'The LDN lobby was found, but the connection timed out before any commands were sent. Wi-Fi restoration was not confirmed; restore Wi-Fi before another attempt.';
        if(command==='start'){
          if(!saved.networkRestored)saved.phase='interrupted';
          if(saved.phase==='interrupted')saved.retryStep=saved.networkRestored?3:1;
        }
        attempt.status = interrupted ? 'cancelled' : 'failed'; attempt.error = saved.error; log(saved.error);
      }).finally(async () => {
        try {await logQueue; await persist(); await ledger.write(attempt); lastAttempt = attempt;}
        finally {task = null; search=null;}
      });
      return {ok: true, started: true};
    } finally {posting = false;}
  }
  async function stop({beforeSignal} = {}) {
    await loaded;
    if (task) {
      interrupted = true; search?.abort(); saved.phase = 'stopping';
      if (child) child.kill('SIGTERM');
      await task;
    }
    await beforeSignal?.(); ready = false; verifiedHost = ''; return {ok: true};
  }
  async function snapshot() {
    await loaded;
    if (!setup) await refresh();
    const prepared = !!(setup.supported && setup.keysReady && setup.dependenciesReady && setup.capabilitiesReady && setup.networkManager && setup.radios.includes(saved.phy));
    const serverDetail = ready ? 'PC connected to the game loader. ADV is not required.'
      : prepared ? 'PC Wi-Fi radio ready. ADV and USB input are not required.'
      : !setup.supported ? 'First setup over LDN requires native Linux. Launch can use LAN or Wi-Fi.'
      : !setup.keysReady || !setup.dependenciesReady ? 'Complete PC setup and add prod.keys in step 1.'
      : !setup.capabilitiesReady ? 'Enable network permissions using the command below.'
      : !setup.networkManager ? 'Start NetworkManager before preparing the Wi-Fi radio.'
      : !setup.radios.length ? 'No Wi-Fi radio found. Connect an adapter and refresh status.'
      : 'Select an available Wi-Fi radio in step 2.';
    const network = connections.snapshot();
    return {preview, checks: {ready: ready||prepared, items: [
      {id: 'server', ready: ready||prepared, pending: !!task, detail: serverDetail},
      {id: 'lan', ready, pending: !ready, detail: ready ? 'Remote JS Loader verified on TCP 9026' : 'The console is discovered when LDN starts. The game loader opens TCP 9026 after delivery.'},
    ]}, active: task ? {id: saved.id, kind: 'civswitch-ldn', status: saved.phase === 'stopping' ? 'stopping' : saved.phase === 'sending' ? 'typing' : 'starting', progress: {line: saved.index, total: saved.total}} : null,
      lastAttempt, history: await ledger.listAttempts(), historyAll:true, log: events, library: [], jobs: [], installed: [], hostModules: {}, connected: ready,
      settings: {consoleHost: settings.consoleHost, consoleAddress: saved.host, consolePort: PORT},
      connections: {...network, locked: !!task}, sourcePreparation: {ready: true, preloaded: true, lines: 0}, setupWizard: false, hardStopAvailable: false,
      civswitchLdn: {...saved, restoredAttempt, busy: !!task, loaderReady: ready, setup, prepared, port: PORT, saveName: 'IZ2S.Civ7Save',
        suggestedHost: saved.host || '',saveInstallation:await require('../CivSwitch/save-installation.cjs').read(root,saved)}};
  }
  return {action, snapshot, refresh, stop, waitForOperation:()=>task||Promise.resolve(), hardStop: stop, ownsListener: () => !!task,
    begin: () => {throw Error('Use the CIV7 LAN setup steps');}, setupAction: action,
    configureConnection: async body => {if (task) throw Error('Stop the current operation first'); await connections.update(body); if (body.id === 'lan') saved.host = ''; ready = false; await persist(); return {ok: true};},
    historyLog: id => ledger.all(id), setupReport: async () => JSON.stringify({...saved, setup}),
    remove: id => ledger.removeFailure(id, task ? saved.id : undefined), retry: async () => ({ok: true}), dismiss() {}};
}
module.exports = {create};
