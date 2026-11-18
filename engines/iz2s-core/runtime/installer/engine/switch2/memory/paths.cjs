const {packet} = require("../../shared/ipc.cjs");
const {check} = require("./session.cjs");

function pathPacket(command, input, address, size) {
  return pathPackets(command, input, [{address, size}]);
}

function pathPackets(command, input, descriptors) {
  check(descriptors.length > 0 && descriptors.length <= 2, 'Invalid path descriptor count');
  const original = packet(command, input), bytes = Buffer.alloc(256);
  original.copy(bytes, 0, 0, 8);
  bytes.writeUInt32LE(4 | descriptors.length << 16);
  let at = 8;
  for (const [index, {address, size}] of descriptors.entries()) {
    check(Number.isInteger(size) && size > 0 && size <= 0x800, 'Invalid path descriptor size');
    const a = BigInt(address);
    bytes.writeUInt32LE(Number(BigInt(index) | BigInt(size) << 16n | (a >> 36n & 7n) << 6n |
      (a >> 32n & 15n) << 12n), at);
    bytes.writeUInt32LE(Number(a & 0xffffffffn), at + 4);
    at += 8;
  }
  at = (at + 15) & ~15;
  original.copy(bytes, at, 16, 16 + 16 + input.length);
  return bytes;
}

function paths(s, domain) {
  async function raw(handle, command, bytes, input = Buffer.alloc(0)) {
    const buffer = Buffer.alloc(Math.ceil(bytes.length / 4) * 4);
    bytes.copy(buffer);
    await s.write(s.base + 0x4000, buffer);
    return domain.send(s, 'Path command ' + command, handle,
      pathPacket(command, input, s.base + 0x4000, bytes.length));
  }
  const normal = (handle, command, filename, input) => {
    check(!filename.includes('\0'), 'Path contains an embedded terminator');
    return raw(handle, command, Buffer.from(filename + '\0'), input);
  };
  return {raw, normal};
}

module.exports = {pathPacket, pathPackets, paths};
