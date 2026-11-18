const fs = require('node:fs/promises'), crypto = require('node:crypto');
const {Readable, Transform} = require('node:stream');
const {pipeline} = require('node:stream/promises');
function transient(error) {
  return error?.retryable === true || ['ECONNRESET','ECONNREFUSED','ETIMEDOUT','EAI_AGAIN',
    'ENETUNREACH','UND_ERR_SOCKET','UND_ERR_CONNECT_TIMEOUT','UND_ERR_HEADERS_TIMEOUT']
    .includes(error?.code || error?.cause?.code);
}
async function once(url, file, expected, {fetcher, maxBytes}) {
  let response;
  try { response = await fetcher(url, {signal: AbortSignal.timeout(300000)}); }
  catch (error) { error.retryable = true; throw error; }
  if (!response.ok || new URL(response.url || url).protocol !== 'https:') {
    await response.body?.cancel().catch(() => {});
    throw Object.assign(Error('Runtime download failed: HTTP ' + response.status),
      {retryable:[408,429,500,502,503,504].includes(response.status)});
  }
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
async function download(url, file, expected, {fetcher = fetch, maxBytes = 512 * 1024 * 1024,
  attempts = 3, sleep = ms => new Promise(resolve => setTimeout(resolve, ms)),
  onRetry = message => console.warn(message)} = {}) {
  if (!/^[a-f0-9]{64}$/.test(expected) || new URL(url).protocol !== 'https:') throw Error('HTTPS and a SHA-256 digest are required');
  if (!Number.isInteger(attempts) || attempts < 1 || attempts > 5) throw Error('Invalid download retry limit');
  for (let attempt = 1; ; attempt++) {
    try { return await once(url,file,expected,{fetcher,maxBytes}); }
    catch (error) {
      if (!transient(error) || attempt >= attempts) throw error;
      onRetry('Temporary download failure from ' + new URL(url).hostname + ': ' + error.message +
        '. Retrying (' + (attempt+1) + '/' + attempts + ')...');
      await sleep(attempt * 2000);
    }
  }
}
module.exports = {download};
