// Map, execute and unload a supplied NRO through the standard ldr:ro interface.
const fs = require('node:fs/promises');
const {runPath} = require('../../shared/paths.cjs');

const PageSize = 0x1000;
const EntryOffset = 0x80;
const CurrentProcessHandle = 0xffff8001;

// Standard RO commands carry a PID placeholder before their arguments.
function argumentsWithPid(...values) {
  const data = Buffer.alloc(8 * (values.length + 1));
  values.forEach((value, index) => data.writeBigUInt64LE(BigInt(value), 8 * (index + 1)));
  return data;
}

async function main(options = {}) {
  const {Session, live, check} = require('../app/session.cjs');
  const session = new Session(options.label, 0x20000);
  const file = runPath('switch2', 'ro', options.filename);
  const record = {session: live.session, steps: [], complete: false};
  const save = () => fs.writeFile(file, JSON.stringify(record, null, 2) + '\n');
  const command = async (handle, id, data, copies = []) => {
    record.pending = id;
    await save();
    const reply = await session.ipc('ldr:ro command ' + id, handle, id, data,
      {pid: true, copies});
    record.steps.push({command: id, result: reply.result});
    delete record.pending;
    await save();
    check(!reply.result, 'ldr:ro command ' + id + ' failed: 0x' + reply.result.toString(16));
    return reply;
  };
  try {
    await session.initialize();
    record.allocation = session.base;
    await save();
    const handle = await session.openService('ldr:ro');
    record.handle = handle;
    await command(handle, 4, argumentsWithPid(), [CurrentProcessHandle]);

    const image = options.image;
    const source = session.base + 0x8000;
    const bss = source + image.length;
    await session.write(source, image);
    await session.write(bss, Buffer.alloc(PageSize));
    const reply = await command(handle, 0, argumentsWithPid(source, image.length, bss, PageSize));
    const destination = Number(reply.data.readBigUInt64LE());
    record.destination = destination;
    record.mappings = [];
    const segmentOffsets = [0, image.readUInt32LE(0x28), image.readUInt32LE(0x30), image.length];
    for (const offset of segmentOffsets) {
      record.mappings.push(await session.rpc.query(destination + offset));
    }
    const expectedPermissions = [5, 1, 3, 3];
    check(record.mappings.every((mapping, page) =>
      !mapping.result && mapping.permissions === expectedPermissions[page]),
      'Mapped segment permissions differ from RX/R/RW/RW');
    await save();

    let operationError;
    try {
      record.returnValue = await options.execute(session, destination + EntryOffset, record, save);
      check(record.returnValue === 0, 'Bootstrap returned error ' + record.returnValue);
    } catch (error) {
      if (!record.joined) throw error;
      operationError = error;
    }
    await save();
    await command(handle, 1, argumentsWithPid(destination));
    record.unmappedRegions = [];
    for (const offset of segmentOffsets) {
      record.unmappedRegions.push(await session.rpc.query(destination + offset));
    }
    check(record.unmappedRegions.every(mapping => !mapping.result && mapping.permissions === 0),
      'An NRO segment remains accessible after unload');
    await save();
    await session.close(handle);
    await session.cleanup();
    if (operationError) throw operationError;
    record.complete = true;
    await save();
    return {file, complete: true, returnValue: record.returnValue};
  } catch (error) {
    // Preserve source memory if a mapping or synchronous call has uncertain state.
    // Do not free pages that RO may still have mapped into this process.
    record.error = error.stack;
    await save();
    throw error;
  }
}

module.exports = {main, argumentsWithPid};
