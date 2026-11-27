#!/usr/bin/env node
// Build and verify the SD package and its matching launch save together.
// This is local packaging only. It never connects to or writes to a console.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {inflateRawSync} from 'node:zlib';
import {fileURLToPath, pathToFileURL} from 'node:url';

const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const identity = bytes => ({bytes: bytes.length, sha256: sha(bytes)});
const json = data => JSON.stringify(data, null, 2) + '\n';

function walk(node, visit) {
  if (!node || typeof node !== 'object') return;
  if (node.type) visit(node);
  for (const value of Object.values(node)) {
    if (Array.isArray(value)) value.forEach(item => walk(item, visit));
    else if (value && typeof value === 'object') walk(value, visit);
  }
}

function literal(node) {
  if (node.type === 'Literal') return node.value;
  if (node.type === 'TemplateLiteral' && !node.expressions.length) return node.quasis[0].value.cooked;
  assert.equal(node.type, 'ObjectExpression', 'Expected static build metadata');
  return Object.fromEntries(node.properties.map(property => {
    assert.equal(property.type, 'Property');
    assert.equal(property.computed, false);
    return [property.key.name ?? property.key.value, literal(property.value)];
  }));
}

// Read build metadata as syntax. Do not execute the launch code on the PC.
export function embeddedRuntime(standalone, parse) {
  const found = [], scripts = [standalone], seen = new Set();
  for (let i = 0; i < scripts.length; i++) {
    const source = scripts[i];
    if (seen.has(source)) continue;
    seen.add(source);
    walk(parse(source, {ecmaVersion: 'latest'}), node => {
      const nested = node.type === 'Literal' ? node.value :
        node.type === 'TemplateLiteral' && !node.expressions.length ? node.quasis[0].value.cooked : null;
      if (typeof nested === 'string' && nested.length > 1000 &&
          nested.includes('/iz2s/core/bin/game/main') && nested.includes('sha256')) scripts.push(nested);
      if (node.type === 'ObjectExpression' && node.properties.some(property =>
          (property.key?.name ?? property.key?.value) === 'manifest')) {
        const value = literal(node);
        if (value.manifest?.game?.path === '/iz2s/core/bin/game/main') found.push(value);
      }
    });
  }
  assert.equal(found.length, 1, 'Expected exactly one embedded runtime manifest');
  return found[0];
}

export function verifyFiles(runtime, entries) {
  const files = new Map(entries.map(entry => [entry.name, entry.bytes]));
  assert.equal(files.size, entries.length);
  for (const [name, record] of Object.entries(runtime.manifest)) {
    const bytes = files.get(record.path.slice(1));
    assert(bytes, 'Missing SD runtime file: ' + name);
    assert.equal(bytes.length, record.size, 'SD/save size mismatch: ' + name);
    assert.equal(sha(bytes), record.sha256, 'SD/save SHA256 mismatch: ' + name);
  }
  const metadata = JSON.parse(files.get('iz2s/iz2sciv-sd-manifest.json'));
  assert.equal(metadata.bootCompatibility?.policy, runtime.bootPolicy, 'SD/save boot policy mismatch');
  for (const [name, source] of Object.entries({bootstrap: 'bootstrap.bin', ro: 'ro/main', core: 'core/main'})) {
    const record = runtime.manifest[name];
    assert.deepEqual(metadata.bootCompatibility?.anchors?.[name],
      {path: source, bytes: record.size, sha256: record.sha256}, 'SD/save boot anchor mismatch: ' + name);
  }
  assert.equal(metadata.files.length + 1, files.size, 'Incomplete SD inventory');
  for (const record of metadata.files) {
    const bytes = files.get(record.path);
    assert(bytes, 'Missing SD file: ' + record.path);
    assert.deepEqual(identity(bytes), {bytes: record.bytes, sha256: record.sha256}, record.path);
  }
  assert(entries.every(entry => entry.name.startsWith('iz2s/')));
  assert(!entries.some(entry => entry.name.toLowerCase().endsWith('/iz2s.nro')));
  return metadata;
}

export async function prepare(panel, output) {
  const engine = path.join(panel, 'engines/IzCivSwitchFS');
  const require = createRequire(path.join(panel, 'package.json'));
  const {parse} = require(require.resolve('acorn', {
    paths: [path.join(panel, 'engines/CivSwitch/installer-creator')]
  }));
  const archive = require(path.join(panel, 'core/archive.cjs'));
  const importTool = name => import(pathToFileURL(path.join(engine, 'tools', name)).href);
  const {loadIz2sRuntime} = await importTool('iz2s-runtime.mjs');
  const expected = loadIz2sRuntime();
  const before = await fs.readFile(path.join(panel, 'engines/iz2s-core/runtime/package-manifest.json'));
  const {buildPersistentImage} = await importTool('persist-build.mjs');
  const built = await buildPersistentImage({chain: true});
  assert.equal(built.image.minifyPayload, false);
  assert.equal(built.image.bootRevision,
    JSON.parse(await fs.readFile(path.join(engine, 'boot-revision.json'), 'utf8')).revision);
  const {writeSdPackage} = await importTool('sd-package.mjs');
  const sd = await writeSdPackage();
  const entries = archive.decode(sd.bytes);
  assert.deepEqual(embeddedRuntime(built.standalone, parse), expected);
  const metadata = verifyFiles(expected, entries);

  const template = await fs.readFile(path.join(engine, 'persist/save_template.bin'));
  assert.equal(sha(template), '32fa8a0d6dd0a480d8e541e63ea44c4619bc6f7be4282cfc3cb6a4f6d305dfae');
  const context = vm.createContext({window: {}, Uint8Array, DataView});
  vm.runInContext(await fs.readFile(path.join(engine, 'persist/save-codec.js'), 'utf8'), context);
  const codec = context.window.__cfsSaveCodec;
  const save = Buffer.from(codec.replace(new Uint8Array(template), built.image.stage,
    built.image.triggerTiming, built.image.hideTriggerText));
  const record = codec.inspect(new Uint8Array(save));
  const hex = record.value.match(/XSSP:0:([a-f0-9]+)/)?.[1];
  assert(hex, 'Launch save has no embedded stage');
  const stage = Buffer.from(hex, 'hex').toString('ascii');
  assert.equal(stage, built.image.stage, 'Save stage round-trip failed');
  const encoded = stage.match(/B64\("([A-Za-z0-9+/=]+)"\)/)?.[1];
  assert(encoded, 'Launch stage has no compressed image');
  const unpacked = inflateRawSync(Buffer.from(encoded, 'base64')).toString('ascii');
  assert.equal(unpacked, built.standalone);
  assert.deepEqual(embeddedRuntime(unpacked, parse), expected);

  // Check the image offered by the actual panel installer, not just the exported save.
  const offered = [];
  walk(parse(built.bootstrap, {ecmaVersion: 'latest'}), node => {
    if (node.type === 'CallExpression' && node.callee?.property?.name === 'offer' &&
        node.arguments.length === 1 && node.arguments[0].type === 'ObjectExpression')
      offered.push(literal(node.arguments[0]));
  });
  assert.equal(offered.length, 1);
  assert.deepEqual(offered[0], built.image);
  assert.deepEqual(await fs.readFile(path.join(panel, 'engines/iz2s-core/runtime/package-manifest.json')),
    before, 'Runtime changed while preparing the installation');

  await fs.mkdir(output); // A new kit must not silently overwrite an older one.
  const files = {
    'IZ2S-RUN33-SD.zip': sd.bytes,
    'IZ2SCIV.Civ7Save': save,
    'INSTALL.txt': Buffer.from(
      'IZ2S RUN33 - ' + metadata.revision + '\n\n' +
      'Launch save revision: ' + built.image.bootRevision + '\n\n' +
      '1. Back up the existing iz2s folder before replacing files.\n' +
      '2. Extract IZ2S-RUN33-SD.zip to the SD card root and merge iz2s.\n' +
      '   Preserve Nintendo and other files not supplied by this archive.\n' +
      '3. Safely eject the SD card and insert it into the console.\n' +
      '4. Open the RUN33 panel from ' + panel + '/start.sh.\n' +
      '   Install the current launch save when the panel requests an update.\n' +
      '   Later compatible launcher/menu/ROMFS updates only require copying their SD files.\n' +
      '   Changes to the saved boot script, bootstrap.bin, ro/main, core/main or boot ABI\n' +
      '   require a new save. Copying SD files by FTP cannot update game savedata.\n' +
      '5. Follow the panel instructions for HOME, X and Load Game.\n\n' +
      'IZ2SCIV.Civ7Save is a separate export of the same generated launch save.\n' +
      'It is not an SD-root file. The panel installs it into the game save storage.\n' +
      'Do not copy it into Nintendo or delete your personal game saves.\n\n' +
      'Compatible IZA applications can use Split. NRO applications remain fullscreen.\n' +
      'See README-RUN33.md for the current controls.\n' +
      'The archive contains IZ2S Cheese, IZPSX, Foldrift and NX2 BreRUN with their licences.\n\n' +
      'Local archive and save compatibility have been checked. Actual Load Game,\n' +
      'R launch and simultaneous native app execution require console testing.\n')
  };
  const report = {
    format: 'iz2s.install-kit.v1', revision: metadata.revision, panel,
    saveBootRevision: built.image.bootRevision,
    runtimeFingerprint: sha(before), runtime: expected,
    saveCompatibilityFingerprint: sha(JSON.stringify(metadata.bootCompatibility)),
    bootCompatibility: metadata.bootCompatibility,
    files: Object.fromEntries(Object.entries(files).map(([name, bytes]) => [name, identity(bytes)])),
    verified: {sdInventory: metadata.files.length, saveStageRoundTrip: true,
      saveEmbeddedRuntime: true, panelInstallerSameImage: true, payloadNamesPreserved: true},
    consoleInstalled: false, consoleLaunchVerified: false
  };
  files['compatibility.json'] = Buffer.from(json(report));
  files['SHA256SUMS.txt'] = Buffer.from(Object.entries(files)
    .map(([name, bytes]) => sha(bytes) + '  ' + name + '\n').join(''));
  for (const [name, bytes] of Object.entries(files)) {
    await fs.writeFile(path.join(output, name), bytes, {flag: 'wx'});
    assert.deepEqual(await fs.readFile(path.join(output, name)), bytes);
  }
  console.log(json({output, ...report}));
  return report;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const [panel, output, ...extra] = process.argv.slice(2);
  assert(panel && output && !extra.length, 'Usage: prepare-install-kit.mjs PANEL NEW_OUTPUT_DIRECTORY');
  await prepare(path.resolve(panel), path.resolve(output));
}
