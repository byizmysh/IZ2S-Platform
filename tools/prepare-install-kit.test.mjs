import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {inflateRawSync} from 'node:zlib';
import {embeddedRuntime, verifyFiles} from './prepare-install-kit.mjs';
import {loadIz2sRuntime} from '../engines/IzCivSwitchFS/tools/iz2s-runtime.mjs';

const require = createRequire(import.meta.url);
const {parse} = require('../engines/CivSwitch/installer-creator/node_modules/acorn');
const archive = require('../core/archive.cjs');
const stage = await fs.readFile(new URL('../engines/IzCivSwitchFS/dist/persist/standalone.js', import.meta.url), 'utf8');
const standalone = inflateRawSync(Buffer.from(stage.match(/B64\("([A-Za-z0-9+/=]+)"\)/)[1], 'base64')).toString('ascii');
const entries = archive.decode(await fs.readFile(new URL('../engines/IzCivSwitchFS/dist/iz2sciv-core-sd.zip', import.meta.url)));
const runtime = loadIz2sRuntime();

test('actual launch image embeds exactly the SD archive runtime', () => {
  assert.deepEqual(embeddedRuntime(standalone, parse), runtime);
  assert.equal(verifyFiles(runtime, entries).files.length, entries.length - 1);
});
test('the previous installed main contract is rejected', () => {
  const stale = structuredClone(runtime);
  stale.manifest.game.size = 19399360;
  assert.notEqual(stale.manifest.game.size, runtime.manifest.game.size);
  assert.throws(() => verifyFiles(stale, entries), /SD\/save size mismatch: game/);
});
test('same-length main corruption is rejected against the save hash', () => {
  const damaged = entries.map(entry => {
    if (entry.name !== 'iz2s/core/bin/game/main') return entry;
    const bytes = Buffer.from(entry.bytes);
    bytes[bytes.length - 1] ^= 1;
    return {...entry, bytes};
  });
  assert.throws(() => verifyFiles(runtime, damaged), /SD\/save SHA256 mismatch: game/);
});
test('missing launcher resources stop package verification', () => {
  assert.throws(() => verifyFiles(runtime,
    entries.filter(entry => entry.name !== 'iz2s/izlauncher.romfs')), /Missing SD runtime file: resources/);
});
test('ambiguous embedded runtime cannot silently select the first copy', () => {
  const declaration = 'var runtime=' + JSON.stringify(runtime) + ';';
  assert.throws(() => embeddedRuntime(declaration + declaration, parse), /exactly one embedded runtime/);
});

test('actual installer writes the same save as the local export and preserves other saves', async () => {
  const read = name => fs.readFile(new URL('../engines/IzCivSwitchFS/persist/' + name, import.meta.url));
  const template = new Uint8Array(await read('save_template.bin'));
  const ctx = vm.createContext({window: {}, Uint8Array, DataView, setTimeout});
  vm.runInContext((await read('save-codec.js')).toString(), ctx);
  vm.runInContext((await read('installer.js')).toString(), ctx);
  const codec = ctx.window.__cfsSaveCodec;
  const canonical = 'save:/saves/Single/IZ2SCIV.Civ7Save';
  const personal = 'save:/saves/Single/personal.Civ7Save';
  const original = new Uint8Array([1, 2, 3]);
  const files = new Map([[canonical, template.slice()], [personal, original.slice()]]);
  let committed = false, disposed = false;
  const installer = ctx.window.__cfsCreateInstaller({
    exclusive: task => task(),
    openFS: () => ({
      exists: name => files.has(name),
      read: async name => { assert(files.has(name)); return files.get(name).slice(); },
      freeSpace: () => 32 * 1024 * 1024,
      writeNew: async (name, bytes) => { assert(!files.has(name)); files.set(name, bytes.slice()); },
      verify: async (name, expected) => assert.deepEqual(files.get(name), expected),
      mkdir() {}, delete: name => files.delete(name),
      rename: (a, b) => { assert(files.has(a)); files.set(b, files.get(a)); files.delete(a); },
      commit: () => { committed = true; return {ok: true}; },
      dispose: () => { disposed = true; }
    })
  });
  installer.offer({profile: 'civ7-sdk-19f22ec5', version: 'iz2sciv-0.3.2+iz2s',
    stage, bytes: stage.length, hideTriggerText: true, triggerTiming: false});
  const base64 = Buffer.from(template).toString('base64');
  for (let offset = 0, i = 1; offset < base64.length; offset += 400000, i++)
    installer.stageChunk('run33-install-test', i, offset, base64.slice(offset, offset + 400000));
  const result = await installer.installFromTemplate();
  assert.equal(result.readbackVerified, true);
  assert(committed && disposed);
  assert.deepEqual(Buffer.from(files.get(canonical)), Buffer.from(codec.replace(template, stage, false, true)));
  assert.deepEqual(files.get(personal), original);
});
