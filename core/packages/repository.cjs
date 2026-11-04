const fs = require('node:fs/promises'),
  path = require('node:path'),
  crypto = require('node:crypto');
const registry = require('../modules.cjs'),
  catalog = require('../services/app-catalog.cjs'),
  archive = require('../archive.cjs'),
  storeCatalog = require('./store-catalog.cjs');
async function create(root, options = {}) {
  root = path.resolve(root);
  const shell=options.shell||process.env.IZ2S_SHELL||process.env.IZ15_SHELL||'izlauncher';
  const shellDescriptor=require('../descriptors.cjs').scan(root,'shell').items.find(item=>item.id===shell);
  const compatibilityOptions={...options,shell,shellDescriptor};
  const folder = path.join(root, 'data/imports');
  await fs.mkdir(folder, { recursive: true });
  async function inspectFromInventory(name, inventory) {
    if (!/^[a-z0-9][a-z0-9._-]{0,95}\.(izm|izs|izp)$/.test(name))
      throw Error('Invalid package name');
    const m = inventory.modules.find(
        (m) =>
          m.id + '.izm' === name ||
          m.descriptor.legacy?.packageNames?.includes(name),
      );
    if (!m)
      throw Error(
        'Unknown module; import legacy packages with the original key first',
      );
    if (!m.descriptor.switch) throw Error('Module has no Switch component');
    const files = m.files
        .filter((f) => m.descriptor.switch.files.includes(f.name))
        .map((f) => ({ name: f.name.slice(7), bytes: f.bytes })),
      manifest = JSON.parse(files.find((f) => f.name === 'app.json')?.bytes);
    if (manifest.id !== m.id) throw Error('Switch/module identity differs');
    const compatibility = require('../compatibility.cjs').moduleCompatibility(
      m.descriptor,
      compatibilityOptions,
    );
    for (const dependency of m.descriptor.dependencies || [])
      if (dependency.kind === 'module') {
        const reason=require('../compatibility.cjs').dependencyReason(dependency,inventory.modules.find(other=>other.id===dependency.id));if(!reason)continue;
        compatibility.supported = false;
        compatibility.reasons.push(reason);
      }
    const icon = files.find((f) => f.name === manifest.icon);
    if (!icon || icon.bytes.length > 2 * 1024 * 1024)
      throw Error('Package icon unavailable');
    return {
      ...m,
      file: m.id + '.izm',
      module: m.descriptor,
      files,
      contents: {
        upstreamSourceBytes: m.files
          .filter((f) => f.name.startsWith('vacuumtube/'))
          .reduce((n, f) => n + f.bytes.length, 0),
        moduleDependencies: (m.descriptor.dependencies || [])
          .filter((d) => d.kind === 'module')
          .map((d) => d.id),
        switchSourceBytes: files.reduce((n, f) => n + f.bytes.length, 0),
        serverSourceBytes: m.files
          .filter((f) => f.name.startsWith('server/'))
          .reduce((n, f) => n + f.bytes.length, 0),
        externalDependencies: (m.descriptor.dependencies || [])
          .filter((d) => d.kind === 'external-download')
          .map((d) => d.id),
      },
      metadata: {
        id: m.id,
        name: m.descriptor.name,
        appVersion: m.descriptor.version,
        description: manifest.description,
        kind: manifest.builtin ? 'builtin' : 'gpu-app',
      },
      resident: require('../../shells/izlauncher/resident/compatibility.cjs').candidate({manifest,effects:manifest.audio?.effects||{}},m.descriptor),
      residentAudio: require('../../shells/izlauncher/resident/compatibility.cjs').candidate({manifest,effects:manifest.audio?.effects||{}},m.descriptor,{finiteAudio:true}),
      ...compatibility,
      name: m.descriptor.name,
      version: m.descriptor.version,
      description: manifest.description,
      icon:
        'data:image/' +
        (icon.bytes.toString('ascii', 0, 4) === 'RIFF' ? 'webp' : 'png') +
        ';base64,' +
        icon.bytes.toString('base64'),
    };
  }
  async function inspect(name) {
    return inspectFromInventory(name, await registry.list(root));
  }
  async function list() {
    const inventory = await registry.list(root),
      packages = [],
      errors = [...inventory.errors];
    for (const m of inventory.modules)
      if (m.descriptor.switch && storeCatalog.visible(m.descriptor))
        try {
          packages.push(await inspectFromInventory(m.id + '.izm', inventory));
        } catch (e) {
          errors.push({ file: m.id + '.izm', error: e.message });
        }
    return { packages, errors };
  }
  async function install(name, destination, {onProgress} = {}) {
    const p = await inspect(name);
    await registry.verify(root, [p.id]);
    if (!p.supported) throw Error('Module unsupported on this host: ' + p.id);
    await fs.mkdir(destination, { recursive: true });
    const stage = path.join(
      destination,
      '.' + p.id + '-' + crypto.randomUUID(),
    );
    await fs.mkdir(stage);
    try {
      const totalBytes=p.files.reduce((n,f)=>n+f.bytes.length,0);let completedBytes=0;
      if(onProgress)await onProgress({completedBytes,totalBytes});
      for (const f of p.files) {
        const file = path.join(stage, archivePath(f.name));
        await fs.mkdir(path.dirname(file), { recursive: true });
        if(onProgress){const handle=await fs.open(file,'w');try{for(let at=0;at<f.bytes.length;at+=262144){let written=0;const part=f.bytes.subarray(at,at+262144);while(written<part.length){const result=await handle.write(part,written,part.length-written);if(!result.bytesWritten)throw Error('Module file write made no progress');written+=result.bytesWritten;}completedBytes+=part.length;await onProgress({completedBytes,totalBytes});}}finally{await handle.close();}}
        else await fs.writeFile(file, f.bytes);
      }
      const app = await catalog.readApp(stage);
      if (app.id !== p.id) throw Error('App identity differs');
      const target = path.join(destination, p.id),
        backup = target + '.old-' + crypto.randomUUID();
      let exists = false;
      try {
        await fs.rename(target, backup);
        exists = true;
      } catch (e) {
        if (e.code !== 'ENOENT') throw e;
      }
      try {
        await fs.rename(stage, target);
      } catch (e) {
        if (exists) await fs.rename(backup, target);
        throw e;
      }
      if (exists) await fs.rm(backup, { recursive: true, force: true });
      return {
        id: p.id,
        name: p.name,
        bytes: p.packageBytes,
        fingerprint: app.fingerprint,
      };
    } finally {
      await fs.rm(stage, { recursive: true, force: true });
    }
  }
  async function previewApps(cacheRoot, {filter = () => true} = {}) {
    const result = await list(),
      apps = [], packages = [], rejected = [...result.errors];
    for (const p of result.packages.filter((p) => p.supported && filter(p)).slice(0, 16)) {
      try {
        await install(p.file, cacheRoot);
        apps.push(await catalog.readApp(path.join(cacheRoot, p.id)));
        packages.push(p);
      } catch (error) {
        rejected.push({ file: p.file, error: error.message });
      }
    }
    return {
      root: cacheRoot,
      apps,
      rejected,
      packages,
    };
  }
  return { folder, inspect, list, install, previewApps };
}
const archivePath = require('../descriptors.cjs').relative;
function publicPackage(p) {
  return {
    id: p.id,
    file: p.file,
    name: p.name,
    version: p.version,
    description: p.description,
    bytes: p.packageBytes,
    expandedBytes: p.expandedBytes,
    contents: p.contents,
    kind: p.metadata.kind,
    icon: p.icon,
    module: p.module,
    supported: p.supported,
    resident: p.resident,
    residentAudio: p.residentAudio,
    unsupportedReasons: p.reasons,
  };
}
module.exports = { create, publicPackage };
