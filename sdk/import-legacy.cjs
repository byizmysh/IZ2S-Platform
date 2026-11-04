const fs = require('node:fs/promises'),
  path = require('node:path'),
  capsule = require('../core/packages/capsule.cjs'),
  archive = require('../core/archive.cjs'),
  ROOT = require('../core/root.cjs');
async function convert(file, keyFile, destination) {
  const secret = await fs.readFile(keyFile);
  if (secret.length !== 32) throw Error('Expected the original 32-byte local key');
  const legacy = capsule.decode(await fs.readFile(file), secret),
    id = legacy.metadata.id,
    manifest = JSON.parse(legacy.files.find((f) => f.name === 'app.json').bytes),
    files = legacy.files.map((f) => ({ name: 'switch/' + f.name, bytes: Buffer.from(f.bytes) }));
  let base;
  if (manifest.builtin && !manifest.shellContribution) {
    try {
      base = JSON.parse(await fs.readFile(path.join(require('../core/module-paths.cjs').home(ROOT,id), 'module.json')));
      const modern = JSON.parse(
        await fs.readFile(path.join(require('../core/module-paths.cjs').home(ROOT,id), 'switch/app.json')),
      );
      if (modern.builtin !== manifest.builtin || !modern.shellContribution)
        throw Error('Unknown behavior');
      manifest.shellContribution = modern.shellContribution;
      delete manifest.run11Modules;
      files.find((f) => f.name === 'switch/app.json').bytes = Buffer.from(JSON.stringify(manifest));
      async function walk(dir, prefix) {
        for (const e of await fs.readdir(dir, { withFileTypes: true })) {
          if (e.isSymbolicLink()) throw Error('Module source link');
          const name = prefix + '/' + e.name;
          if (e.isDirectory()) await walk(path.join(dir, e.name), name);
          else files.push({ name, bytes: await fs.readFile(path.join(dir, e.name)) });
        }
      }
      if (base.server) await walk(path.join(require('../core/module-paths.cjs').home(ROOT,id), 'server'), 'server');
    } catch (e) {
      throw Error('No bundled host adapter for legacy built-in ' + id + ': ' + e.message);
    }
  }
  const descriptor = {
    ...(base || {}),
    format: 'izlauncher.module',
    formatVersion: 1,
    apiVersion: 1,
    id,
    name: legacy.metadata.name,
    version: legacy.metadata.appVersion || '1.0.0',
    author: base?.author || 'Legacy package author (not independently verified)',
    platforms: base?.platforms || ['linux-x64', 'linux-arm64'],
    requires: { platformApi: 1, shell: 'izlauncher', consoleAbi: 1 },
    switch: {
      entry: 'switch/app.json',
      files: files.filter((f) => f.name.startsWith('switch/')).map((f) => f.name),
      abi: 1,
    },
    legacy: { packageNames: [path.basename(file)] },
    license: 'LicenseRef-Legacy-Unknown',
    migration: {
      sourceFormat: 'iz-capsule/1',
      hostAdapter: base ? 'IZlauncher bundled adapter' : 'none',
    },
  };
  files.push(
    { name: 'module.json', bytes: Buffer.from(JSON.stringify(descriptor, null, 2) + '\n') },
    {
      name: 'README.md',
      bytes: Buffer.from(
        'Imported locally from ' +
          path.basename(file) +
          '. ' +
          (base
            ? 'Host adapter supplied by IZlauncher.'
            : 'Console-only capsule; no host implementation inferred.') +
          '\n',
      ),
    },
    {
      name: 'LICENSE',
      bytes: Buffer.from(
        'Original author license applies. No new permission is inferred by this importer.\n',
      ),
    },
  );
  const bytes = archive.encode([
    ...files,
    {
      name: 'checksums.json',
      bytes: Buffer.from(
        JSON.stringify({
          algorithm: 'sha256',
          files: Object.fromEntries(files.map((f) => [f.name, archive.sha(f.bytes)])),
        }),
      ),
    },
  ]);
  archive.inspect(bytes);
  await fs.mkdir(path.dirname(destination), { recursive: true });
  await fs.writeFile(destination, bytes, { flag: 'wx' });
  return { id, bytes: bytes.length };
}
if (require.main === module) {
  const args = process.argv.slice(2),
    key = args.indexOf('--key');
  if (key < 0 || !args[key + 1] || !args[0] || !args[1]) {
    console.error(
      'Usage: node sdk/import-legacy.cjs old.izs output.izm --key /path/to/original/capsule.key',
    );
    process.exitCode = 1;
  } else
    convert(args[0], args[key + 1], path.resolve(args[1]))
      .then((x) => console.log(JSON.stringify(x)))
      .catch((e) => {
        console.error(e.message);
        process.exitCode = 1;
      });
}
module.exports = { convert };
