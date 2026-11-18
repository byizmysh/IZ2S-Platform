// Called only before a fresh server starts. Active sessions keep their archives.
const fs = require('node:fs/promises'), path = require('node:path'), archive = require('../core/archive.cjs');
async function publish(root) {
  const result = { updated: [], errors: [] };
  const directory = path.join(root, 'modules');
  for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    const folder = path.join(directory, entry.name);
    if (!await fs.access(path.join(folder, 'module.json')).then(() => true, () => false)) continue;
    let temporary;
    try {
      const descriptor = require('../core/descriptors.cjs').moduleDescriptor(JSON.parse(await fs.readFile(path.join(folder, 'module.json'))));
      if (![descriptor.id, require('../core/module-paths.cjs').directory(descriptor)].includes(entry.name)) throw Error('Module installation directory mismatch');
      // Packages installed without a development checkout need no rebuilding.
      const sources = [descriptor.server?.entry, descriptor.switch?.entry].filter(Boolean);
      if (!(await Promise.all(sources.map(file => fs.access(path.join(folder, file)).then(() => true, () => false)))).every(Boolean)) continue;
      const bytes = await archive.pack(folder), target = path.join(folder, descriptor.id + '.module.zip');
      const previous = await fs.readFile(target).catch(error => { if (error.code !== 'ENOENT') throw error; return null; });
      if (previous && archive.sameContents(previous, bytes)) continue;
      temporary = path.join(folder, '.publish-' + require('node:crypto').randomUUID());
      await fs.writeFile(temporary, bytes); await fs.rename(temporary, target); result.updated.push(descriptor.id);
    } catch (error) { result.errors.push({ id: entry.name, error: error.message }); }
    finally { if (temporary) await fs.rm(temporary, { force: true }); }
  }
  return result;
}
module.exports = { publish };
