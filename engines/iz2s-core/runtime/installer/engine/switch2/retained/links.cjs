// These are candidates from captured native bytes, not inferred mappings.
function linksFrom(bytes, address, seed) {
  const links = [];
  for (let offset = (0x4000 - address % 0x4000) % 0x4000; offset + 64 <= bytes.length; offset += 0x4000) {
    const value = Number(bytes.readBigUInt64LE(offset));
    if (!Number.isSafeInteger(value) || value <= 2 ** 32 || value >= 2 ** 39 ||
        value % 0x4000 || Math.abs(value - seed) >= 0x2000000) continue;
    links.push({source: address + offset, value,
      zeroTail: !bytes.subarray(offset + 8, offset + 64).some(Boolean)});
  }
  return links;
}
module.exports = {linksFrom};
