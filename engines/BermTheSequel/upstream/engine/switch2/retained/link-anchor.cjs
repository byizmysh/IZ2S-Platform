// The allocator's external link lives at a fixed offset from the pool base.
// Its self-referential next/previous pair and extent distinguish pool candidates.
function externalLink(bytes, offset, address) {
  if (offset < 0 || offset + 24 > bytes.length) return undefined;
  const next = Number(bytes.readBigUInt64LE(offset));
  const previous = Number(bytes.readBigUInt64LE(offset + 8));
  const extent = Number(bytes.readBigUInt64LE(offset + 16));
  const valid = Number.isSafeInteger(next) && next >= 2 ** 32 && next < 2 ** 39 && next % 8 === 0 &&
    next === previous && Math.abs(next - address) > 0x8000000 &&
    Number.isSafeInteger(extent) && extent >= 0x10000 && extent <= 0x10000000 && extent % 0x1000 === 0;
  return valid ? {next, previous, extent} : undefined;
}

module.exports = {externalLink};
