'use strict';
const fs = require('node:fs/promises');

function writer(file, now = Date.now) {
  let end = 0;
  let pending = Promise.resolve();
  return {
    append(text) {
      const data = Buffer.isBuffer(text) ? text : Buffer.from(text);
      if (!data.length) return pending;
      const at = now();
      pending = pending.then(async () => {
        const next = end + data.length;
        // Record the byte boundary first. Readers ignore entries past the
        // captured file size, including a process dying before its log write.
        await fs.appendFile(file + '.timings.jsonl', JSON.stringify([next, at]) + '\n').catch(() => {});
        await fs.appendFile(file, data, { flag: end ? 'a' : 'wx' });
        end = next;
      });
      pending.catch(() => {});
      return pending;
    },
    flush: () => pending,
  };
}

async function timings(file, size) {
  const contents = await fs.readFile(file + '.timings.jsonl', 'utf8').catch(error => {
    if (error.code === 'ENOENT') return '';
    throw error;
  });
  const stamps = [];
  let previous = 0;
  for (const line of contents.split('\n')) {
    if (!line) continue;
    let record;
    try { record = JSON.parse(line); }
    catch { break; }
    if (!Array.isArray(record) || record.length !== 2) break;
    const [end, at] = record;
    if (!Number.isSafeInteger(end) || end <= previous || end > size ||
        !Number.isSafeInteger(at) || at <= 0 || at > 8.64e15) break;
    stamps.push(record);
    previous = end;
  }
  return stamps;
}

module.exports = {writer,timings};
