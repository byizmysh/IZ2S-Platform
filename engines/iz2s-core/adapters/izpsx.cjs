'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const archive = require('../../../core/archive.cjs');
const {matchesRelease, releaseDigests} = require('./releases.cjs');
const pkg = require('../../../core/console/sd-launcher/package.cjs');
const assets = require('../../../core/console/sd-launcher/assets.cjs');

const adapter = 'sb4-izpsx-v1';
const shim = 'native/shim.bin';
const versions = new Map([
  ['0.5.1',{
    pin:'izpsx',
    images:['4a7db1fa00f855b334e7f8177711ab861a617c7368154327452dc73dab3a0c52'],
    file:'izpsx-core.module.zip',
    digest:'a4daf3ab488c4cf3365c96b7323398dcd0be46f37e7f8a5f5d7441981b9a796c',
  }],
  ['0.5.2',{
    pin:'izpsx@0.5.2',
    images:[
      'ef1ba1999736450a1806067bc0617d187de9b1d43793f5f2b333818a2e676867',
      // SD export with the current controller normalization shader.
      'bad94f29fa99f3aadbb054fc527781ae20e716fc916d2952ef4e34fdd31fa8b9',
    ],
    file:'izpsx-0.5.2-core.module.zip',
    digest:'ad93b8b06e599d17260865a00f5f2905c1ec909a52ce93c232469364763aeb71',
  }],
]);

function matchesArchive(manifest, descriptor, sha256, validatedFiles) {
  const release = versions.get(manifest.version);
  return descriptor.id === 'izpsx' && manifest.id === 'izpsx' &&
    descriptor.version === manifest.version && release &&
    manifest.native?.abi === 5 && manifest.native.code === shim &&
    matchesRelease(release.pin,sha256,validatedFiles);
}

async function replacement(version) {
  const release = versions.get(version);
  assert(release, 'Unsupported IZPSX version');
  const bytes = await fs.readFile(path.join(__dirname, 'izpsx', release.file));
  assert.equal(archive.sha(bytes), release.digest, 'IZPSX adapter checksum differs');
  const checked = archive.inspect(bytes);
  assert.equal(checked.descriptor.id, 'izpsx');
  const files = new Map(checked.files.filter(file => file.name.startsWith('switch/'))
    .map(file => [file.name.slice(7), file.bytes]));
  const app = JSON.parse(files.get('app.json'));
  assert.equal(app.version, version);
  assert.equal(app.native.abi, 7);
  assert.equal(app.native.code, shim);
  assets.inspectPacked(app, [...files].map(([name, bytes]) => ({name, bytes})));
  return {bytes, checked, app, files};
}

async function convertArchive(checked) {
  const {manifest} = archive.switchApp(checked);
  if (!matchesArchive(manifest,checked.descriptor,checked.sha256,checked.files)) return null;
  return replacement(manifest.version);
}

async function prepareSource(directory) {
  const file = path.join(directory, 'app.json');
  const app = JSON.parse(await fs.readFile(file, 'utf8'));
  assert(app.id === 'izpsx' && app.native.abi === 5 && app.native.code === shim);
  const verified = await replacement(app.version);
  const declared = assets.declarations(verified.app);
  await fs.writeFile(path.join(directory, shim), verified.files.get(shim));
  for (const item of declared)
    await fs.writeFile(path.join(directory, item.file), verified.files.get(item.file), {flag: 'wx'});
  app.native = verified.app.native;
  delete app.hostRuntime;
  await fs.writeFile(file, JSON.stringify(app, null, 2) + '\n');
}

async function convertImage(bytes, header, scratch) {
  if (header.id !== 'izpsx' || header.nativeAbi !== 5) return null;
  const digest = archive.sha(bytes);
  const entry = [...versions].find(([,release])=>release.images.includes(digest));
  if (!entry) return null;
  const verified = await replacement(entry[0]);
  const {tile, pad, ...sections} = header.sections;
  const files = Object.fromEntries(Object.entries(sections).map(([name, bytes]) => [name, {bytes}]));
  files.memory.allocationBytes = header.privateBytes;
  files.nativeCode.bytes = verified.files.get(shim);
  assert.equal(header.nativeDataBytes, verified.app.native.dataBytes);
  assert.equal(header.repeat, 0);
  const converted = pkg.encode({id: header.id, name: header.name, files, manifest: {
    native: verified.app.native,
    computeWorkgroups: header.groups,
  }}, {tile, pad, sb4: true});
  const sources = [];
  await fs.mkdir(scratch);
  for (const item of assets.declarations(verified.app)) {
    const file = path.join(scratch, item.path);
    await fs.writeFile(file, verified.files.get(item.file), {flag: 'wx'});
    sources.push({...item, file});
  }
  assets.verifyBinding(pkg.decode(converted), sources);
  return {adapter, bytes: converted, sources};
}

async function offlineReleases() {
  const releases = [];
  for (const [version, release] of versions) {
    const {bytes} = await replacement(version);
    const {archiveSha256, checksumsSha256} = releaseDigests(release.pin);
    const inputs = [archiveSha256, ...release.images];
    releases.push({version, archiveSha256, checksumsSha256, inputs, bytes});
  }
  return releases;
}

module.exports = {adapter, matchesArchive, prepareSource, convertArchive, convertImage, offlineReleases};
