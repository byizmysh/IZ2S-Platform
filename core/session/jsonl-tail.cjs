const fs = require('node:fs/promises');

// Live status needs only a bounded tail. Full history exports use a separate read.
async function readJsonLinesTail(file, { limit = 250, maxBytes = 1024 * 1024 } = {}) {
  if (!Number.isInteger(limit) || limit < 1 || !Number.isInteger(maxBytes) || maxBytes < 1)
    throw new RangeError('Invalid JSONL tail limits');
  let handle;
  try {
    handle = await fs.open(file, 'r');
    const { size } = await handle.stat();
    let position = size, remaining = Math.min(size, maxBytes), entries = [];
    const chunks = [];
    while (remaining > 0) {
      const length = Math.min(64 * 1024, remaining);
      position -= length;
      const buffer = Buffer.alloc(length);
      let filled = 0;
      while (filled < length) {
        const { bytesRead } = await handle.read(buffer, filled, length - filled, position + filled);
        if (!bytesRead) break;
        filled += bytesRead;
      }
      chunks.unshift(buffer.subarray(0, filled));
      remaining -= length;
      const lines = Buffer.concat(chunks).toString('utf8').split('\n');
      // A read can begin inside a UTF-8 character or JSON record. Discard that
      // partial record; decode the combined chunks so inner boundaries are safe.
      if (position > 0) lines.shift();
      entries = lines.flatMap(line => {
        if (!line.trim()) return [];
        try { return [JSON.parse(line)]; } catch { return []; }
      });
      if (entries.length >= limit) break;
    }
    return entries.slice(-limit);
  } catch (error) {
    if (error.code === 'ENOENT') return [];
    throw error;
  } finally {
    await handle?.close();
  }
}

module.exports = { readJsonLinesTail };
