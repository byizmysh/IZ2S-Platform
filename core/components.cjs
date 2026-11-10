const registry = require('./modules.cjs'),
  ROOT = require('./root.cjs');
function create(root = ROOT) {
async function get(id, name) {
  return (await registry.loadServer(root, id)).load(name);
}
async function provider(capability, { optional = false } = {}) {
  const modules = (await registry.metadata(root)).modules.filter(
    (m) => m.descriptor.provides?.[capability],
  );
  if (!modules.length && optional) return null;
  if (modules.length !== 1) throw Error('Expected one provider for ' + capability);
  return get(modules[0].id, modules[0].descriptor.provides[capability]);
}
return { get, provider };
}
module.exports = { ...create(), create };
