// Reclaim root-level NS2_GAP entries on the inspected application mounts.
const fs = require('node:fs/promises');
const path = require('node:path');
const check = (ok, message) => {if (!ok) throw Error(message);};

const gapScratch = entry => entry.name.startsWith('NS2_GAP');

function scratchDeleteCommand(type) {
  if (type === 0) return 4; // DeleteDirectoryRecursively
  if (type === 1) return 1; // DeleteFile
  throw Error('NS2_GAP entry has an unsupported type');
}

async function main() {
  const proof = {started: new Date().toISOString(), previous: [], removed: [], cleanedUp: false};
  const {Session, live} = require('./app/session.cjs');
  const mounted = require('./app/mounted-save.cjs');
  const filesystem = require('./app/filesystem.cjs');
  const domain = require('./app/domain.cjs');
  const {paths} = require('./memory/paths.cjs');
  const s = new Session('cleanup-prior-launches');
  proof.session = live.session;
  const file = path.join(__dirname, 'scratch-cleanup.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2) + '\n', {mode: 0o600});
  try {
    await s.initialize();
    const {normal} = paths(s, domain);
    const captured = require('./app/mounts.json');
    const targets = ['save'];
    if (captured.mounts?.some(m => m.name === 'tmp')) targets.push('tmp');
    for (const name of targets) {
      const handle = await mounted.cloneMountedFilesystem(s, name);
      const root = await filesystem.listDirectory(s, handle, '/', 4096);
      const gaps = root.filter(gapScratch).map(item => ({name, path: '/' + item.name}));
      proof.previous.push(...gaps);
      for (const record of gaps) {
        const entry = await normal(handle, 7, record.path);
        if (entry.canonicalResult === 0x202) continue;
        check(!entry.result, 'NS2_GAP entry lookup failed');
        const command = scratchDeleteCommand(entry.data.readUInt32LE());
        proof.pending = record; await save();
        check(!(await normal(handle, command, record.path)).result, 'NS2_GAP removal failed');
        check(!(await s.ipc('Commit previous scratch removal', handle, 10)).result, 'Scratch cleanup commit failed');
        check((await normal(handle, 7, record.path)).canonicalResult === 0x202, 'NS2_GAP entry still exists');
        proof.removed.push(proof.pending); delete proof.pending; await save();
      }
      await s.close(handle);
    }
  } catch (error) {proof.error = error.stack; throw error;}
  finally {
    await s.cleanup(); proof.cleanedUp = !s.base && !s.handles.size;
    proof.completed = new Date().toISOString(); await save();
  }
  return proof;
}

module.exports = {main, gapScratch, scratchDeleteCommand};
