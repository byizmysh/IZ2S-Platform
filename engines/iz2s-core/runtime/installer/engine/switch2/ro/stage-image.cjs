// Stage a process image in the established SmileBASIC save view.
const fs = require('node:fs/promises');
const {Session, check, u32} = require('../app/session.cjs');
const domain = require('../app/domain.cjs');
const filesystem = require('../app/filesystem.cjs');
const {cloneMountedFilesystem} = require('../app/mounted-save.cjs');
const {pathRequest} = require('./files.cjs');

async function stage(imagePath, policyPath) {
  const image = await fs.readFile(imagePath);
  const policy = await fs.readFile(policyPath);
  const s = new Session('core-image-stage', 0x30000);
  const record = {files: []};
  const save = async () => {};
  await s.initialize();
  try {
    const handle = await cloneMountedFilesystem(s, 'save');
    const pathCall = async (name, id, input = Buffer.alloc(0)) => {
      const text = Buffer.alloc(0x301);
      text.write('/' + name);
      await s.write(s.base + 0x2000, text);
      return domain.send(s, 'Replacement file ' + name, handle,
        pathRequest(id, input, s.base + 0x2000));
    };
    for (const [name, bytes] of [['main.npdm', policy], ['main', image]]) {
      const exists = await pathCall(name, 7);
      if (!exists.result) {
        check(!(await pathCall(name, 1)).result, 'Could not replace ' + name);
      } else {
        check(exists.canonicalResult === 0x202, 'Path lookup failed for ' + name);
      }
      const input = Buffer.alloc(16);
      input.writeBigUInt64LE(BigInt(bytes.length), 8);
      check(!(await pathCall(name, 0, input)).result, 'Create failed for ' + name);
      const child = domain.oneHandle(await pathCall(name, 8, u32(3)));
      try {
        for (let offset = 0; offset < bytes.length; offset += 0x10000) {
          const part = bytes.subarray(offset, offset + 0x10000);
          const args = Buffer.alloc(24);
          args.writeUInt32LE(1);
          args.writeBigUInt64LE(BigInt(offset), 8);
          args.writeBigUInt64LE(BigInt(part.length), 16);
          await s.write(s.base + 0x4000, part);
          const reply = await s.ipc('Write replacement', child, 1, args,
            {send: [{address: s.base + 0x4000, size: part.length, mode: 1}]});
          check(!reply.result, 'Replacement write failed');
        }
        check((await filesystem.readFile(s, child, 0, bytes.length)).equals(bytes), 'Readback differs');
      } finally {
        await s.close(child);
      }
      record.files.push({name, size: bytes.length});
      await save();
    }
    check(!(await s.ipc('Commit replacement', handle, 10)).result, 'Commit failed');
    await s.close(handle);
    await s.cleanup();
    return record;
  } finally {
    await s.cleanup();
  }
}
module.exports = {stage};
