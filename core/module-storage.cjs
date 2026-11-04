// Console SD paths use POSIX separators on every host OS. PC dataDirectory is separate.
const BASE = '/IZ2S/Homebrew/Modules';
const ID = /^[a-z0-9][a-z0-9-]{0,47}$/;
const queues = new WeakMap();

function paths(id) {
  if (typeof id !== 'string' || !ID.test(id)) throw Error('Invalid module id');
  return Object.freeze({
    id,
    path: BASE + '/' + id,
    virtualPath: '/sd' + BASE + '/' + id,
    displayPath: 'SD:' + BASE + '/' + id + '/',
  });
}

async function prepare(storage, ids) {
  const directories = [...new Set(ids)].sort().map(paths); // Validate before writing anything.
  if (!storage || !storage.writableMounts?.includes('sd') ||
      typeof storage.list !== 'function' || typeof storage.mkdir !== 'function') {
    throw Error('Writable console SD storage required. Enable FS endpoint (--rpc) and open SD & console storage.');
  }
  const task = (queues.get(storage) || Promise.resolve()).then(async () => {
    const listings = new Map(), created = [];
    async function child(parent, name) {
      if (!listings.has(parent)) listings.set(parent, await storage.list(parent));
      const entries = listings.get(parent);
      // The SD card may be case-insensitive; never create a second spelling.
      const matches = entries.filter(entry => entry.name.toLowerCase() === name.toLowerCase());
      if (matches.length > 1) throw Error('Ambiguous module SD folder: ' + parent + '/' + name);
      const existing = matches[0];
      if (existing && existing.type !== 'dir') throw Error('Module SD folder is blocked by a file: ' + parent + '/' + name);
      const value = parent + '/' + (existing?.name || name);
      if (!existing) {
        await storage.mkdir(value);
        entries.push({name, type: 'dir'});
        created.push(value);
      }
      return value;
    }
    let parent = '/sd';
    for (const name of BASE.slice(1).split('/')) parent = await child(parent, name);
    for (const directory of directories) await child(parent, directory.id);
    if (created.length && typeof storage.commit === 'function') await storage.commit();
    return {directories, created};
  });
  // One failed operation is reported to its caller, without poisoning later explicit calls.
  queues.set(storage, task.catch(() => {}));
  return task;
}

function forModule(id) {
  return Object.freeze({...paths(id), async ensure(storage) {
    await prepare(storage, [id]);
    return paths(id);
  }});
}

async function prepareInstalled(storage, root) {
  const {items, errors} = require('./module-paths.cjs').discover(root);
  const result = await prepare(storage, items.map(item => item.id));
  return {...result, skipped: errors};
}

module.exports = {BASE, paths, forModule, prepare, prepareInstalled};
