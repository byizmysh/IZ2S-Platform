const fs = require('node:fs/promises'),
  path = require('node:path'),
  { execFile } = require('node:child_process'),
  { promisify } = require('node:util');
const ROOT = require('./root.cjs'),
  archive = require('./archive.cjs'),
  modulePaths = require('./module-paths.cjs'),
  { ID, local, assert } = require('./descriptors.cjs');
const inflight = new Map(),
  packageCache = new Map();
async function packageSnapshot(source) {
  const stat = await fs.stat(source);
  assert(
    stat.isFile() && stat.size <= archive.MAX_ARCHIVE,
    'Invalid package file size',
  );
  const stamp = [stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
  let cached = packageCache.get(source);
  if (!cached || cached.stamp !== stamp) {
    const bytes = await fs.readFile(source);
    cached = { stamp, bytes, pkg: archive.inspect(bytes) };
    packageCache.set(source, cached);
    if (packageCache.size > 64)
      packageCache.delete(packageCache.keys().next().value);
  }
  return cached;
}
async function read(root, id) {
  assert(ID.test(id), 'Invalid module id');
  return readLocated(root, id, modulePaths.home(root, id));
}
// A discovered location belongs to this inventory snapshot. Resolving it again
// for every component would rescan all installed descriptors on the stream loop.
async function readLocated(root, id, home) {
  const pinned = require('./module-pins.cjs').read(root, id, home);
  if (pinned) return describe(root, id, pinned, home);
  const source = path.join(home, id + '.izm'),
    snapshot = await packageSnapshot(source),
    pkg = snapshot.pkg;
  assert(pkg.descriptor.id === id, 'Module id differs from folder');
  const folder = path.join(home, '.versions', pkg.sha256),
    key = folder;
  if (!inflight.has(key))
    inflight.set(
      key,
      (async () => {
        try {
          await fs.access(path.join(folder, 'checksums.json'));
        } catch {
          const stage = folder + '.tmp-' + require('node:crypto').randomUUID();
          await archive.unpack(snapshot.bytes, stage);
          await fs.mkdir(path.dirname(folder), { recursive: true });
          try {
            await fs.rename(stage, folder);
          } catch (e) {
            await fs.rm(stage, { recursive: true, force: true });
            if (e.code !== 'EEXIST' && e.code !== 'ENOTEMPTY') throw e;
          }
        }
      })().catch((e) => {
        inflight.delete(key);
        throw e;
      }),
    );
  await inflight.get(key);
  require('./module-pins.cjs').remember(root, id, pkg);
  return describe(root, id, pkg, home);
}
function describe(root, id, pkg, home) {
  const source = path.join(home, id + '.izm'), folder = path.join(home, '.versions', pkg.sha256);
  const d = pkg.descriptor;
  return {
    id,
    folder,
    source,
    descriptor: d,
    packagePath: source,
    switchRoot: d.switch ? path.join(folder, 'switch') : null,
    serverEntry: d.server ? local(folder, d.server.entry) : null,
    localPackages: (d.server?.localPackages || []).map((p) => ({
      ...p,
      root: local(folder, p.path),
      entry: local(folder, p.entry),
      installer: local(folder, p.installer),
      moduleId: id,
    })),
    ...pkg,
  };
}
async function capture(root = ROOT) {
  const inventory = await list(root);
  return { version: 1, root: path.resolve(root), modules: Object.fromEntries(inventory.modules.map(m => [m.id, { sha256: m.sha256, packageBytes: m.packageBytes }])) };
}
async function list(root = ROOT) {
  const modules = [],
    errors = [];
  const inventory = modulePaths.discover(root);
  errors.push(...inventory.errors);
  for (const item of inventory.items)
    try { modules.push(await readLocated(root, item.id, item.home)); }
    catch (error) { errors.push({ module: item.id, error: error.message }); }
  return { modules, errors };
}
async function loadServer(root, id) {
  const m = await read(root, id);
  assert(m.serverEntry, 'Module has no server component');
  assert(
    m.descriptor.platforms.includes(process.platform + '-' + process.arch),
    'Module platform unsupported',
  );
  return require(m.serverEntry);
}
// Check the selected modules and their module dependencies before touching a device.
// This reads metadata and bytes only; it does not import extension code.
async function verify(root, ids, seen = new Set()) {
  for (const id of ids) {
    if (seen.has(id)) continue;
    seen.add(id);
    const m = await read(root, id);
    for(const d of m.descriptor.dependencies||[])if(d.kind==='module'){
      const dependency=await read(root,d.id),reason=require('./compatibility.cjs').dependencyReason(d,dependency);assert(!reason,reason);
    }
    for (const file of m.files) {
      const target = local(m.folder, file.name), stat = await fs.lstat(target);
      assert(stat.isFile() && !stat.isSymbolicLink(), 'Invalid installed module file: ' + id + '/' + file.name);
      assert(archive.sha(await fs.readFile(target)) === archive.sha(file.bytes),
        'Installed module file differs: ' + id + '/' + file.name);
    }
    await verify(root, (m.descriptor.dependencies || []).filter(d => d.kind === 'module').map(d => d.id), seen);
  }
  return [...seen];
}
async function serverEntries(root = ROOT) {
  return (await list(root)).modules
    .filter((m) => m.serverEntry)
    .map((m) => ({
      id: m.id,
      entry: m.serverEntry,
      implementations: m.descriptor.server.implementations || {},
      localPackages: m.localPackages,
    }));
}
async function localPackage(root, id) {
  const p = (await list(root)).modules
    .flatMap((m) => m.localPackages)
    .find((m) => m.id === id);
  assert(p, 'Unknown local module: ' + id);
  return p;
}
async function status(root = ROOT) {
  const result = {};
  for (const m of (await list(root)).modules)
    for (const p of m.localPackages) {
      const record = await fs
        .readFile(
          path.join(modulePaths.home(root, m.id), 'runtime', 'installed.json'),
          'utf8',
        )
        .then(JSON.parse, () => null);
      const missingFiles=[];
      const home=modulePaths.home(root,m.id),requirements=p.requiredFiles?.[process.platform+'-'+process.arch]||[];
      for(const file of requirements){
        try{const target=local(home,file),resolved=await fs.realpath(target);assert(resolved.startsWith(path.resolve(home)+path.sep),'Runtime file leaves module');assert((await fs.stat(resolved)).isFile(),'Runtime file missing');}
        catch{missingFiles.push(file);}
      }
      result[p.id] = {
        installed: !!record&&!missingFiles.length,
        version: record?.version || null,
        platform: process.platform,
        supported: m.descriptor.platforms.includes(
          process.platform + '-' + process.arch,
        ),
        ...(missingFiles.length?{missingFiles}:{}),
      };
    }
  return result;
}
async function install(root = ROOT, id, {source}={}) {
  const p = await localPackage(root, id);
  if(source!==undefined){assert(p.importSource===true&&typeof source==='string'&&path.isAbsolute(source),'This installer needs an absolute local source folder');assert((await fs.stat(source)).isDirectory(),'Local source must be a folder');}
  if(p.importSource)assert(source,'Choose the local runtime source folder first');
  await promisify(execFile)(process.execPath, [p.installer,...(p.importSource?['--source',source]:[])], {
    cwd: root,
    env: {
      ...process.env,
      IZ2S_ROOT: root,
      IZ15_ROOT: root,
      IZ2S_MODULE_HOME: modulePaths.home(root, p.moduleId),
      IZ15_MODULE_HOME: modulePaths.home(root, p.moduleId),
      IZ15_MODULE_ID: p.moduleId,
    },
    timeout: 240000,
    maxBuffer: 65536,
  });
  return status(root);
}
async function activate(root, id, context = {}) {
  await verify(root,[id]);
  const m = await read(root, id);
  assert(
    m.descriptor.platforms.includes(process.platform + '-' + process.arch),
    'Module platform unsupported',
  );
  const entry = m.serverEntry ? require(m.serverEntry) : null;
  if (entry)
    assert(
      typeof entry.createModule === 'function',
      'Server must export createModule',
    );
  const adapter = entry?.createModule
    ? await entry.createModule({
        ...context,
        root,
        module: m,
        services: require('../sdk/platform.cjs').forModule(root,id),
        dataDirectory: path.join(modulePaths.home(root, id), 'data'),
        sdStorage: require('./module-storage.cjs').forModule(id),
      })
    : null;
  if (adapter)
    for (const method of ['init', 'start', 'stop', 'dispose'])
      assert(
        typeof adapter[method] === 'function',
        'Missing lifecycle method: ' + method,
      );
  let state = 'created';
  return {
    module: m,
    get state() {
      return state;
    },
    async start() {
      assert(state === 'created', 'Module already started');
      try {
        await adapter?.init();
        await adapter?.start();
        state = 'running';
      } catch (e) {
        try {
          await adapter?.stop();
        } finally {
          await adapter?.dispose();
          state = 'disposed';
        }
        throw e;
      }
    },
    async stop() {
      if (state === 'disposed') return;
      try {
        await adapter?.stop();
      } finally {
        await adapter?.dispose();
        state = 'disposed';
      }
    },
  };
}
module.exports = {
  ROOT,
  read,
  list,
  loadServer,
  serverEntries,
  local: localPackage,
  status,
  install,
  activate,
  verify,
  capture,
};
