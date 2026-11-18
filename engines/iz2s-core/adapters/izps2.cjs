'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const archive = require('../../../core/archive.cjs');
const assets = require('../../../core/console/sd-launcher/assets.cjs');
const pkg = require('../../../core/console/sd-launcher/package.cjs');
const {matchesRelease, releaseDigests} = require('./releases.cjs');

const id = 'izps2';
const adapter = 'sb4-izps2-v1';
const imageSha256 = 'ff550281e51830a9342fb9fe2aa608bcbb741bb1fc59abcc40a0487f4e40180c';
const versions = new Map([
  ['0.1.767', {
    pin: id,
    imageSha256,
    file: 'izps2-core.module.zip',
    sha256: 'aed0a798caadd6f06f6314d4f20378645a094064dcc4dac6b022d1bd182aab25',
  }],
  ['0.1.799', {
    pin: 'izps2@0.1.799',
    archiveBytes: 16698075,
    imageSha256: 'e186ad13e6aec258df8c338b564e87f323180dc97be2146a751067c3d5df3868',
    file: 'izps2-0.1.799-core.module.zip',
    sha256: '0b84d10338fd4d2bf66f45449d4d3f897bad0c9c0f7e1eeb66cecabf51fba373',
    image: 'izps2-0.1.799-core.izapp',
    convertedSha256: '8f62290b5869d09cf116011458f5723161b1cd7308001462c99bacdd3425b214',
  }],
]);

function legacyApp(app) {
  return app.id === id && versions.has(app.version) && app.native?.abi === 6 &&
    app.native.code === 'native/frontend.bin';
}

function matchesArchive(manifest, descriptor, sha256, validatedFiles) {
  return descriptor.id === id && descriptor.version === manifest.version && legacyApp(manifest) &&
    Boolean(matchesRelease(versions.get(manifest.version).pin, sha256, validatedFiles));
}

async function replacement(version) {
  const release = versions.get(version);
  const bytes = await fs.readFile(path.join(__dirname, 'izps2', release.file));
  assert.equal(archive.sha(bytes), release.sha256, 'IZPS2 adapter checksum differs');
  const checked = archive.inspect(bytes);
  const {manifest, files} = archive.switchApp(checked);
  assert.equal(checked.descriptor.id, id);
  assert.equal(checked.descriptor.version, manifest.version);
  assert.equal(manifest.native.abi, 7);
  assets.inspectPacked(manifest, files);
  return {adapter, bytes, checked, manifest, files: new Map(files.map(file => [file.name, file.bytes]))};
}

async function convertArchive(checked) {
  const {manifest} = archive.switchApp(checked);
  if (!matchesArchive(manifest, checked.descriptor, checked.sha256, checked.files)) return null;
  return replacement(manifest.version);
}

async function prepareSource(directory) {
  const appFile = path.join(directory, 'app.json');
  const app = JSON.parse(await fs.readFile(appFile, 'utf8'));
  assert(legacyApp(app), 'Unexpected IZPS2 archive manifest');
  const next = await replacement(app.version);
  const {native} = next.manifest;
  await fs.writeFile(path.join(directory, native.code), next.files.get(native.code));
  await fs.writeFile(path.join(directory, native.initial), next.files.get(native.initial), {flag: 'wx'});
  for (const entry of assets.declarations(next.manifest))
    await fs.writeFile(path.join(directory, entry.file), next.files.get(entry.file), {flag: 'wx'});
  app.native = native;
  app.version = next.manifest.version;
  app.description = next.manifest.description;
  await fs.writeFile(appFile, JSON.stringify(app, null, 2) + '\n');
}

async function convertImage(bytes, header, scratch) {
  if (header.id !== id || header.nativeAbi !== 6) return null;
  const digest = archive.sha(bytes);
  const match = [...versions].find(([, release]) => release.imageSha256 === digest);
  if (!match) return null;
  const [version, release] = match;
  const next = await replacement(version);
  const converted = release.image
    ? await fs.readFile(path.join(__dirname, 'izps2', release.image))
    : replaceImageSections(header, next);
  if (release.image)
    assert.equal(archive.sha(converted), release.convertedSha256, 'IZPS2 adapter image checksum differs');
  const image = pkg.decode(converted);
  assert.equal(image.id, id);
  assert.equal(image.nativeAbi, 7);
  const sources = [];
  await fs.mkdir(scratch);
  for (const item of assets.declarations(next.manifest)) {
    const file = path.join(scratch, item.path);
    await fs.writeFile(file, next.files.get(item.file), {flag: 'wx'});
    sources.push({...item, file});
  }
  assets.verifyBinding(image, sources);
  return {adapter, bytes: converted, sources};
}

function replaceImageSections(header, next) {
  const {native} = next.manifest;
  const {tile, pad, ...sections} = header.sections;
  const files = Object.fromEntries(Object.entries(sections).map(([name, bytes]) => [name, {bytes}]));
  assert.equal(header.nativeDataBytes, native.dataBytes);
  assert.equal(header.repeat, 0);
  files.nativeCode.bytes = next.files.get(native.code);
  files.nativeData.bytes = next.files.get(native.initial);
  files.memory.allocationBytes = header.privateBytes;
  return pkg.encode({id, name: header.name, files, manifest: {
    native, computeWorkgroups: header.groups,
  }}, {tile, pad, sb4: true});
}

async function offlineReleases() {
  const releases = [];
  for (const [version, release] of versions) {
    if (!release.archiveBytes) continue;
    const {bytes} = await replacement(version);
    releases.push({version, ...releaseDigests(release.pin), bytes,
      archiveBytes: release.archiveBytes, inputs: [release.imageSha256],
      convertedSha256: release.convertedSha256});
  }
  return releases;
}

module.exports = {adapter, matchesArchive, prepareSource, convertArchive, convertImage, imageSha256, offlineReleases};
