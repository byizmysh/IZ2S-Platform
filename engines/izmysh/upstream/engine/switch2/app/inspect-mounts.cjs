// Read the application's existing mount objects; never unmount or close them.
const fs = require('node:fs/promises');
const {client} = require('./rpc.cjs');
const {live, check} = require("./session.cjs");
const layout = require("../../shared/sdk-layout.json");
const pointer = (bytes, at = 0) => Number(bytes.readBigUInt64LE(at));

async function inspect() {
  const rpc = await client();
  for (const entry of layout.verifiedFunctions) {
    check((await rpc.read(live.sdk + entry.offset, entry.bytes.length / 2)).toString('hex') === entry.bytes,
      'Live SDK layout differs from the inspected module');
  }
  const head = live.sdk + layout.mountTableOffset;
  let next = pointer(await rpc.read(head + 8, 8));
  const mounts = [], seen = new Set();
  while (next !== head) {
    check(next > 0 && next < 2 ** 39 && !seen.has(next) && seen.size < 32, 'Invalid mount list');
    seen.add(next);
    const accessor = next - 8;
    const bytes = await rpc.read(accessor, 128);
    const name = bytes.subarray(24, 40).toString('utf8').split('\0')[0];
    const implementation = pointer(bytes, 40);
    const implementationBytes = await rpc.read(implementation, 96);
    const service = pointer(implementationBytes, 16);
    const entry = {name, accessor, implementation, service, accessorHex: bytes.toString('hex'),
      implementationHex: implementationBytes.toString('hex')};
    if (name === 'save' || name === 'tmp') {
      const serviceBytes = await rpc.read(service, 128);
      entry.serviceHex = serviceBytes.toString('hex');
      entry.serviceVtable = pointer(serviceBytes);
      entry.serviceVtableHex = (await rpc.read(entry.serviceVtable, 0xa0)).toString('hex');
    }
    mounts.push(entry);
    console.log(JSON.stringify({name, accessor, implementation, service}));
    next = pointer(bytes, 16);
  }
  await fs.writeFile(__dirname + '/mounts.json',
    JSON.stringify({session: live.session, at: new Date().toISOString(), mounts}, null, 2) + '\n', {mode: 0o600});
}

if (require.main === module) inspect().catch(error => {console.error(error.message); process.exitCode = 1;});
