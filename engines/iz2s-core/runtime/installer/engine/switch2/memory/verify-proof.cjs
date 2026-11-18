// Verify saved native bytes independently of the runner's success flags.
const fs = require('node:fs/promises');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

async function verifyProof(file, session) {
  const report = JSON.parse(await fs.readFile(file, 'utf8'));
  if (session) assert.equal(report.session, session, 'Proof belongs to another boot');
  const proof = report.cacheAddress;
  assert.ok(proof, 'No address-access proof was recorded');
  assert.ok(Number.isSafeInteger(proof.target) && proof.target >= 0x100000000 &&
    proof.target < 2 ** 39 && proof.target % 0x4000 === 0, 'Invalid proof address');
  assert.deepEqual(proof.readResults, [0, 0], 'Both native reads must succeed');
  assert.equal(proof.writeResult, 0, 'The native write must succeed');
  assert.equal(proof.restoreWriteResult, 0, 'The restoration write must succeed');
  assert.equal(proof.restoreReadResult, 0, 'The restoration read must succeed');

  async function read(name, digestName) {
    assert.equal(typeof proof[name], 'string', 'Missing saved bytes: ' + name);
    const bytes = await fs.readFile(proof[name]);
    assert.equal(bytes.length, 0x4000, 'Expected a complete proof block: ' + name);
    if (digestName) assert.equal(hash(bytes), proof[digestName], 'Saved hash differs: ' + name);
    return bytes;
  }
  const original = await read('outputFile', 'outputSha256');
  const repeated = await read('repeatOutputFile', 'repeatSha256');
  const ordinary = await read('controlFile');
  const changed = await read('writeOutputFile', 'writeOutputSha256');
  const restored = await read('restoreOutputFile', 'restoreSha256');
  assert.ok(original.equals(repeated), 'Reads with different output poisons differ');
  assert.ok(!original.equals(ordinary), 'The read returned ordinary file data');
  assert.ok(!original.every(byte => byte === 0xa5) && !original.every(byte => byte === 0x5a),
    'The original read is untouched output poison');

  assert.match(proof.writeMarker, /^[0-9a-f]{32}$/i, 'Expected exactly sixteen marker bytes');
  const marker = Buffer.from(proof.writeMarker, 'hex');
  const offset = proof.writeAddress - proof.target;
  assert.ok(Number.isSafeInteger(offset) && offset >= 0 && offset + marker.length <= original.length,
    'The write is outside the captured block');
  assert.ok(!original.subarray(offset, offset + marker.length).equals(marker), 'Marker did not change memory');
  const expected = Buffer.from(original);
  marker.copy(expected, offset);
  assert.ok(changed.equals(expected), 'Write changed the wrong bytes or neighboring memory');
  assert.ok(restored.equals(original), 'Restored block differs from its original bytes');
  return {session: report.session, report: file, target: '0x' + proof.target.toString(16),
    bytes: original.length, originalSha256: hash(original), changedSha256: hash(changed),
    restoredSha256: hash(restored), encoding: proof.encoding,
    readWriteRestoreVerified: true, restartReliabilityVerified: false};
}

module.exports = {verifyProof};
