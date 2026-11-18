// Pure payload planning. This file performs no console calls or emulation.
const decoder = new TextDecoder('utf-8', {fatal: true});

function nulWrites(desired) {
  const last = desired.findLastIndex(byte => byte !== 0);
  if (last + 1 >= desired.length) throw Error('The requested prefix needs a trailing zero');
  const ends = [last + 1];
  for (let end = last; end >= 0; end--) if (!desired[end]) ends.push(end);
  return ends.map(end => {
    const bytes = Buffer.from(desired.subarray(0, end).map(byte => byte || 0x42));
    decoder.decode(bytes);
    return {end, bytes};
  });
}

module.exports = {nulWrites};
