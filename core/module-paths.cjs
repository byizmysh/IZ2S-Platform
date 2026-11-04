// Stable API identities and human-readable installation directories are separate.
// Discovery reads bounded JSON only; it never imports an extension's code.
const fs = require('node:fs'), path = require('node:path');
const ID = /^[a-z0-9][a-z0-9-]{0,47}$/;
const DIRECTORY = /^[A-Za-z0-9][A-Za-z0-9-]{0,63}$/;
const packageDescriptors = new Map();
function packagedDescriptor(source, stat) {
  const stamp = [stat.ino, stat.size, stat.mtimeMs, stat.ctimeMs].join(':');
  const cached = packageDescriptors.get(source);
  if (cached?.stamp === stamp) return cached.descriptor;
  const descriptor = require('./archive.cjs').inspect(fs.readFileSync(source)).descriptor;
  // Keep metadata only. Full verified package bytes belong to modules.cjs.
  packageDescriptors.set(source, { stamp, descriptor });
  if (packageDescriptors.size > 64) packageDescriptors.delete(packageDescriptors.keys().next().value);
  return descriptor;
}
function directory(descriptor) {
  const name = descriptor.installationDirectory ?? descriptor.id;
  if (typeof descriptor.id!=='string' || typeof name!=='string' || !ID.test(descriptor.id) || !DIRECTORY.test(name)) throw Error('Invalid module installation directory');
  return name;
}
function discover(root) {
  const parent = path.join(root, 'modules'), items = [], errors = [];
  if (!fs.existsSync(parent)) return { items, errors };
  for (const entry of fs.readdirSync(parent, { withFileTypes: true }).sort((a,b)=>a.name.localeCompare(b.name))) {
    if (!entry.isDirectory() || !DIRECTORY.test(entry.name)) continue;
    const home = path.join(parent, entry.name), file = path.join(home, 'module.json');
    try {
      let descriptor;
      if (!fs.existsSync(file)) {
        const names=fs.readdirSync(home).filter(name=>/^[a-z0-9][a-z0-9-]{0,47}\.izm$/.test(name));
        if(!names.length)continue;
        if(names.length!==1)throw Error('Ambiguous module package');
        const source=path.join(home,names[0]),stat=fs.lstatSync(source),archive=require('./archive.cjs');
        if(!stat.isFile()||stat.isSymbolicLink()||stat.size>archive.MAX_ARCHIVE)throw Error('Invalid module package file');
        descriptor=packagedDescriptor(source,stat);
      } else {
      const stat = fs.lstatSync(file);
      if (!stat.isFile() || stat.isSymbolicLink() || stat.size > 65536) throw Error('Invalid module descriptor file');
      descriptor = JSON.parse(fs.readFileSync(file, 'utf8'));
      }
      const name = directory(descriptor);
      // The identity-named directory remains readable during an explicit migration.
      if (entry.name !== name && entry.name !== descriptor.id) throw Error('Module installation directory mismatch');
      items.push({ id: descriptor.id, home, descriptor });
    } catch (error) { errors.push({ module: entry.name, error: error.message }); }
  }
  const counts = new Map();
  for (const item of items) counts.set(item.id, (counts.get(item.id)||0)+1);
  for (const [id,count] of counts) if (count > 1) errors.push({module:id,error:'Duplicate module identity: '+id});
  return { items: items.filter(item=>counts.get(item.id)===1), errors };
}
function find(root, id) {
  if (!ID.test(id)) throw Error('Invalid module id');
  const inventory = discover(root);
  const duplicate = inventory.errors.find(e=>e.module===id);
  if (duplicate) throw Error(duplicate.error);
  return inventory.items.find(item=>item.id===id)?.home ?? null;
}
function home(root, id) { return find(root,id) ?? path.join(root,'modules',id); }
function target(root, descriptor) { return path.join(root,'modules',directory(descriptor)); }
module.exports = { directory, discover, find, home, target };
