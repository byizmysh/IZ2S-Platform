import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import esbuild from 'esbuild';
import policy from '../src/runtime-policy.cjs';
import {loadIz2sRuntime, RUNTIME_FILES} from './iz2s-runtime.mjs';

const runtime = loadIz2sRuntime();
const originals = new Map(Object.values(RUNTIME_FILES).map(([source, target]) =>
    [target, fs.readFileSync(new URL('../../iz2s-core/runtime/package/' + source, import.meta.url))]));
const plugin = {name: 'sd-device-fixture', setup(build) {
    build.onResolve({filter: /^\.\/(core\/runtime|kaihen\/sdcard)\.mjs$/}, args =>
        ({path: args.path, namespace: 'device'}));
    build.onLoad({filter: /.*/, namespace: 'device'}, args => ({contents:
        args.path.includes('runtime') ? 'export default {closeHandle:device.close};' : `
        export const openSdCard=()=>1;
        export const sdGetEntryType=(_c,_s,p)=>({transport:0,result:device.files.has(p)?0:1});
        export const sdOpenFile=(_c,_s,p)=>device.open(p);
        export const sdFileSize=(_c,h)=>BigInt(device.files.get(h).length);
        export const sdReadFileChunk=(_c,h,o,n)=>device.header(h,o,n);
        export const sdReadFile=(_c,_s,p)=>device.full(p);`
    }));
}};
const built = await esbuild.build({entryPoints: [new URL('../src/sd-verify.mjs', import.meta.url).pathname],
    bundle: true, write: false, format: 'cjs', platform: 'node', plugins: [plugin]});
function fixture() {
    const files = new Map(originals), open = new Set(), reads = [];
    const device = {files,
        open: p => {open.add(p); return p;}, close: p => open.delete(p),
        header: (p, o, n) => {reads.push({p, n, kind: 'header'}); return Array.from(files.get(p).subarray(o, o + n));},
        full: p => {reads.push({p, n: files.get(p).length, kind: 'full'}); return {bytes: Array.from(files.get(p))};}};
    const module = {exports: {}};
    vm.runInNewContext(built.outputFiles[0].text, {module, exports: module.exports, device});
    return {files, open, reads, verify: (manifest = runtime.manifest) => module.exports.verifySdRuntime({}, manifest)};
}

test('real SD files pass with stale mutable sizes; only 848 mutable header bytes are read', () => {
    const f = fixture(), old = structuredClone(runtime.manifest);
    for (const name of Object.keys(policy.HEADER_BYTES)) {old[name].size -= 4096; old[name].sha256 = '0'.repeat(64);}
    const result = f.verify(old);
    assert.equal(result.game, 'format'); assert.equal(result.core, 'sha256');
    assert.equal(f.open.size, 0);
    assert.equal(f.reads.filter(r => r.kind === 'header').reduce((sum, r) => sum + r.n, 0), 848);
    assert.deepEqual(f.reads.filter(r => r.kind === 'full').map(r => r.p),
        Object.keys(policy.FIXED_FILES).map(name => runtime.manifest[name].path));
});

for (const name of Object.keys(policy.FIXED_FILES)) {
    test(name + ': same-size corruption still fails SHA256', () => {
        const f = fixture(), p = runtime.manifest[name].path, damaged = Buffer.from(f.files.get(p));
        damaged[damaged.length - 1] ^= 1; f.files.set(p, damaged);
        assert.throws(() => f.verify(), /boot core SHA-256/); assert.equal(f.open.size, 0);
    });
    test(name + ': different size still requires a new save', () => {
        const f = fixture(), p = runtime.manifest[name].path;
        f.files.set(p, f.files.get(p).subarray(0, -1));
        assert.throws(() => f.verify(), /boot core size mismatch/); assert.equal(f.open.size, 0);
    });
}

for (const name of ['game', 'launcher', 'resources']) {
    test(name + ': missing or truncated file fails before launch, closing handles', () => {
        const f = fixture(), p = runtime.manifest[name].path;
        f.files.delete(p); assert.throws(() => f.verify(), /missing or inaccessible/);
        f.files.set(p, originals.get(p).subarray(0, -1));
        assert.throws(() => f.verify(), /invalid|unsupported/); assert.equal(f.open.size, 0);
    });
}

test('NSO with disabled hashes or overlapping segments is rejected', () => {
    const original = originals.get(runtime.manifest.game.path);
    for (const patch of [h => h.writeUInt32LE(7, 12), h => h.writeUInt32LE(256, 32)]) {
        const header = Buffer.from(original.subarray(0, 256)); patch(header);
        assert.throws(() => policy.validateMutable('game', header, original.length), /hashes|bounds/);
    }
});
test('another IZA app cannot substitute for the menu', () => {
    const original = originals.get(runtime.manifest.launcher.path), header = Buffer.from(original.subarray(0, 512));
    header[24] = 120;
    assert.throws(() => policy.validateMutable('launcher', header, original.length), /menu header/);
});
test('ROMFS table overflows and overlaps are rejected', () => {
    const original = originals.get(runtime.manifest.resources.path);
    for (const patch of [h => h.writeBigUInt64LE(1n << 63n, 8), h => h.writeBigUInt64LE(h.readBigUInt64LE(8), 24)]) {
        const header = Buffer.from(original.subarray(0, 80)); patch(header);
        assert.throws(() => policy.validateMutable('resources', header, original.length), /safe range|overlapping/);
    }
});
test('unknown components and incomplete or ambiguous boot anchors are rejected', () => {
    assert.throws(() => policy.validateMutable('other', [], 100), /invalid file size/);
    const records = Object.entries(policy.FIXED_FILES).map(([name, path]) =>
        ({path, bytes: runtime.manifest[name].size, sha256: runtime.manifest[name].sha256}));
    assert.equal(policy.compatibilityContract(records).policy, runtime.bootPolicy);
    assert.deepEqual(policy.compatibilityContract(records), policy.compatibilityContract([...records].reverse()));
    assert.throws(() => policy.compatibilityContract(records.slice(1)), /Invalid boot anchor/);
    assert.throws(() => policy.compatibilityContract([...records, records[0]]), /Invalid boot anchor/);
});
