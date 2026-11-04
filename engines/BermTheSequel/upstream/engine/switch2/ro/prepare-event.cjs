const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const {exportHandle} = require("./export-handle.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
async function main() {
  const memory = await connect();
  const notifier = JSON.parse(await fs.readFile(path.join(__dirname, memory.info.session + '-notifier-proof.json')));
  check(notifier.report === memory.reportFile && notifier.identityVerified, 'Stale notifier proof');
  const b = await memory.read(notifier.B + 0x20, 16);
  check(b.readUInt32LE(4) === notifier.handle && b.readUInt32LE(8) > 0x8000 &&
    b.readUInt32LE(8) !== notifier.handle, 'Native SystemEvent layout changed');
  const copy = await exportHandle(memory, b.readUInt32LE(8), 'owned notifier writable event');
  const proof = {session: memory.info.session, report: memory.reportFile, sourceHandle: notifier.handle,
    read: notifier.event.handle, write: copy.handle, export: copy.proof, calls: []};
  const file = path.join(__dirname, memory.info.session + '-gate-event.json');
  const save = () => fs.writeFile(file, JSON.stringify(proof, null, 2));
  await save();
  for (const [command, handle, expected] of [
    ['event-clear', proof.read, 0], ['wait-zero', proof.read, 0xea01],
    ['event-signal', proof.write, 0], ['wait-zero', proof.read, 0],
    ['event-clear', proof.read, 0], ['wait-zero', proof.read, 0xea01]]) {
    const r = await memory.request({command: 'root-native-' + command, handle});
    proof.calls.push(r); await save();
    check(r.result === expected, 'Native event signal/clear qualification failed');
  }
  proof.verified = true; await save();
  console.log(JSON.stringify({file, sourceHandle: proof.sourceHandle, read: proof.read, write: proof.write, verified: true}));
}
main().catch(e => { console.error(e.stack); process.exitCode = 1; });
