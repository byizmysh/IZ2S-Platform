const modulePaths = require('./module-paths.cjs');
// A native-session child receives metadata only. Verified files remain in the
// immutable version directory; replacing an IZM cannot change that session.
const fs = require('node:fs'), path = require('node:path');
const archive = require('./archive.cjs'), { relative, assert, moduleDescriptor, ID } = require('./descriptors.cjs');
let previous, view;
const cache = new Map(), descriptors = new Map();
function selection(root) {
  const raw = process.env.IZ2S_MODULE_PINS;
  if (raw !== previous) { previous = raw; view = raw ? JSON.parse(raw) : null; cache.clear(); descriptors.clear(); }
  return view?.version === 1 && view.root === path.resolve(root) ? view : null;
}
function remember(root, id, pkg) {
  const selected = selection(root);
  if (selected && !selected.modules[id]) selected.modules[id] = { sha256: pkg.sha256, packageBytes: pkg.packageBytes, descriptorSha256: archive.sha(pkg.files.find(f=>f.name==='module.json').bytes) };
}
function folderFor(root,id,home,pin){
  assert(ID.test(id) && /^[a-f0-9]{64}$/.test(pin.sha256), 'Invalid module session fingerprint');
  return path.join(home ?? modulePaths.home(root,id), '.versions', pin.sha256);
}
function smallFile(folder,name,maximum){
  const file=path.join(folder,name),stat=fs.lstatSync(file);
  assert(stat.isFile()&&!stat.isSymbolicLink()&&stat.size>0&&stat.size<=maximum,'Invalid pinned metadata: '+name);
  return fs.readFileSync(file);
}
// A provider lookup needs a descriptor, not every binary/shader/source file in
// the library. Loading the selected provider still validates its whole package.
function descriptor(root,id,home){
  const pin=selection(root)?.modules[id];if(!pin)return null;
  const folder=folderFor(root,id,home,pin);if(descriptors.has(folder))return descriptors.get(folder);
  const bytes=smallFile(folder,'module.json',65536),checks=JSON.parse(smallFile(folder,'checksums.json',1024*1024)),hash=archive.sha(bytes);
  assert(checks.algorithm==='sha256'&&checks.files?.['module.json']===hash,'Pinned module descriptor checksum differs');
  if(pin.descriptorSha256!==undefined)assert(pin.descriptorSha256===hash,'Pinned module descriptor changed since session capture');
  const value=moduleDescriptor(JSON.parse(bytes));assert(value.id===id,'Pinned module identity differs');descriptors.set(folder,value);return value;
}
function read(root, id, home) {
  const pin = selection(root)?.modules[id];
  if (!pin) return null;
  const folder = folderFor(root,id,home,pin);
  if (cache.has(folder)) return cache.get(folder);
  const manifest = smallFile(folder,'checksums.json',1024*1024);
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
  // inspectFiles performs the same manifest/content/size/path checks as the
  // archive path, with no synthetic ZIP or repeated compression/CRC pass.
  const pkg = archive.inspectFiles(files.sort((a,b)=>a.name.localeCompare(b.name)));
  assert(pkg.descriptor.id === id, 'Pinned module identity differs');
  if(pin.descriptorSha256!==undefined)assert(archive.sha(files.find(f=>f.name==='module.json').bytes)===pin.descriptorSha256,'Pinned module descriptor changed since session capture');
  pkg.sha256 = pin.sha256; pkg.packageBytes = pin.packageBytes;
  cache.set(folder, pkg); return pkg;
}
function fingerprint(root,id){return selection(root)?.modules[id]?.sha256||null;}
module.exports = { read, remember, descriptor, fingerprint };
