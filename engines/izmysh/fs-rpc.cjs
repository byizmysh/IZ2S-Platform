const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const crypto = require('node:crypto');

// One bounded local endpoint exchange. No busy retry or uncertain replay.
function requestEndpoint(socketPath, value, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    let text = '', settled = false;
    const socket = net.createConnection(socketPath);
    const finish = (error, result) => { if (settled) return; settled = true; clearTimeout(timer); socket.destroy(); error ? reject(error) : resolve(result); };
    const timer = setTimeout(() => finish(Error('FS admission endpoint outcome unknown')), timeoutMs);
    socket.setEncoding('utf8');
    socket.on('connect', () => socket.write(JSON.stringify(value) + '\n'));
    socket.on('data', part => { text += part; if (text.length > 65536) finish(Error('FS admission endpoint response exceeded bound')); });
    socket.on('error', () => finish(Error('FS admission endpoint transport unknown')));
    socket.on('end', () => { try { const result = JSON.parse(text); if (result.error) throw Error(); finish(null, result); } catch { finish(Error('FS admission endpoint response unverified')); } });
  });
}
function cleanupAck(child, value, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    if (!child?.connected || typeof child.send !== 'function') { reject(Error('FS admission cleanup IPC unavailable')); return; }
    const requestId = crypto.randomUUID(); let settled = false;
    const finish = error => { if (settled) return; settled = true; clearTimeout(timer); child.off('message', message); child.off('close', closed); error ? reject(error) : resolve(); };
    const closed = () => finish(Error('FS admission cleanup child exited'));
    const message = reply => {
      if (reply?.requestId !== requestId) return;
      if (reply.type !== 'native-admission-cleanup-ready' || reply.authorityId !== value.authorityId ||
        reply.generation !== value.generation || reply.cleanupMode !== true)
        finish(Error('FS admission cleanup acknowledgement differs'));
      else finish();
    };
    const timer = setTimeout(() => finish(Error('FS admission cleanup acknowledgement unknown')), timeoutMs);
    child.on('message', message); child.once('close', closed);
    child.send({type: 'native-admission-cleanup', requestId, ...value}, error => { if (error) finish(Error('FS admission cleanup IPC failed')); });
  });
}
function boundedOutcome(promise, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(Error('FS admission cleanup completion unknown')), timeoutMs);
    Promise.resolve(promise).then(value => {clearTimeout(timer); resolve(value);}, error => {clearTimeout(timer); reject(error);});
  });
}

// Run the upstream --rpc acquisition before graphics, sharing the existing
// serialized, authenticated native bridge rather than opening another listener.
async function start({directory, source = path.join(__dirname, 'upstream'),
  startEngine = require('./upstream/run.cjs').startEngine,
  checkEngine = require('./upstream/run.cjs').checkEngine, admission}) {
  const stateFile = path.join(directory, 'fs-rpc-state.json');
  const workspace = path.join(directory, 'fs-rpc');
  const state = {mode: 'held-fs-rpc', state: 'starting', started: new Date().toISOString(), workspace};
  const save = async () => {
    const temporary = stateFile + '.tmp';
    await fs.writeFile(temporary, JSON.stringify(state, null, 2) + '\n', {mode: 0o600});
    await fs.rename(temporary, stateFile);
  };
  let engine, staged, admissionHost, endpointSocket;
  try {
    require('./upstream/engine/check-runtime.cjs').verify(source);
    await save();
    await fs.mkdir(workspace, {mode: 0o700});
    for (const name of ['shared', 'switch2'])
      await fs.cp(path.join(source, 'engine', name), path.join(workspace, name), {recursive: true});
    // Upstream RPC has no bearer-token support; only this per-run copy changes.
    if (admission) {
      const expectedConnection = JSON.parse(await fs.readFile(path.join(directory, 'connection.json'), 'utf8'));
      staged = await require('./admission/overlay.cjs').prepare({workspace, admission, expectedConnection});
      admissionHost = require('./admission/scoped-rpc.cjs').open({configurationFile: admission.configurationFile,
        clientModule: path.join(workspace, 'shared/admission-client.cjs'), installIpc: false});
      state.nativeAdmission = {authorityId: staged.authorityId, session: staged.session,
        outerDispatchFenced: false, nestedSdkScoped: false, runtimeConfirmed: false};
      state.admissionSources = staged.sourceHashes;
    } else await fs.copyFile(path.join(directory, 'shared/rpc.cjs'), path.join(workspace, 'shared/rpc.cjs'));
    await fs.copyFile(path.join(directory, 'connection.json'), path.join(workspace, 'connection.json'));
    await fs.chmod(path.join(workspace, 'connection.json'), 0o600);
    engine = await startEngine(workspace, null, true);
    const done = engine.finished.then(async outcome => {
      try {checkEngine(outcome);} catch (error) {
        if (admissionHost) {await admissionHost.admission.retainAuthority(); throw Error('FS admission engine outcome unknown; retained');}
        throw error;
      }
    });
    done.catch(() => {});
    const ready = await engine.ready;
    const proof = JSON.parse(await fs.readFile(path.join(workspace, 'switch2/fs-execution-current.json'), 'utf8'));
    const connection = JSON.parse(await fs.readFile(path.join(directory, 'connection.json'), 'utf8'));
    if (!proof.verified || !proof.restored || !proof.cleanup?.deferred ||
      !proof.retainedEndpoint?.ready || ready.session !== connection.addresses.session ||
      proof.retainedEndpoint.session !== connection.addresses.session)
      throw Error('Held FS RPC proof does not match this live console session');
    if (admissionHost) {
      endpointSocket = proof.retainedEndpoint.socketPath;
      if (typeof endpointSocket !== 'string' || !path.isAbsolute(endpointSocket)) throw Error('FS admission endpoint proof missing');
      const observed = await requestEndpoint(endpointSocket, {command: 'native-admission-status'});
      if (observed.authorityId !== staged.authorityId || observed.session !== ready.session ||
        observed.outerDispatchFenced !== true || observed.nestedSdkScoped !== true || observed.runtimeConfirmed !== true)
        throw Error('FS admission runtime not confirmed by actual endpoint');
      state.nativeAdmission = {authorityId: observed.authorityId, session: observed.session,
        outerDispatchFenced: true, nestedSdkScoped: true, runtimeConfirmed: true};
    }
    Object.assign(state, {state: 'ready', verified: true, session: ready.session,
      readyAt: new Date().toISOString(), proof: path.join(workspace, 'switch2/fs-execution-current.json')});
    await save();
    console.log('FS RPC ready (--rpc): native FS execution verified; endpoint retained.');
    let closeStarted = false;
    return {done, state, async close({admissionCleanup} = {}) {
      if (admissionHost && closeStarted) throw Error('FS admission cleanup cannot be retried');
      closeStarted = true;
      state.state = 'releasing'; await save();
      try {
        if (admissionHost) {
          if (admissionCleanup?.authorityId !== staged.authorityId || !/^[a-f0-9]{64}$/.test(admissionCleanup?.permit || '') ||
            !Number.isSafeInteger(admissionCleanup.generation) || admissionCleanup.generation < 0)
            throw Error('Exact FS admission cleanup authority required');
          await cleanupAck(engine.child, admissionCleanup, Math.min(staged.timeoutMs, 5000));
          const acknowledged = await requestEndpoint(endpointSocket, {command: 'native-admission-cleanup', admissionCleanup}, Math.min(staged.timeoutMs, 5000));
          if (acknowledged.cleanupMode !== true || acknowledged.authorityId !== staged.authorityId ||
            acknowledged.generation !== admissionCleanup.generation) throw Error('FS endpoint cleanup acknowledgement differs');
        }
        checkEngine(await (admissionHost ? boundedOutcome(engine.release(), staged.timeoutMs) : engine.release())); state.state = 'closed';
      }
      catch (error) {
        if (admissionHost) {await admissionHost.admission.retainAuthority(); state.resourcesRetained = true;}
        state.state = 'failed'; state.error = admissionHost ? 'FS admission cleanup retained; no retry' : error.message; throw error;
      }
      finally {state.closedAt = new Date().toISOString(); await save();}
    }};
  } catch (error) {
    state.state = 'failed'; state.error = admission ? 'FS admission initialization failed' : error.message;
    const pipeline = await fs.readFile(path.join(workspace, 'switch2/pipeline.json'), 'utf8').then(JSON.parse).catch(() => null);
    if (pipeline?.error && !admission) state.detail = pipeline.error;
    const acquisition = await fs.readFile(path.join(workspace, 'switch2/acquisition-supervisor.json'), 'utf8').then(JSON.parse).catch(() => null);
    if (acquisition?.session === path.basename(directory) && acquisition.resourcesRetained === true) {
      state.resourcesRetained = true;
      state.automaticRetry = false;
      state.failureReport = acquisition.attempts?.at(-1)?.report;
    }
    state.failureStage = pipeline?.stages?.find(row => row.status === 'failed')?.name;
    if (admissionHost && engine) {await admissionHost.admission.retainAuthority(); state.resourcesRetained = true;}
    await save();
    if (engine && !admission && !state.resourcesRetained) await engine.release().catch(() => {});
    const failure = Error('FS RPC initialization failed: ' + (admission ? 'admitted runtime unverified; retained without release' : state.detail?.split('\n')[0] || error.message));
    if (state.resourcesRetained) failure.resourcesRetained = true;
    if (state.failureReport) failure.report = state.failureReport;
    throw failure;
  }
}
module.exports = {start};
