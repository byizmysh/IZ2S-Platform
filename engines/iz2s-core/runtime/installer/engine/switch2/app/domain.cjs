// Serial CMIF operations on domains created by this session.
const {u32, u64, check} = require("./session.cjs");
const wire = require("../../shared/domain.cjs");

async function send(s, label, handle, packet, format = {}) {
  await s.write(s.base, packet);
  const transport = await s.call('send', s.base, 4096, handle);
  await s.record({operation: label, handle, transport});
  check(!transport, label + ': transport failed ' + transport);
  const bytes = await s.readOwned(s.base, 256);
  const reply = wire.response(bytes, format);
  for (const child of [...reply.moves, ...reply.copies]) s.handles.add(child);
  await s.record({operation: label + ' reply', result: reply.result,
    objects: reply.objects, moves: reply.moves, copies: reply.copies,
    data: reply.data.subarray(0, 16).toString('hex')});
  return reply;
}

function oneHandle(reply) {
  check(!reply.result && reply.moves.length === 1 && !reply.copies.length,
    'Expected one moved session handle');
  return reply.moves[0];
}

async function openDomain(s, label) {
  const handle = await s.openService('fsp-srv');
  check(!(await s.ipc(label + ' SetCurrentProcess', handle, 1, u64(0), {pid: true})).result,
    label + ': FS initialization failed');
  const reply = await send(s, label + ' ConvertToDomain', handle, wire.control(0));
  check(!reply.result, label + ': domain conversion failed');
  return {label, handle, root: reply.data.readUInt32LE()};
}

async function openStorage(s, domain, dataId = null) {
  const input = dataId === null ? Buffer.alloc(0) :
    Buffer.concat([u64(3), u64(dataId)]);
  const reply = await send(s, domain.label + ' OpenStorage', domain.handle,
    wire.domain(domain.root, dataId === null ? 200 : 202, input), {domain: true});
  check(!reply.result && reply.objects.length === 1, 'Storage did not open in the owned domain');
  return reply.objects[0];
}

async function copyObject(s, domain, objectId) {
  return send(s, domain.label + ' CopyObject', domain.handle,
    wire.control(1, u32(objectId)));
}

async function closeObject(s, domain, objectId) {
  await s.write(s.base, wire.closeObject(objectId));
  const transport = await s.call('send', s.base, 4096, domain.handle);
  await s.record({operation: domain.label + ' CloseObject', objectId, transport});
  check(!transport, 'Owned domain object did not close');
}

async function storageSize(s, handle) {
  const reply = await s.ipc('StorageGetSize', handle, 4);
  check(!reply.result, 'Storage GetSize failed');
  return reply.data.readBigUInt64LE();
}

module.exports = {wire, send, oneHandle, openDomain, openStorage, copyObject, closeObject, storageSize};
