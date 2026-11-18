'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const {createHash} = require('node:crypto');
const {readMetadata} = require('../../../core/console/nro-metadata.cjs');
const catalog = require('../../../core/services/app-catalog.cjs');
const {jpegIcon} = require('../../../core/services/asset-validation.cjs');

const DEFAULT_ROOT = path.join(require('../../../core/root.cjs'), 'data', 'homebrew');
const MENU_LIMIT = 15;

async function directories(folder) {
  const entries = await fs.readdir(folder, {withFileTypes: true});
  return entries.sort((a, b) => a.name.localeCompare(b.name) || Buffer.compare(Buffer.from(a.name), Buffer.from(b.name)));
}

async function* candidates(root, entries) {
  for (const entry of entries) {
    if (entry.name.startsWith('.')) continue;
    if (entry.isFile() && /\.nro$/i.test(entry.name)) yield entry.name;
  }
  for (const entry of entries) {
    if (!entry.isDirectory() || entry.name.startsWith('.')) continue;
    const children = await directories(path.join(root, entry.name)).catch(error => {
      if (error.code === 'ENOENT' || error.code === 'ENOTDIR') return [];
      throw error;
    });
    for (const child of children) {
      if (child.isFile() && !child.name.startsWith('.') && /\.nro$/i.test(child.name))
        yield [entry.name, child.name].join('/');
    }
  }
}

async function preview(root, relative, id) {
  const file = path.join(root, relative);
  const {name, author, version, icon: embedded} = await readMetadata(file);
  let icon = null;
  if (embedded?.subarray(0, 3).equals(Buffer.from([0xff, 0xd8, 0xff]))) {
    icon = await jpegIcon(embedded).then(() => embedded, () => null);
  }
  const description = [version, author].filter(Boolean).join('  ·  ') || 'Homebrew preview';
  const fingerprint = createHash('sha256').update(JSON.stringify([id, name, description]))
    .update(icon || Buffer.alloc(0)).digest('hex');
  return {
    id, name, author, version, description, root: file, fingerprint, effects: {},
    previewOnly: true,
    manifest: {displayOnly: true, category: 'tool'},
    files: {icon: icon ? {bytes: icon} : null},
  };
}

async function scan(appRoot, {homebrewRoot = process.env.IZ2S_NRO_DIR || DEFAULT_ROOT} = {}) {
  const result = await catalog.scan(appRoot);
  if (result.apps.length >= MENU_LIMIT) return result;
  const root = path.resolve(homebrewRoot);
  const used = new Set(result.apps.map(app => app.id));
  try {
    const entries = await directories(root).catch(error => {
      if (error.code === 'ENOENT') return [];
      throw error;
    });
    for await (const relative of candidates(root, entries)) {
      const id = 'nro-' + createHash('sha256').update(relative).digest('hex').slice(0, 40);
      try {
        if (used.has(id)) throw Error('Duplicate homebrew identity');
        result.apps.push(await preview(root, relative, id));
        used.add(id);
        if (result.apps.length === MENU_LIMIT) break;
      } catch (error) {
        result.rejected.push({folder: relative, error: error.message});
      }
    }
  } catch (error) {
    result.rejected.push({folder: root, error: error.message});
  }
  return result;
}

module.exports = {scan};
