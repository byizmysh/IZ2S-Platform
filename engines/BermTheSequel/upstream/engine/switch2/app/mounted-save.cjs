// Copy an existing application filesystem; never close its borrowed session.
const {live, check, u32} = require("./session.cjs");
const domain = require("./domain.cjs");
const layout = require("../../shared/sdk-layout.json");

async function inspectMountedFilesystem(s, name = 'save') {
  check(['save', 'tmp'].includes(name), 'Only the inspected application mounts are supported');
  const captured = require("./mounts.json");
  check(captured.session === live.session, 'Save mount inspection belongs to another boot');
  let mount = captured.mounts.find(m => m.name === name);
  if (!mount && name === 'tmp') {
    mount = captured.mounts.find(m => m.name === 'save');
  }
  check(mount, `${name === 'save' ? 'Save' : 'Tmp'} mount was not inspected`);
  for (const entry of layout.verifiedFunctions) {
    check((await s.read(live.sdk + entry.offset, entry.bytes.length / 2)).toString('hex') === entry.bytes,
      'SDK mount implementation changed');
  }
  const accessor = await s.read(mount.accessor, 48);
  check(accessor.subarray(24, 40).toString('utf8').split('\0')[0] === mount.name &&
    Number(accessor.readBigUInt64LE(40)) === mount.implementation, `${mount.name} mount changed`);
  const implementation = await s.read(mount.implementation, 24);
  check(Number(implementation.readBigUInt64LE(16)) === mount.service, `${mount.name} service changed`);
  const service = await s.read(mount.service, 32);
  check(Number(service.readBigUInt64LE()) === mount.serviceVtable, `${mount.name} service type changed`);
  const manager = Number(service.readBigUInt64LE(16)), objectId = service.readUInt32LE(24);
  const state = await s.read(manager, 0x80);
  check(state.readUInt32LE(8) === 0x800 && state.readUInt32LE(0x5c) === 3, 'Unexpected SDK session manager');
  const slots = Number(state.readBigUInt64LE(16));
  const borrowed = (await s.read(slots, 4)).readUInt32LE();
  check(borrowed > 0 && borrowed < 0xffff8000 && objectId > 0, `Invalid borrowed ${mount.name} session`);
  return {name: mount.name, borrowed, objectId, manager};
}

async function cloneMountedFilesystem(s, name = 'save') {
  const {name: resolvedName, borrowed, objectId} = await inspectMountedFilesystem(s, name);
  const reply = await domain.send(s, `Copy existing ${resolvedName} filesystem`, borrowed, domain.wire.control(1, u32(objectId)));
  const handle = domain.oneHandle(reply);
  await s.record({operation: 'MountedFilesystemCopied', name: resolvedName, requestedName: name, borrowed, objectId, handle});
  return handle;
}

async function cloneMountedRoot(s) {
  const mount = await inspectMountedFilesystem(s, 'save');
  const code = require("./sdk-proxy-code.json");
  check((await s.read(live.sdk + code.functionOffset, code.bytes.length / 2)).toString('hex') === code.bytes,
    'SDK filesystem proxy getter differs');
  const global = await s.read(live.sdk + code.globalOffset, 16);
  check(global[8] & 1, 'SDK filesystem proxy is not initialized');
  const service = Number(global.readBigUInt64LE());
  const object = await s.read(service, 32), root = object.readUInt32LE(24);
  check(Number(object.readBigUInt64LE(16)) === mount.manager && root > 0 && root !== mount.objectId,
    'SDK root and save do not share the expected domain');
  const handle = domain.oneHandle(await domain.send(s, 'Clone SDK filesystem domain', mount.borrowed, domain.wire.control(2)));
  return {owner: {label: 'Owned clone of SDK filesystem domain', handle, root}, filesystemId: mount.objectId};
}

module.exports = {cloneMountedSave: s => cloneMountedFilesystem(s, 'save'), cloneMountedFilesystem, cloneMountedRoot};
