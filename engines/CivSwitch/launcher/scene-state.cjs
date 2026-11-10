// Shared scene mailbox, retained at its original ABI address. It belongs to the
// shell; calculator digits and glyph resources are initialized by Pop Calc only.
const OFFSET = 0x700000, MAGIC = 0x495a4335;
async function initialize({ rpc, mapped }) {
  const header = Buffer.alloc(32);
  header.writeUInt32LE(MAGIC, 28);
  await rpc.write(mapped + OFFSET, header);
}
module.exports = { initialize, OFFSET, MAGIC };
