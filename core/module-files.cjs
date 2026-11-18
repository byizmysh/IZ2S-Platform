const modulePaths = require('./module-paths.cjs');
// Synchronous bridge for legacy CommonJS imports. Resolve verified module contents,
// never a development checkout. New asynchronous callers use modules.loadServer.
const fs = require('node:fs'), path = require('node:path'), crypto = require('node:crypto');
const archive = require('./archive.cjs'), { ID, relative, assert } = require('./descriptors.cjs');
const cache = new Map();
function resolve(id, file, root = require('./root.cjs')) {
  assert(ID.test(id), 'Invalid module id'); relative(file);
  const pinned = require('./module-pins.cjs').read(root, id);
  if (pinned) {
    assert(pinned.files.some(entry => entry.name === file), 'Unknown pinned module file: ' + id + '/' + file);
    return path.join(modulePaths.home(root, id), '.versions', pinned.sha256, file);
  }
  const source = path.join(modulePaths.home(root, id), id + '.module.zip'), stat = fs.statSync(source);
  const stamp = [stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
  let row = cache.get(source);
  if (!row || row.stamp !== stamp) {
    const pkg = archive.inspect(fs.readFileSync(source));
    assert(pkg.descriptor.id === id, 'Module id differs from folder');
    row = { stamp, pkg, folder: path.join(modulePaths.home(root, id), '.versions', pkg.sha256) };
    if (!fs.existsSync(path.join(row.folder, 'checksums.json'))) {
      const stage = row.folder + '.tmp-' + crypto.randomUUID();
      try {
        fs.mkdirSync(stage, { recursive: true });
        for (const entry of pkg.files) {
          const target = path.join(stage, entry.name);
          fs.mkdirSync(path.dirname(target), { recursive: true });
          fs.writeFileSync(target, entry.bytes, { flag: 'wx', mode: 0o600 });
        }
        try { fs.renameSync(stage, row.folder); }
        catch (error) { if (!['EEXIST', 'ENOTEMPTY'].includes(error.code)) throw error; }
      } finally { fs.rmSync(stage, { recursive: true, force: true }); }
    }
    cache.set(source, row);
    require('./module-pins.cjs').remember(root, id, pkg);
    if (cache.size > 64) cache.delete(cache.keys().next().value);
  }
  const expected = row.pkg.files.find(entry => entry.name === file);
  assert(expected, 'Unknown packaged module file: ' + id + '/' + file);
  const target = path.join(row.folder, file), statFile = fs.lstatSync(target);
  assert(statFile.isFile() && !statFile.isSymbolicLink() && archive.sha(fs.readFileSync(target)) === archive.sha(expected.bytes),
    'Installed module file differs: ' + id + '/' + file);
  return target;
}
module.exports = { resolve };
