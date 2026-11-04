const fs = require('node:fs/promises'),
  sync = require('node:fs'),
  path = require('node:path'),
  crypto = require('node:crypto'),
  Module = require('node:module');
const { scan, assert, ID } = require('./descriptors.cjs');
function catalog(root) {
  return {
    engines: scan(root, 'engine'),
    shells: scan(root, 'shell'),
    profiles: scan(root, 'profile'),
  };
}
// Capture source before activation. Relative JS imports remain pinned to these bytes
// even if the source folder changes while the session is running.
async function pin(root, kind, id) {
  assert(ID.test(id), 'Invalid extension id');
  const inventory = scan(root, kind).items;
  const found = inventory.find((x) => x.id === id);
  assert(found, 'Extension not available: ' + id);
  // Engine facades capture the canonical runtime in the same immutable snapshot.
  const members = [], visiting = new Set();
  async function capture(member) {
    assert(!visiting.has(member.id), 'Shared runtime engine cycle');
    if (members.some(item => item.id === member.id)) return;
    visiting.add(member.id);
    if (kind === 'engine' && member.runtimeEngine) {
      const runtime = inventory.find(item => item.id === member.runtimeEngine);
      assert(runtime?.supported && runtime.installed !== false,
        'Shared runtime engine unavailable: ' + member.runtimeEngine);
      await capture(runtime);
    }
    await walk(member.folder);
    members.push(member);
    visiting.delete(member.id);
  }
  const sources = new Map(),
    logicalNames = new Map(),
    modes = new Map();
  async function walk(dir) {
    for (const e of await fs.readdir(dir, { withFileTypes: true })) {
      if (
        e.name.startsWith('.') ||
        ['runs', 'data', 'node_modules'].includes(e.name)
      )
        continue;
      const f = path.join(dir, e.name);
      let type = e;
      if (e.isSymbolicLink()) {
        require('./source-layout.cjs').compatibilityTarget(root, f);
        type = await fs.stat(f);
      }
      if (type.isDirectory()) await walk(f);
      else if (type.isFile()) {
        sources.set(f, await fs.readFile(f));
        logicalNames.set(await fs.realpath(f), f);
        modes.set(f, (await fs.stat(f)).mode & 0o777);
      }
    }
  }
  await capture(found);
  const hash = crypto.createHash('sha256');
  for (const [name, b] of [...sources].sort())
    hash.update(path.relative(found.folder, name)).update(b);
  const fingerprint = hash.digest('hex'),
    cache = new Map();
  function load(file) {
    file = require.resolve(file);
    file = logicalNames.get(file) || file;
    if (!sources.has(file)) return require(file);
    if (cache.has(file)) return cache.get(file).exports;
    if (file.endsWith('.json')) return JSON.parse(sources.get(file));
    const m = new Module(file, module);
    cache.set(file, m);
    m.filename = file;
    m.paths = Module._nodeModulePaths(path.dirname(file));
    m.require = (name) =>
      load(require.resolve(name, { paths: [path.dirname(file)] }));
    m._compile(sources.get(file).toString('utf8'), file);
    return m.exports;
  }
  async function materializeMember(member, destination) {
    await fs.mkdir(destination, { recursive: true });
    for (const [name, bytes] of sources) {
      if (!name.startsWith(member.folder + path.sep)) continue;
      const out = path.join(destination, path.relative(member.folder, name));
      await fs.mkdir(path.dirname(out), { recursive: true });
      try {
        await fs.writeFile(out, bytes, {flag: 'wx', mode: modes.get(name) || 0o600});
      } catch (e) {
        if (e.code !== 'EEXIST') throw e;
        assert((await fs.readFile(out)).equals(bytes), 'Pinned extension cache was modified');
      }
    }
    return destination;
  }
  return {
    descriptor: found,
    members,
    fingerprint,
    load: () => load(path.join(found.folder, found.entry)),
    bytes: (relative) => sources.get(path.join(found.folder, relative)),
    async materializeEngineTree(directory) {
      assert(kind === 'engine', 'Expected an engine snapshot');
      for (const member of members)
        await materializeMember(member, path.join(directory, member.directory || member.id));
      return path.join(directory, found.directory || found.id);
    },
    async materialize(destination) {
      return materializeMember(found, destination);
    },
  };
}
function watch(root, onChange, { interval = 1500 } = {}) {
  let stamp = '';
  const timer = setInterval(() => {
    try {
      const value = catalog(root),
        next = JSON.stringify(value);
      if (next !== stamp) {
        stamp = next;
        onChange(value);
      }
    } catch (e) {
      onChange({ error: e.message });
    }
  }, interval);
  timer.unref();
  return { close: () => clearInterval(timer) };
}
module.exports = { catalog, pin, watch };
