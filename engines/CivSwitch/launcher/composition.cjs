// Contributions are declared by modules; no application ID is special here.
const fs = require('node:fs/promises'),
  path = require('node:path'),
  crypto = require('node:crypto'),
  catalog = require('../../../core/services/app-catalog.cjs');
const SHELL = path.resolve(__dirname, 'app');
async function build(apps, cacheRoot) {
  const assets = new Map(),
    defines = new Set();
  let memory = 0;
  for (const entry of apps) {
    const app = await catalog.readApp(entry.root),
      c = app.manifest.shellContribution;
    if (!c) continue;
    if (
      !Array.isArray(c.files) ||
      !Array.isArray(c.defines) ||
      c.defines.some((d) => !/^IZ_[A-Z0-9_]+$/.test(d))
    )
      throw Error('Invalid shell contribution');
    for (const d of c.defines) defines.add(d);
    memory = Math.max(memory, c.memoryBytes || 0);
    if (memory > 32 * 1024 * 1024 || memory % 4096) throw Error('Invalid contribution memory');
    for (const name of c.files) {
      const item = await catalog.asset(app.root, name, 256 * 1024),
        base = path.basename(name);
      if (assets.has(base) && !assets.get(base).equals(item.bytes))
        throw Error('Conflicting shell contribution: ' + base);
      assets.set(base, item.bytes);
    }
  }
  if (!defines.size) return catalog.readApp(SHELL);
  const hash = crypto.createHash('sha256');
  for (const [name, b] of [...assets].sort()) hash.update(name).update(b);
  hash.update([...defines].sort().join('\n'));
  hash.update(String(memory));
  for (const name of (await fs.readdir(path.join(SHELL, 'shaders'))).sort())
    hash.update(await fs.readFile(path.join(SHELL, 'shaders', name)));
  const id = 'iz-shell-' + hash.digest('hex').slice(0, 16),
    folder = path.join(cacheRoot, id);
  if (
    await fs.access(path.join(folder, 'ready.json')).then(
      () => true,
      () => false,
    )
  )
    return catalog.readApp(folder);
  await fs.mkdir(folder, { recursive: true });
  await fs.cp(path.join(SHELL, 'shaders'), path.join(folder, 'shaders'), { recursive: true });
  await fs.cp(path.join(SHELL, 'assets'), path.join(folder, 'assets'), { recursive: true });
  for (const [name, b] of assets) await fs.writeFile(path.join(folder, 'shaders', name), b);
  await fs.writeFile(
    path.join(folder, 'shaders/features.glsl'),
    [...defines]
      .sort()
      .map((d) => '#define ' + d + ' 1')
      .join('\n') + '\n',
  );
  const manifest = JSON.parse(await fs.readFile(path.join(SHELL, 'app.json')));
  manifest.id = id;
  manifest.name = 'IZlauncher modules';
  if (memory) {
    manifest.memory = { bytes: memory, initial: 'video-empty.bin' };
    await fs.writeFile(path.join(folder, 'video-empty.bin'), Buffer.alloc(4));
  }
  await fs.writeFile(path.join(folder, 'app.json'), JSON.stringify(manifest));
  await require('../../../core/packages/build-focus.cjs').build(folder);
  const app = await catalog.readApp(folder);
  await fs.writeFile(
    path.join(folder, 'ready.json'),
    JSON.stringify({ id, defines: [...defines], memory }),
  );
  return app;
}
module.exports = { build, SHELL };
