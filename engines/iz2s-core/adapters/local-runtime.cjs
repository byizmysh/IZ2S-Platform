'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const archive = require('../../../core/archive.cjs');
const {matchesRelease} = require('./releases.cjs');
const catalog = require('../../../core/services/app-catalog.cjs');
const instructions = require('../../../core/console/sd-launcher/instructions.cjs');
const sonicResident = require('../../../core/console/sd-launcher/instructions/izsonic-resident-releases.json');
const reader = require('../../../core/console/sd-launcher/reader/prepare.cjs');
const assets = require('../../../core/console/sd-launcher/assets.cjs');
const runtime = require('../runtime.cjs');

const localAudioDependencies = new Map([
  ['izdoom-native18', ['ims-gpu-pcm-cityfast', '>=0.1.1']],
  ['izdoom-wide18', ['ims-gpu-pcm-cityfast', '>=0.1.1']],
  ['izdoom-prime18', ['ims-gpu-pcm-cityfast', '>=0.1.1']],
  ['izdoom-audio-test', ['ims-gpu-pcm-cityfast', '>=0.1.1']],
  ['izsonic-transfer18', ['ims-music-packed-lab', '>=0.7.0']],
  ...Object.keys(sonicResident).map(id => [id,['ims-gpu-pcm-cityfast','>=0.1.1']]),
]);

function resolvesDependencies(descriptor, sha256, validatedFiles) {
  const dependencies = descriptor.dependencies ?? [];
  if (!dependencies.length) return true;
  const [dependency] = dependencies;
  const expected = localAudioDependencies.get(descriptor.id);
  return expected && matchesRelease(descriptor.id,sha256,validatedFiles) &&
    dependencies.length === 1 && dependency.kind === 'module' &&
    dependency.id === expected[0] && dependency.version === expected[1];
}

async function convertArchive(checked, {entry, prefix, files: switchFiles, manifest}) {
  if (manifest.native) return null;
  const {descriptor} = checked;
  if (!resolvesDependencies(descriptor, checked.sha256, checked.files)) return null;
  const instructionPolicy = instructions.eligibility(manifest, switchFiles);
  const policy = instructionPolicy ?? reader.eligibility(manifest, switchFiles);
  if (!policy) return null;
  assert(policy.compatible, policy.reason);

  const temporary = path.join(runtime.output(), 'tmp');
  await fs.mkdir(temporary, {recursive:true});
  const scratch = await fs.mkdtemp(path.join(temporary, 'local-runtime-'));
  try {
    const source = path.join(scratch, 'source');
    await archive.writeFiles(source, switchFiles);
    const original = await catalog.readApp(source, {entry: path.posix.basename(entry)});
    // A selected application upload includes the bundled book. Exporting the PC
    // library remains part of the explicit SD library installation.
    const adapted = instructionPolicy ? await instructions.prepare(original)
      : await reader.prepare(original, path.join(scratch, 'reader'), {includeLibrary:false});
    assert.equal((adapted?.localInstructions ?? adapted?.localReader)?.hostRequired, false);
    const standalone = {...adapted.manifest, native:{
      ...adapted.manifest.native, initial:adapted.manifest.native.code.replace(/\.bin$/, '.dat'),
    }};
    delete standalone.hostRuntime;
    delete standalone.localRuntime;

    const added = new Map([
      [prefix + standalone.native.code, adapted.files.nativeCode.bytes],
      [prefix + standalone.native.initial, adapted.files.nativeData.bytes],
    ]);
    for (const file of switchFiles)
      assert(!added.has(prefix + file.name), 'Application already contains a generated controller path');
    const originalPaths = new Set(switchFiles.map(file => file.name));
    const stagedAssets = new Map(adapted.sdAssets.map(asset => [asset.path, asset]));
    for (const asset of assets.declarations(standalone)) {
      if (originalPaths.has(asset.file)) continue;
      added.set(prefix + asset.file, await fs.readFile(stagedAssets.get(asset.path).file));
    }
    assets.inspectPacked(standalone, [
      ...switchFiles, ...[...added].map(([name, bytes]) => ({name:name.slice(prefix.length), bytes})),
    ]);
    const module = JSON.parse(checked.files.find(file => file.name === 'module.json').bytes);
    module.dependencies = [];
    module.switch.files.push(...added.keys());
    const replacements = new Map([
      [entry, Buffer.from(JSON.stringify(standalone, null, 2) + '\n')],
      ['module.json', Buffer.from(JSON.stringify(module, null, 2) + '\n')],
    ]);
    const files = checked.files.filter(file => file.name !== 'checksums.json')
      .map(file => ({name:file.name, bytes:replacements.get(file.name) ?? file.bytes}));
    for (const [name, bytes] of added) files.push({name, bytes});
    const checksums = {algorithm:'sha256', files:Object.fromEntries(
      files.map(file => [file.name, archive.sha(file.bytes)]),
    )};
    files.push({name:'checksums.json', bytes:Buffer.from(JSON.stringify(checksums, null, 2) + '\n')});
    const bytes = archive.encode(files);
    return {bytes, checked:archive.inspect(bytes)};
  } finally {
    await fs.rm(scratch, {recursive:true, force:true});
  }
}

module.exports = {convertArchive, resolvesDependencies};
