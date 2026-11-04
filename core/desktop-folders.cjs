const fs = require('node:fs/promises'), path = require('node:path');
const { spawn } = require('node:child_process');
const identifier = value => {
  if (!/^[a-z0-9][a-z0-9-]{0,47}$/.test(value || '')) throw Error('Invalid folder identifier');
  return value;
};
async function resolve(root, { kind, id }) {
  root = await fs.realpath(root);
  let folder;
  switch (kind) {
    case 'platform': folder = root; break;
    case 'modules': folder = path.join(root, 'modules'); break;
    case 'module': {
      identifier(id);
      const inventory = await require('./modules.cjs').list(root);
      if (!inventory.modules.some(m => m.id === id || m.descriptor.id === id)) throw Error('Unknown module');
      folder = require('./module-paths.cjs').home(root,id); break;
    }
    case 'engine': case 'engine-source': {
      const engine = require('./descriptors.cjs').scan(root, 'engine').items.find(e => e.id === identifier(id));
      if (!engine) throw Error('Unknown engine');
      folder = kind === 'engine-source' && engine.installation ? path.join(engine.folder, 'upstream') : engine.folder;
      break;
    }
    case 'configuration': {
      const profile = require('../profiles/index.cjs').list(root).find(p => p.id === identifier(id));
      if (!profile) throw Error('Unknown hardware configuration');
      folder = profile.folder; break;
    }
    case 'input': folder = path.join(root, 'hardware/agents/arm-input'); break;
    case 'shell': {
      const shellId = identifier(id || 'izlauncher');
      const shell = require('./descriptors.cjs').scan(root, 'shell').items.find(item => item.id === shellId);
      if (!shell) throw Error('Unknown shell');
      folder = shell.folder; break;
    }
    case 'history': folder = path.join(root, 'data/state/attempts'); break;
    case 'session': {
      const row = await require('./session/ledger.cjs').createLedger(root).read(id);
      if (!row) throw Error('Unknown session');
      folder = path.join(root, 'data/state/attempts', id); break;
    }
    default: throw Error('Unknown folder');
  }
  const inside = value => value === root || value.startsWith(root + path.sep);
  if (!inside(path.resolve(folder))) throw Error('Folder escapes the platform');
  let existing = folder;
  while (!await fs.stat(existing).then(() => true, error => { if (error.code === 'ENOENT') return false; throw error; })) existing = path.dirname(existing);
  if (!inside(await fs.realpath(existing))) throw Error('Folder escapes the platform');
  await fs.mkdir(folder, { recursive: true });
  folder = await fs.realpath(folder);
  if (!inside(folder)) throw Error('Folder escapes the platform');
  return folder;
}
async function open(root, request, { launch = spawn, ...options } = {}) {
  if (request.kind === 'rpc-browser') return require('./rpc-browser.cjs').open(root, request, {launch});
  // Retain access from an already running panel while its live FS owner stays up.
  if (request.kind === 'dolphin-rpc') return require('./dolphin-rpc.cjs').open(root, request, {launch});
  const folder = await resolve(root, request);
  const [command,args] = require('./platform/desktop.cjs').folderCommand(folder,options);
  await new Promise((accept, reject) => {
    const child = launch(command, args, { detached: true, stdio: 'ignore' });
    child.once('error', reject);
    child.once('spawn', () => { child.unref(); accept(); });
  });
  return { ok: true };
}
module.exports = { resolve, open };
