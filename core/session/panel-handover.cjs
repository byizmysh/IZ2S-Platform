const fs = require('node:fs/promises');
const path = require('node:path');

async function processIdentity(pid) {
  try {
    const stat = await fs.readFile('/proc/' + pid + '/stat', 'utf8');
    const fields = stat.slice(stat.lastIndexOf(')') + 2).trim().split(/\s+/);
    return {start: fields[19], state: fields[0], parent: Number(fields[1])};
  } catch (error) {
    if (error.code === 'ENOENT' || error.code === 'ESRCH') return null;
    throw error;
  }
}
async function processAlive(pid) {
  if (process.platform === 'linux') {
    const identity = await processIdentity(pid);
    return !!identity && !['Z', 'X'].includes(identity.state);
  }
  try { process.kill(pid, 0); return true; }
  catch (error) { if (error.code === 'ESRCH') return false; throw error; }
}

async function inspectOwner(pid) {
  if (!Number.isSafeInteger(pid) || pid <= 1 || process.platform !== 'linux') return null;
  try {
    const root = await fs.realpath('/proc/' + pid + '/cwd');
    const args = (await fs.readFile('/proc/' + pid + '/cmdline', 'utf8')).split('\0');
    if (!args.some(arg => arg && path.resolve(root, arg) === path.join(root, 'start.cjs'))) return null;
    const manifest = JSON.parse(await fs.readFile(path.join(root, 'package.json'), 'utf8'));
    const state = JSON.parse(await fs.readFile(path.join(root, 'data/state/window.json'), 'utf8'));
    if (manifest.name !== 'iz2s-platform' || state.pid !== pid || state.preview) return null;
    const identity = await processIdentity(pid);
    if (!identity || ['Z', 'X'].includes(identity.state)) return null;
    return { root, version: state.version, start: identity.start };
  } catch { return null; }
}

async function ownedChildren(pid) {
  if (process.platform !== 'linux') return [];
  const children = [], seen = new Set([pid]);
  async function visit(parent) {
    let ids;
    try { ids = (await fs.readFile('/proc/' + parent + '/task/' + parent + '/children', 'utf8')).trim().split(/\s+/).filter(Boolean).map(Number); }
    catch (error) { if (error.code === 'ENOENT' || error.code === 'ESRCH') return; throw error; }
    for (const child of ids) {
      if (!Number.isSafeInteger(child) || child <= 1 || seen.has(child)) continue;
      seen.add(child);
      const identity = await processIdentity(child);
      if (!identity || identity.parent !== parent) continue;
      // A desktop browser can contain unrelated user tabs. It is never part
      // of forced console-worker cleanup, even if this panel launched it.
      const executable = await fs.realpath('/proc/' + child + '/exe').catch(() => '');
      if (/chrome|chromium|firefox|brave|vivaldi|microsoft-edge/i.test(path.basename(executable))) continue;
      await visit(child);
      children.push({pid: child, start: identity.start});
    }
  }
  await visit(pid);
  return children;
}

async function recordForcedStop(root, receipt) {
  const directory = path.join(root, 'data/state');
  await fs.mkdir(directory, {recursive: true, mode: 0o700});
  await fs.writeFile(path.join(directory, 'forced-server-stop.json'), JSON.stringify(receipt, null, 2) + '\n', {mode: 0o600});
}

async function stopPanel({pid, owner, root, inspect = inspectOwner,
  signal = (pid, name) => process.kill(pid, name), alive = processAlive,
  identity = processIdentity, children = ownedChildren, record = recordForcedStop,
  sleep = ms => new Promise(resolve => setTimeout(resolve, ms)),
  timeout = 8000, forceTimeout = 3000, force = true}) {
  const verified = await inspect(pid);
  if (!owner || !verified || !owner.start || owner.start !== verified.start ||
      path.resolve(owner.root) !== path.resolve(verified.root) || owner.version !== verified.version)
    throw Error('Panel process identity changed; automatic termination refused.');
  const descendants = await children(pid);
  const send = (target, name) => {try { signal(target, name); } catch (error) {if (error.code !== 'ESRCH') throw error;}};
  send(pid, 'SIGTERM');
  for (let elapsed = 0; await alive(pid); elapsed += 100) {
    if (elapsed >= timeout) break;
    await sleep(100);
  }
  if (!await alive(pid)) return {forced: false};
  if (!force) throw Error('The preceding panel is still stopping.');
  const current = await identity(pid);
  if (!current || current.start !== owner.start)
    throw Error('Panel process identity changed; forced termination refused.');
  const receipt = {at: new Date().toISOString(), previousRoot: owner.root, pid,
    reason: 'Graceful panel shutdown timed out', forced: true,
    consoleRestartRequired: true, nativeCleanupProven: false};
  // Preserve the restart requirement before terminating any live owner.
  await record(root, receipt);
  if (path.resolve(root) !== path.resolve(owner.root)) await record(owner.root, receipt);
  console.error('The preceding panel did not stop. Terminating its verified PC processes; restart Switch before another console session.');
  for (const child of descendants) {
    const actual = await identity(child.pid);
    if (actual?.start === child.start && !['Z', 'X'].includes(actual.state)) send(child.pid, 'SIGKILL');
  }
  // Recheck after asynchronous child cleanup so PID reuse never targets a new process.
  const actual = await identity(pid);
  if (actual?.start === owner.start) send(pid, 'SIGKILL');
  const targets = [{pid, start: owner.start}, ...descendants];
  for (let elapsed = 0;; elapsed += 100) {
    const remaining = await Promise.all(targets.map(async target => {
      const current = await identity(target.pid);
      return current?.start === target.start && !['Z', 'X'].includes(current.state) && await alive(target.pid);
    }));
    if (!remaining.some(Boolean)) break;
    if (elapsed >= forceTimeout) throw Error('The preceding panel or a console worker did not exit after forced termination.');
    await sleep(100);
  }
  return {forced: true};
}

async function findStoredOwner({root, port, inspect = inspectOwner}) {
  const candidates = new Set([path.resolve(root)]);
  for (const entry of await fs.readdir(path.dirname(root), {withFileTypes: true}))
    if (entry.isDirectory() && /^iz2s-platform(?:-run\d+)?$/.test(entry.name))
      candidates.add(path.join(path.dirname(root), entry.name));
  for (const candidate of candidates) {
    try {
      const state = JSON.parse(await fs.readFile(path.join(candidate, 'data/state/window.json'), 'utf8'));
      const url = new URL(state.url);
      if (state.preview || url.hostname !== '127.0.0.1' || Number(url.port) !== port) continue;
      const owner = await inspect(state.pid);
      if (owner && path.resolve(owner.root) === candidate && owner.version === state.version)
        return {pid: state.pid, owner};
    } catch {}
  }
  return null;
}

async function handover({ root, port, probe = async () => {
  try {
    const response = await fetch('http://127.0.0.1:' + port + '/health', { signal: AbortSignal.timeout(1500) });
    if (!response.ok) return null;
    return await response.json();
  } catch { return null; }
}, inspect = inspectOwner, find = findStoredOwner, stop = stopPanel,
restart = false, ...options }) {
  const health = await probe();
  const stored = health ? null : await find({root, port, inspect});
  if (!health && !stored) return false;
  const pid = health?.pid ?? stored.pid;
  const owner = stored?.owner ?? await inspect(pid);
  if (!owner || (health && (owner.version !== health.version || health.preview)))
    throw Error('The panel port is occupied by an unverified process. Close that application before starting IZ2S Platform.');
  if (path.resolve(owner.root) === path.resolve(root) && !restart) return false;
  console.log('Stopping the preceding IZ2S Platform panel before starting this release.');
  await stop({pid, owner, root, inspect, ...options});
  return true;
}

module.exports = { handover, inspectOwner, stopPanel, processIdentity, processAlive, ownedChildren, findStoredOwner };
