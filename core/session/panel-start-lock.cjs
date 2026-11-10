'use strict';
const fs = require('node:fs/promises'), path = require('node:path'), os = require('node:os');
const {processIdentity, processAlive} = require('./panel-handover.cjs');
async function acquireStartupLock({root, port, directory = path.join(os.tmpdir(),
  'iz2s-panel-' + (process.getuid?.() ?? 'user') + '-' + port + '.lock'),
  timeout = 60000, sleep = ms => new Promise(resolve => setTimeout(resolve, ms))}) {
  const file = path.join(directory, 'owner.json');
  const readOwner = () => fs.readFile(file, 'utf8').then(JSON.parse).catch(() => null);
  const identity = process.platform === 'linux' ? await processIdentity(process.pid) : null;
  const owner = {pid: process.pid, start: identity?.start, root: path.resolve(root)};
  for (let elapsed = 0;; elapsed += 200) {
    try {
      await fs.mkdir(directory, {mode: 0o700});
      try { await fs.writeFile(file, JSON.stringify(owner), {flag: 'wx', mode: 0o600}); }
      catch (error) { await fs.rm(directory, {recursive: true, force: true}); throw error; }
      return async () => {
        const current = await readOwner();
        if (current?.pid === owner.pid && current?.start === owner.start && current?.root === owner.root)
          await fs.rm(directory, {recursive: true, force: true});
      };
    } catch (error) {
      if (error.code !== 'EEXIST') throw error;
    }
    const previous = await readOwner();
    if (previous?.pid && Number.isSafeInteger(previous.pid) && previous.pid > 1) {
      const actual = process.platform === 'linux' ? await processIdentity(previous.pid) : null;
      if (!await processAlive(previous.pid) || (actual && actual.start !== previous.start)) {
        // Only remove the same stale owner that was inspected above.
        const current = await readOwner();
        if (JSON.stringify(current) === JSON.stringify(previous)) await fs.rm(directory, {recursive: true, force: true});
        continue;
      }
    } else {
      const stat = await fs.stat(directory).catch(() => null);
      if (stat && Date.now() - stat.mtimeMs > 30000) {
        await fs.rm(directory, {recursive: true, force: true});
        continue;
      }
    }
    if (elapsed >= timeout) throw Error('Another IZ2S launch is still initializing. Wait for its window to open.');
    await sleep(200);
  }
}
module.exports = {acquireStartupLock};
