// These are address candidates from saved native reads, not verified pointers.
// A reused block is weaker evidence than a change confined to its free-list word.
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

async function findNativeLinks(snapshots, allowReusedBlockHeads = false) {
  const reads = [];
  for (const snapshot of snapshots) {
    const bytes = await fs.readFile(snapshot.outputFile);
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), snapshot.outputSha256,
      'Saved native leak hash differs');
    reads.push({...snapshot, bytes});
  }
  const candidates = [];
  for (let index = 0; index < reads.length; index++) {
    const snapshot = reads[index], leaked = snapshot.bytes;
    const previous = reads[index - 1]?.bytes, next = reads[index + 1]?.bytes;
    for (let offset = 0x4000; offset + 0x4000 <= leaked.length; offset += 0x4000) {
      const value = leaked.readBigUInt64LE(offset);
      if (value < 0x100000000n || value >= (1n << 39n) || value % 0x4000n ||
          !(value & 0xffffffn)) continue;
      const tail = leaked.subarray(offset + 8, offset + 0x4000);
      const zeroTail = !leaked.subarray(offset + 8, offset + 64).some(byte => byte !== 0);
      const zeroAfter32 = !leaked.subarray(offset + 32, offset + 0x4000).some(byte => byte !== 0);
      const changedLink = !!previous && previous.length >= offset + 0x4000 &&
        previous.readBigUInt64LE(offset) !== value &&
        previous.subarray(offset + 8, offset + 0x4000).equals(tail);
      const reusedBlockHead = !!next && next.length >= offset + 0x4000 &&
        next.readBigUInt64LE(offset) !== value &&
        !next.subarray(offset + 8, offset + 0x4000).equals(tail);
      if (!zeroTail && !zeroAfter32 && !changedLink && !(allowReusedBlockHeads && reusedBlockHead)) continue;
      candidates.push({offset, target: Number(value), zeroTail, zeroAfter32, changedLink, reusedBlockHead,
        sourceFile: snapshot.outputFile, sourceSha256: snapshot.outputSha256,
        snapshot: snapshot.name || 'initial'});
    }
  }
  const strength = item => item.changedLink ? 3 : item.zeroTail ? 2 : item.zeroAfter32 ? 1 : 0;
  return candidates.sort((a, b) => strength(b) - strength(a) || a.target - b.target);
}

module.exports = {findNativeLinks};
