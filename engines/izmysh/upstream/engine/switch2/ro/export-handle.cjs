// Copy one existing FS handle through our identified event notifier.
const fs = require('node:fs/promises');
const path = require('node:path');
const check = (ok, message) => { if (!ok) throw Error(message); };
const u32 = value => { const b = Buffer.alloc(4); b.writeUInt32LE(value); return b; };

async function exportHandle(memory, handle, label) {
  // IPC resolves this pseudo-handle in FS, producing a real FS process handle.
  check(Number.isInteger(handle) && handle > 0 && (handle < 0x80000000 || handle === 0xffff8001),
    'Expected an existing FS handle or its current-process pseudo-handle');
  const file = path.join(__dirname, memory.info.session + '-notifier-proof.json');
  const notifier = JSON.parse(await fs.readFile(file));
  check(notifier.session === memory.info.session && notifier.report === memory.reportFile &&
    notifier.identityVerified, 'Notifier proof is from another endpoint');
  check(await memory.pointer(notifier.E + 8) === notifier.B, 'Notifier backend changed');
  const original = u32(notifier.handle), replacement = u32(handle);
  const entry = {session: memory.info.session, label, sourceHandle: handle,
    notifier: file, started: new Date().toISOString(), restored: false};
  const output = path.join(__dirname, memory.info.session + '-export-' + Date.now() + '.json');
  const save = () => fs.writeFile(output, JSON.stringify(entry, null, 2) + '\n', {mode: 0o600});
  await save();
  await memory.write(notifier.handleField, original, replacement);
  try {
    entry.copy = await memory.request({command: 'root-native-notifier-event'});
    await save();
  } finally {
    await memory.write(notifier.handleField, replacement, original);
    entry.restored = true;
    await save();
  }
  return {handle: entry.copy.handle, proof: output};
}

module.exports = {exportHandle};
