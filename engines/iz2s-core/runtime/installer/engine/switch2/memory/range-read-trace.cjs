// Append-only host checkpoints for the first widened candidate read.
// The four 512 KiB transfers are the same bridge requests as bulk.read(2 MiB).
const CHUNK_BYTES = 0x80000;

async function readFirstCandidateRange({s, bulk, output, handle, size, offset, poison, u64}) {
  async function record(phase, state, extra = {}) {
    try {
      await s.record({operation: 'firstCandidateRangeRead', phase, state,
        readOffset: offset, readSize: size, ...extra});
    } catch (_) {
      // A diagnostic write must not replace a console or transfer error.
    }
  }
  async function stage(phase, work, extra = {}) {
    await record(phase, 'start', extra);
    try {
      const value = await work();
      await record(phase, 'end', extra);
      return value;
    } catch (error) {
      await record(phase, 'error', {...extra, error: String(error?.message ?? error)});
      throw error;
    }
  }

  await stage('memset', () => s.call('memset', output, poison, size));
  const reply = await stage('ipc', () => s.ipc('Read candidate content', handle, 0,
    Buffer.concat([u64(offset), u64(size)]),
    {receive: [{address: output, size, mode: 1}]}));
  if (reply.result) return {result: reply.canonicalResult, bytes: null};

  const bytes = await stage('readback', async () => {
    const parts = [];
    for (let at = 0; at < size; at += CHUNK_BYTES) {
      const length = Math.min(CHUNK_BYTES, size - at);
      parts.push(await stage('chunk', () => bulk.read(s, output + at, length),
        {chunkIndex: at / CHUNK_BYTES, chunkOffset: at, chunkBytes: length}));
    }
    return Buffer.concat(parts);
  });
  return {result: reply.canonicalResult, bytes};
}

module.exports = {readFirstCandidateRange, CHUNK_BYTES};
