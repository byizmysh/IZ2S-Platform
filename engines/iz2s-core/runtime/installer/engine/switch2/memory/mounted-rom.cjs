// Trace the current application's RomFS storage and copy its domain object.
// The borrowed application session and original domain object remain owned by it.
const fs = require('node:fs/promises');
const path = require('node:path');
const {check} = require("./session.cjs");

async function cloneMountedRom(s, live, directory) {
  const mounts = JSON.parse(await fs.readFile(path.join(directory, 'mounts.json')));
  check(mounts.session === live.session, 'Mount capture belongs to another session');
  const rom = mounts.mounts.find(item => item.name === 'rom');
  const save = mounts.mounts.find(item => item.name === 'save');
  check(rom && save, 'Required native mounts are missing');
  const accessor = await s.read(rom.accessor, 48);
  check(accessor.subarray(24, 40).toString('utf8').split('\0')[0] === 'rom' &&
    Number(accessor.readBigUInt64LE(40)) === rom.implementation, 'RomFS mount changed');
  const root = await s.read(rom.implementation, 0x128);
  const storage = Number(root.readBigUInt64LE(0x118));
  check(Number(root.readBigUInt64LE()) === live.sdk + 0xcb7598 &&
    storage === Number(root.readBigUInt64LE(0x120)), 'RomFS implementation differs');
  const wrapper = await s.read(storage, 16);
  check(Number(wrapper.readBigUInt64LE()) === live.sdk + 0xcb46d8, 'RomFS storage wrapper differs');
  const serviceAddress = Number(wrapper.readBigUInt64LE(8));
  const service = await s.read(serviceAddress, 32);
  check(Number(service.readBigUInt64LE()) === live.sdk + 0xcb5508, 'RomFS service type differs');
  const manager = Number(service.readBigUInt64LE(16)), objectId = service.readUInt32LE(24);
  const saveService = await s.read(save.service, 32);
  check(manager === Number(saveService.readBigUInt64LE(16)) && objectId > 0,
    'RomFS and save do not share the expected manager');
  const state = await s.read(manager, 0x80);
  check(state.readUInt32LE(8) === 0x800 && state.readUInt32LE(0x5c) === 3,
    'Unexpected SDK session manager');
  const borrowed = (await s.read(Number(state.readBigUInt64LE(16)), 4)).readUInt32LE();
  check(borrowed > 0 && borrowed < 0xffff8000, 'Invalid borrowed session');
  const domain = require(path.join(directory, 'domain.cjs'));
  const id = Buffer.alloc(4); id.writeUInt32LE(objectId);
  const handle = domain.oneHandle(await domain.send(s, 'Copy mounted RomFS storage', borrowed,
    domain.wire.control(1, id)));
  const source = {mount: rom.implementation, storage, serviceAddress, manager, borrowed, objectId};
  await s.record({operation: 'MountedRomStorageCopied', ...source, handle});
  return {handle, source};
}

module.exports = {cloneMountedRom};
