// Start the server, then attach the separately owned console shell.
const { spawn } = require('node:child_process');
const path = require('node:path');
const root = path.resolve(__dirname, '../..');
const children = new Set();
let stopping, offlineSession = false;
const applicationRecovery = require('./app-recovery/lifetime.cjs').create();
const hostScript = 'engines/host.cjs';
// Latch before any child spawn; later environment changes cannot remove safety.
const nativeParentRetention = process.env.IZ2S_NATIVE_PARENT_RETENTION === '1';
const nativeParentStop = require('./native-parent-stop.cjs').create({
  enabled: nativeParentRetention,
  onStatus: status => console.log('[native-parent-stop] ' + JSON.stringify(status)),
});
function launchStage(script, args = [], readyText) {
  if (nativeParentRetention && stopping) throw Error('Session stopping; refusing a new child');
  const child = spawn(process.execPath, [script, ...args], {
    cwd: root,
    stdio: ['ignore', 'pipe', 'inherit'],
  });
  child.script = script;
  children.add(child);
  if (nativeParentRetention) nativeParentStop.track(child, { script });
  console.log('[process] Starting ' + script);
  child.once('spawn', () => console.log('[process] Started ' + script + ' (pid ' + child.pid + ')'));
  let output = '',
    readyResolve,
    readyReject;
  const ready = new Promise((resolve, reject) => {
    readyResolve = resolve;
    readyReject = reject;
  });
  // Some stages only need completion; keep unused readiness rejections handled.
  ready.catch(() => {});
  child.stdout.on('data', (bytes) => {
    process.stdout.write(bytes);
    output = (output + bytes).slice(-32768);
    if (readyText && output.includes(readyText)) readyResolve(output);
  });
  const done = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('close', (code, signal) => {
      console.log('[process] Closed ' + script + ' (exit code: ' + (code ?? 'none') + ', signal: ' + (signal || 'none') + ')');
      children.delete(child);
      readyReject(Error(script + ' exited before ready'));
      if (code === 0) resolve(output);
      else reject(Error(script + ' exited: ' + (signal || code)));
    });
  });
  done.catch(() => {});
  return { child, ready, done };
}
function closeSession() {
  if (stopping) return stopping;
  // In protected mode publish the cached promise before invoking cleanup; a
  // reentrant STOP sees this same operation and cannot signal a child twice.
  const shutdown = async () => {
    if (nativeParentRetention) nativeParentStop.begin();
    await applicationRecovery.close().catch(error =>
      console.error('Application status shutdown: ' + error.message));
    async function end(child) {
      if (!children.has(child)) return;
      if (nativeParentRetention) return nativeParentStop.stopChild(child);
      await new Promise((resolve) => {
        const timer = setTimeout(() => {
          if (children.has(child)) child.kill('SIGKILL');
          resolve();
        }, offlineSession ? 1000 : 5000);
        child.once('close', () => {
          clearTimeout(timer);
          resolve();
        });
        child.kill('SIGTERM');
      });
    }
    // Close file-transfer handles and commit writes while RPC is still available.
    await Promise.all(
      [...children].filter((c) => c.script === 'core/console/host/ftp-service.cjs').map(end),
    );
    // Keep RPC alive until the audio owner has stopped its console output.
    await Promise.all([...children].filter((c) => /\/game-audio\.cjs$/.test(c.script)).map(end));
    await Promise.all([...children].filter((c) => c.script !== hostScript).map(end));
    await Promise.all([...children].map(end));
    if (nativeParentRetention) {
      await nativeParentStop.waitAllClosed();
      nativeParentStop.finish();
    }
  };
  stopping = nativeParentRetention ? Promise.resolve().then(shutdown) : shutdown();
  return stopping;
}
// Auxiliary services never own the native connection's lifetime.
function watchAuxiliary(task, name, onExit, isStopping = () => !!stopping) {
  let reported = false;
  const report = (error) => {
    if (reported || isStopping()) return;
    reported = true;
    try {
      return Promise.resolve(onExit(name, error)).catch((e) =>
        console.error('Service report: ' + e.message),
      );
    } catch (e) {
      console.error('Service report: ' + e.message);
    }
  };
  task.done
    .then(() => report(Error('Service exited')), report)
    .catch((error) => console.error('Service report: ' + error.message));
  return task.ready.catch((error) => report(error));
}
async function runQualifiedSession() {
  const id = process.env.IZ2S_SHELL || process.env.IZ15_SHELL || 'izlauncher';
  require('./runtime-check.cjs').verify(root, id);
  const pinned = await require('../extensions.cjs').pin(root, 'shell', id),
    factory = pinned.load();
  if (
    typeof factory.compatible === 'function' &&
    !(await factory.compatible(
      require('../../engines/index.cjs')
        .list()
        .find((e) => e.id === (process.env.IZ2S_LOADER || process.env.IZ15_LOADER || 'izmysh')),
    ))
  )
    throw Error('Shell incompatible with the selected engine');
  const host = launchStage(
    hostScript,
    process.argv.slice(2),
    'Qualified native session ready for graphics inspection.',
  );
  try {
    const output = await host.ready;
    const match = output.match(/Run folder: ([^\r\n]+)/);
    if (!match) throw Error('Missing fresh run directory');
    const directory = match[1];
    if (process.env.IZ18_RESIDENT_GRAPHICS === '1') {
      try {
        const started = await applicationRecovery.start({
          directory,
          host: process.env.HB_STREAM_HOST || '127.0.0.1',
          onEvent: event => {
            if (event.state !== 'status')
              console.log('Application status link: ' + event.state);
          },
        });
        if (!started) return;
        console.log('Application status listener ready; independent console client attachment is still required.');
      } catch (error) {
        console.error('Application status listener unavailable: ' + error.message);
      }
    }
    if (stopping) return;
    const connection = JSON.parse(
      await require('node:fs/promises').readFile(path.join(directory, 'connection.json')),
    );
    const instance = await factory.create({
      root,
      session: { directory, connection },
      watchAuxiliary,
      closeSession,
      launchStage,
    });
    for (const method of ['init', 'start', 'stop', 'dispose'])
      if (typeof instance[method] !== 'function') throw Error('Missing shell lifecycle: ' + method);
    try {
      await instance.init();
      await instance.start();
      if (!instance.done || typeof instance.done.then !== 'function')
        throw Error('Shell must expose its completion Promise');
      console.log('IZ launcher runtime is running.');
      await require('./offline-policy.cjs').awaitCompletion({
        hostDone: host.done, shellDone: instance.done, directory,
        isStopping: () => !!stopping,
        onOffline: () => {
          offlineSession = true;
          console.log('IZ launcher offline: resources retained; waiting for STOP.');
        },
      });
    } finally {
      try {
        await instance.stop();
      } finally {
        await instance.dispose();
      }
    }
  } finally {
    await closeSession();
  }
}
if (require.main === module) {
  process.on('SIGINT', closeSession);
  process.on('SIGTERM', closeSession);
  runQualifiedSession().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
module.exports = {watchAuxiliary, runQualifiedSession, closeSession, getShutdownStatus: () => nativeParentStop.snapshot()};
