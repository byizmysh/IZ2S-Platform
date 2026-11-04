// Identify the owned notifier's request buffer and verify ordinary inline IPC.
const fs = require('node:fs/promises');
const path = require('node:path');
const {check, read} = require("./client.cjs");

async function bind(client) {
  const m = client.memory, session = m.info.session;
  const native = path.join(__dirname, '../ro');
  const layout = await read(path.join(native, session + '-layout.json'));
  const notifier = await read(path.join(native, session + '-notifier-proof.json'));
  check(layout.report === m.reportFile && notifier.report === m.reportFile && notifier.identityVerified &&
    await m.pointer(notifier.E + 8) === notifier.B, 'Notifier identity belongs to another object or boot');
  const state = await m.request({command: 'root-native-info'}), service = state.notifiers[0];
  const bytes = await m.read(layout.slots, 0x10000), rows = [];
  for (let offset = 0; offset + 0x88 <= bytes.length; offset += 0x88) {
    const raw = bytes.subarray(offset, offset + 0x88), pointer = at => Number(raw.readBigUInt64LE(at));
    if (pointer(0) !== layout.holderVptr || pointer(0x70) !== notifier.E) continue;
    rows.push({address: layout.slots + offset, object: notifier.E, wait: raw.readUInt32LE(0x30),
      receive: raw.readUInt32LE(0x48), state: raw.readUInt32LE(0x40), request: pointer(0x60),
      capacity: pointer(0x68), metadata: pointer(0x78), raw: raw.toString('hex')});
  }
  check(service && rows.length === 1 && rows[0].state === 1 && rows[0].capacity === 256 &&
    rows[0].wait === rows[0].receive, 'The owned notifier session is not unique and idle');
  const replies = await client.worker({kind: 'notifier-probe', handle: service, candidates: [0xffff8001, 0]});
  for (const reply of replies) {
    check(!reply.transport && !reply.result && reply.copies.length === 1, 'Stock notifier rejected bounded extra input');
    for (const handle of reply.copies) await m.request({command: 'root-native-close-handle', handle});
  }
  const result = {session, report: m.reportFile, service, row: rows[0], replies,
    verified: true, protocolEvidence: client.file};
  const file = path.join(__dirname, session + '-notifier-input-' + Date.now() + '.json');
  await fs.writeFile(file, JSON.stringify(result, null, 2) + '\n', {mode: 0o600});
  return {file, ...result};
}
module.exports = {bind};
