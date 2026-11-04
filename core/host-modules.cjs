const path = require('node:path'),
  { fork } = require('node:child_process'),
  active = new Map();
async function start(root, id, { session } = {}) {
  if (active.has(id)) return { id, state: active.get(id).state };
  await require('./modules.cjs').read(root, id);
  const child = fork(path.join(__dirname, 'module-worker.cjs'), [], {
      cwd: root,
      env: { ...process.env, IZ2S_ROOT:root, IZ15_ROOT: root, IZ2S_ATTEMPT_ID:session?.id||'' },
      stdio: ['ignore', 'ignore', 'pipe', 'ipc'],
    }),
    row = { id, child, state: 'starting', errors: '' };
  active.set(id, row);
  child.stderr.on('data', (b) => (row.errors = (row.errors + b).slice(-2000)));
  child.once('exit', () => {
    if (active.get(id) === row) active.delete(id);
  });
  try {
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(Error('Module startup timed out')), 15000);
      const fail = (e) => {
        clearTimeout(timer);
        reject(e);
      };
      child.once('error', fail);
      child.once('exit', () => fail(Error(row.errors || 'Module exited before readiness')));
      child.on('message', (m) => {
        if (m.state === 'running') {
          clearTimeout(timer);
          row.state = 'running';
          resolve();
        } else if (m.state === 'failed') fail(Error(m.error));
      });
      child.send({ action: 'start', root, id, session });
    });
    return { id, state: row.state };
  } catch (e) {
    child.kill('SIGKILL');
    active.delete(id);
    throw e;
  }
}
async function stop(id) {
  const row = active.get(id);
  if (!row) return { id, state: 'disposed' };
  if (row.stopTask) return row.stopTask;
  row.stopTask = (async () => {
  await new Promise((resolve) => {
    const timer = setTimeout(() => {
      row.child.kill('SIGKILL');
      resolve();
    }, 5000);
    row.child.once('exit', () => {
      clearTimeout(timer);
      resolve();
    });
    if (row.child.connected) row.child.send({ action: 'stop' });
    else {
      clearTimeout(timer);
      row.child.kill('SIGKILL');
      resolve();
    }
  });
  active.delete(id);
  return { id, state: 'disposed' };
  })();
  return row.stopTask;
}
async function close() {
  await Promise.all([...active.keys()].map(stop));
}
function hardStop() {
  for (const row of active.values()) row.child.kill('SIGKILL');
  active.clear();
}
module.exports = {
  start,
  stop,
  close,
  hardStop,
  status: () => [...active].map(([id, h]) => ({ id, state: h.state })),
};
