const fs = require('node:fs/promises'),
  path = require('node:path'),
  { ID } = require('../core/descriptors.cjs');
async function create(kind, id, target) {
  if (!['engine', 'shell', 'module'].includes(kind) || !ID.test(id))
    throw Error('Use engine|shell|module and a lowercase ID');
  try {
    await fs.lstat(target);
    throw Error('Destination already exists');
  } catch (e) {
    if (e.code !== 'ENOENT') throw e;
  }
  await fs.cp(path.join(__dirname, 'templates', kind), target, {
    recursive: true,
    errorOnExist: true,
    force: false,
  });
  const file = path.join(target, kind + '.json'),
    d = JSON.parse(await fs.readFile(file));
  d.id = id;
  d.name = id;
  await fs.writeFile(file, JSON.stringify(d, null, 2) + '\n');
  if (kind === 'module') {
    await fs.writeFile(
      path.join(target, 'README.md'),
      '# ' + id + '\n\nDescribe your module here.\n\nConsole SD files: `SD:/IZ2S/Homebrew/Modules/' + id + '/`. Use `context.sdStorage` (see `sdk/SD-STORAGE.md` in IZ2S Platform). PC-local files use `context.dataDirectory`.\n',
    );
    await fs.writeFile(
      path.join(target, 'LICENSE'),
      await fs.readFile(path.join(__dirname, 'LICENSE')),
    );
  }
  return target;
}
if (require.main === module) {
  const [kind, id, target] = process.argv.slice(2);
  create(kind, id, path.resolve(target || id || '.'))
    .then((p) => console.log(p))
    .catch((e) => {
      console.error(e.message);
      process.exitCode = 1;
    });
}
module.exports = { create };
