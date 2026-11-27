'use strict';
const {createHash} = require('node:crypto');
const releases = require('../../distribution/store-releases.json');

const ids = Object.freeze(Object.keys(releases));
const redirectHosts = new Set(['release-assets.githubusercontent.com', 'objects.githubusercontent.com']);
const release = id => Object.hasOwn(releases,id) ? releases[id] : undefined;

async function download(id, {request=fetch, signal=AbortSignal.timeout(90000)} = {}) {
  const row = release(id);
  if (!row) return null;
  const source = new URL(row.asset);
  const assetPath = /^\/byizmysh\/[a-z0-9_-]+\/releases\/download\/[a-z0-9._-]+\/([a-z0-9][a-z0-9.-]*\.(?:iza|nro))$/i;
  const name = assetPath.exec(source.pathname)?.[1];
  if (source.origin !== 'https://github.com' || source.username || source.password || !name ||
      !Number.isSafeInteger(row.bytes) || row.bytes <= 0 || row.bytes > 64 * 1024 * 1024 ||
      !/^[a-f0-9]{64}$/.test(row.sha256))
    throw Error('Invalid App Store release metadata.');

  let response = await request(source, {redirect:'manual',signal});
  if ([301,302,303,307,308].includes(response.status)) {
    const location = response.headers.get('location');
    await response.body?.cancel();
    const target = location && new URL(location, source);
    if (target?.protocol !== 'https:' || target.port || target.username || target.password ||
        !redirectHosts.has(target.hostname))
      throw Error('The repository returned an unexpected download address.');
    response = await request(target, {redirect:'error',signal});
  }
  const length = response.headers.get('content-length');
  if (!response.ok || !response.body || (length !== null && Number(length) !== row.bytes)) {
    await response.body?.cancel();
    throw Error('The repository did not return the expected release file.');
  }
  const bytes = Buffer.alloc(row.bytes);
  let offset = 0;
  for await (const chunk of response.body) {
    if (offset + chunk.length > bytes.length) throw Error('The repository download exceeded the recorded size.');
    bytes.set(chunk, offset);
    offset += chunk.length;
  }
  if (offset !== bytes.length || createHash('sha256').update(bytes).digest('hex') !== row.sha256)
    throw Error('The repository download failed its SHA-256 check.');
  return {name, bytes};
}

module.exports = {ids, release, download};
