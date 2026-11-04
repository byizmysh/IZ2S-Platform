const fs = require('node:fs/promises'), crypto = require('node:crypto');
const {Readable, Transform} = require('node:stream');
const {pipeline} = require('node:stream/promises');
async function download(url, file, expected, {fetcher = fetch, maxBytes = 512 * 1024 * 1024} = {}) {
  if (!/^[a-f0-9]{64}$/.test(expected) || new URL(url).protocol !== 'https:') throw Error('HTTPS and a SHA-256 digest are required');
  const response = await fetcher(url, {signal: AbortSignal.timeout(300000)});
  if (!response.ok || new URL(response.url || url).protocol !== 'https:') throw Error('Runtime download failed: HTTP ' + response.status);
  const digest = crypto.createHash('sha256'); let size = 0;
  const output = await fs.open(file, 'wx', 0o600); // EEXIST must never delete another file.
  try {
    await pipeline(Readable.fromWeb(response.body), new Transform({transform(bytes, encoding, done) {
      size += bytes.length;
      if (size > maxBytes) return done(Error('Runtime download exceeded its size limit'));
      digest.update(bytes); done(null, bytes);
    }}), output.createWriteStream());
    if (digest.digest('hex') !== expected) throw Error('Runtime checksum verification failed');
  } catch (error) {await output.close().catch(() => {}); await fs.rm(file, {force: true}); throw error;}
  return file;
}
module.exports = {download};
