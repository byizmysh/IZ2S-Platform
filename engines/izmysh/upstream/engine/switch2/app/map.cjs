const fs = require('node:fs/promises');
const path = require('node:path');
const {client, live} = require('./rpc.cjs');

async function main() {
  const rpc = await client(), regions = [];
  for (let address = 0; address < 2 ** 39;) {
    if (regions.length >= 4096) throw Error('Application memory map exceeded its bound');
    const region = await rpc.query(address);
    const next = region.address + region.size;
    if (region.result || region.address > address || !Number.isSafeInteger(next) || next <= address)
      throw Error('Application memory map is incomplete');
    regions.push(region); address = next;
  }
  await fs.writeFile(path.join(__dirname, 'memory-map.json'),
    JSON.stringify({session: live.session, complete: true, regions}, null, 2) + '\n', {mode: 0o600});
  console.log(JSON.stringify({complete: true, regions: regions.length}));
}
if (require.main === module) main().catch(error => {console.error(error.stack); process.exitCode = 1;});
