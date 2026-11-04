// Some acquisitions leave the known UAF pathname in unused cache entries.
// Reject every other nonzero tail; it could own real cached buffers.
function unusedTail(bytes) {
  if (bytes.length !== 0x5a0) return false;
  const tail = bytes.subarray(0x30);
  if (!tail.some(Boolean)) return true;
  let start;
  let exactEightBytePath = false;
  if (tail[0] === 0x41) start = 0;
  else if (tail[0] === 0 && tail[1] === 0x41) start = 1;
  else if (tail.subarray(0, 8).toString('hex') === '0000400100000000') {
    start = 8;
    exactEightBytePath = true;
  }
  else if (tail.subarray(0, 5).toString('hex') === '0000400100')
    start = tail[5] === 0 ? 6 : 5;
  else return false;
  const text = tail.subarray(start), end = text.indexOf(0);
  return end > 0 && (!exactEightBytePath || end === 552) &&
    text.subarray(0, end).every(b => b === 0x41 || b === 0x2f) &&
    text.subarray(0, end).includes(0x2f) && !text.subarray(end).some(Boolean);
}
module.exports = {unusedTail};
