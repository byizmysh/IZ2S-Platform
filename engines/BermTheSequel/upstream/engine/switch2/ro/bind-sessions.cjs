const fs = require('node:fs/promises');
const path = require('node:path');
const {connect} = require("../tools/fs-memory.cjs");
const check = (ok, message) => { if (!ok) throw Error(message); };
const ptr = (b, o = 0) => Number(b.readBigUInt64LE(o));

async function main() {
  const memory = await connect();
  const read = name => fs.readFile(path.join(__dirname, memory.info.session + name)).then(JSON.parse);
  const layout = await read('-layout.json'), proof = await read('-loader-root-copy.json');
  check(layout.report === memory.reportFile && proof.report === memory.reportFile, 'Stale session layout');
  check(layout.slots >= layout.range.address && layout.slots + 0x10000 <= layout.range.address + layout.range.length,
    'Session pool is outside the captured native heap');
  const bytes = await memory.read(layout.slots, 0x10000), rows = [];
  for (let i = 0; i + 0x88 <= bytes.length; i += 0x88) {
    const b = bytes.subarray(i, i + 0x88), domain = ptr(b, 0x70);
    if (ptr(b) !== layout.holderVptr || ![proof.ownDomain, proof.targetDomain].includes(domain)) continue;
    const row = {address: layout.slots + i, domain, parent: ptr(b, 0x28), wait: b.readUInt32LE(0x30),
      tag: ptr(b, 0x38), state: b.readUInt32LE(0x40), receiveResult: b.readUInt32LE(0x44),
      receive: b.readUInt32LE(0x48), request: ptr(b, 0x60), capacity: ptr(b, 0x68),
      metadata: ptr(b, 0x78), raw: b.toString('hex')};
    check(row.wait === row.receive && row.capacity === 0x100 && row.tag === 1 && row.state === 1 &&
      layout.waiters.includes(row.parent), 'Server-session shape differs');
    rows.push(row);
  }
  check(rows.length === 2 && new Set(rows.map(r => r.domain)).size === 2, 'Server bindings are not unique');
  const binding = {session: memory.info.session, report: memory.reportFile, rows, waiters: layout.waiters};
  await fs.writeFile(path.join(__dirname, memory.info.session + '-bound-sessions.json'), JSON.stringify(binding, null, 2));

  const state = await memory.request({command: 'root-native-info'});
  const code = await memory.request({command: 'root-native-code-domain', handle: state.codeFileSystems[0].handle});
  const types = [];
  for (const [name, item] of [['donor', state.donor], ['code', code]]) {
    check(Number.isInteger(item.root) && item.root > 0 && item.root <= layout.count,
      'A domain object ID must be within the verified table before computing its address');
    const address = layout.table + (item.root - 1) * 0x30;
    const entry = await memory.read(address, 0x30), object = ptr(entry, 0x20);
    check(ptr(entry, 0x18) > 2 ** 32 && object > 2 ** 32 && object < 2 ** 39,
      'The domain entry has no live owner or object');
    const header = await memory.read(object, 0x40);
    types.push({name, handle: item.handle, root: item.root, entry: address, domain: ptr(entry, 0x18), object,
      metadata: ptr(entry, 0x28), vptr: ptr(header), raw: entry.toString('hex'), header: header.toString('hex')});
  }
  check(types[0].metadata === types[1].metadata && types[0].vptr === types[1].vptr, 'Filesystem adapter types differ');
  await fs.writeFile(path.join(__dirname, memory.info.session + '-filesystem-types.json'),
    JSON.stringify({session: memory.info.session, report: memory.reportFile, rows: types, compatible: true}, null, 2));
  console.log(JSON.stringify({session: memory.info.session, rows, types}));
}
main().catch(e => { console.error(e.stack); process.exitCode = 1; });
