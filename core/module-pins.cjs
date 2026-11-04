const modulePaths = require('./module-paths.cjs');
// A native-session child receives metadata only. Verified files remain in the
// immutable version directory; replacing an IZM cannot change that session.
const fs = require('node:fs'), path = require('node:path');
const archive = require('./archive.cjs'), { relative, assert } = require('./descriptors.cjs');
let previous, view;
const cache = new Map();
function selection(root) {
  const raw = process.env.IZ2S_MODULE_PINS;
  if (raw !== previous) { previous = raw; view = raw ? JSON.parse(raw) : null; cache.clear(); }
  return view?.version === 1 && view.root === path.resolve(root) ? view : null;
}
function remember(root, id, pkg) {
  const selected = selection(root);
  if (selected && !selected.modules[id]) selected.modules[id] = { sha256: pkg.sha256, packageBytes: pkg.packageBytes };
}
function read(root, id, home) {
  const pin = selection(root)?.modules[id];
  if (!pin) return null;
  assert(/^[a-f0-9]{64}$/.test(pin.sha256), 'Invalid module session fingerprint');
  const folder = path.join(home ?? modulePaths.home(root, id), '.versions', pin.sha256);
  if (cache.has(folder)) return cache.get(folder);
  const manifest = fs.readFileSync(path.join(folder, 'checksums.json'));
  assert(manifest.length <= 1024 * 1024, 'Pinned checksum manifest too large');
  const checks = JSON.parse(manifest), names = Object.keys(checks.files || {});
  assert(names.length > 0 && names.length < archive.MAX_FILES, 'Invalid pinned file inventory');
  let size = 0;
  const files = names.map(name => {
    relative(name); const target = path.join(folder, name), stat = fs.lstatSync(target);
    size += stat.size;
    assert(stat.isFile() && !stat.isSymbolicLink() && stat.size <= archive.MAX_FILE && size <= archive.MAX_EXPANDED, 'Invalid pinned module file');
    const bytes = fs.readFileSync(target);
    assert(archive.sha(bytes) === checks.files[name], 'Pinned module checksum differs: ' + id + '/' + name);
    return { name, bytes };
  });
  files.push({ name: 'checksums.json', bytes: manifest });
  const pkg = archive.inspect(archive.encode(files));
  assert(pkg.descriptor.id === id, 'Pinned module identity differs');
  pkg.sha256 = pin.sha256; pkg.packageBytes = pin.packageBytes;
  cache.set(folder, pkg); return pkg;
}
module.exports = { read, remember };
