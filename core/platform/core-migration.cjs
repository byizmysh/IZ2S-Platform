'use strict';
const fs = require('node:fs/promises');
const path = require('node:path');
const legacy = require('./legacy-core.json');

function configureEnvironment(env = process.env, root = path.resolve(__dirname, '../..')) {
  if (!env.IZ2S_CORE_OUT && env[legacy.outputVariable]) env.IZ2S_CORE_OUT = env[legacy.outputVariable];
  if (!env.IZ2S_CORE_OUT) return;
  const oldState = Object.entries(legacy.directories).find(([, target]) => target === 'data/core');
  const previous = path.resolve(root, oldState[0]), selected = path.resolve(env.IZ2S_CORE_OUT);
  if (selected === previous || selected.startsWith(previous + path.sep))
    env.IZ2S_CORE_OUT = path.resolve(root, oldState[1]) + selected.slice(previous.length);
}
async function exists(file) {
  try { return await fs.lstat(file); }
  catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}
function displayText(value) {
  return String(value).replace(new RegExp('\\b' + legacy.displayLabel + '\\b', 'g'), 'IZ2S');
}
function normalize(value, root) {
  if (Array.isArray(value)) return value.map(item => normalize(item, root));
  if (value && typeof value === 'object') {
    const result = {};
    for (const [key, item] of Object.entries(value)) {
      const target = key === legacy.engineId ? 'iz2s-core' : legacy.properties[key] || key;
      // A new-format value wins if both versions were already saved.
      if (target !== key && Object.hasOwn(value, target)) continue;
      result[target] = normalize(item, root);
    }
    return result;
  }
  if (typeof value !== 'string') return value;
  if (value === legacy.engineId) return 'iz2s-core';
  if (legacy.tabIds[value]) return legacy.tabIds[value];
  for (const [old, current] of Object.entries(legacy.directories)) {
    const prefix = path.join(root, old);
    if (value === prefix || value.startsWith(prefix + path.sep)) return path.join(root, current) + value.slice(prefix.length);
  }
  return displayText(value);
}
async function migrate(root, {retireLink = require('./core-service-upgrade.cjs').retireLink} = {}) {
  root = path.resolve(root);
  configureEnvironment(process.env, root);
  const moves = {...legacy.directories, ...legacy.stateFiles};
  const selected = [];
  for (const [old, current] of Object.entries(moves)) {
    const from = path.join(root, old), to = path.join(root, current);
    const stat = await exists(from);
    if (!stat) continue;
    if (stat.isSymbolicLink()) throw Error('Cannot migrate a symbolic state path: ' + from);
    if (await exists(to)) throw Error('Both old and new IZ2S state exist. Preserve both and resolve the conflict before starting: ' + from + ' -> ' + to);
    selected.push([from, to]);
  }
  if (selected.length) await retireLink(root);
  for (const [from, to] of selected) {
    await fs.mkdir(path.dirname(to), {recursive: true});
    await fs.rename(from, to);
  }
  // Credentials, binary state and original log text are kept byte-for-byte.
  const directories = ['data/log/iz2s-core', 'data/state/workspace-tabs'];
  const records = ['data/state/launcher-settings.json', 'data/state/launch-configurations.json'];
  for (const dir of directories) {
    const names = await fs.readdir(path.join(root, dir)).catch(e => { if (e.code === 'ENOENT') return []; throw e; });
    records.push(...names.filter(n => n.endsWith('.json')).map(n => dir + '/' + n));
  }
  for (const name of records) {
    const file = path.join(root, name);
    const stat = await exists(file); if (!stat) continue;
    if (!stat.isFile() || stat.isSymbolicLink()) throw Error('Invalid IZ2S state record: ' + file);
    const original = await fs.readFile(file, 'utf8');
    const before = JSON.parse(original), after = normalize(before, root);
    if (JSON.stringify(before) !== JSON.stringify(after)) {
      const temp = file + '.' + require('node:crypto').randomUUID() + '.tmp';
      try {
        await fs.writeFile(temp, JSON.stringify(after, null, 2) + '\n', {mode: stat.mode & 0o777, flag:'wx'});
        await fs.rename(temp, file);
      } finally { await fs.rm(temp, {force: true}); }
    }
    if (name.startsWith('data/state/workspace-tabs/')) {
      const stem = path.basename(name, '.json'), id = legacy.tabIds[stem];
      if (id) {
        const target = path.join(path.dirname(file), id + '.json');
        if (await exists(target)) throw Error('Both old and new workspace tabs exist: ' + id);
        await fs.rename(file, target);
      }
    }
  }
  return {moved: selected.length};
}
module.exports = {migrate, normalize, configureEnvironment, displayText};
