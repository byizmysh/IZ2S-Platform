// Bind ordinary FS experiments to the current, freshly verified application.
const fs = require('node:fs/promises');
const path = require('node:path');
const check = (ok, message) => { if (!ok) throw Error(message); };
const u64 = value => { const bytes = Buffer.alloc(8); bytes.writeBigUInt64LE(BigInt(value)); return bytes; };

async function connect(label, bytes = 0x10000) {
  const directory = path.join(__dirname, '../app');
  const {Session, live} = require(path.join(directory, 'session.cjs'));
  const s = new Session(label, bytes);
  s.log = path.join(__dirname, label + '-events.jsonl');
  try {
    await s.initialize();
    require("./session-tools.cjs").install(s, live, require(path.join(directory, 'bulk-transfer.cjs')));
  }
  catch (error) { await s.cleanup(); throw error; }
  return {s, live, directory};
}

module.exports = {connect, check, u64};
