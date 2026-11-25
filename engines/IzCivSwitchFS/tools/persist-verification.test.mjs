import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = name => fs.readFileSync(new URL('../persist/' + name + '.js', import.meta.url), 'utf8');
const template = new Uint8Array(fs.readFileSync(new URL('../persist/save_template.bin', import.meta.url)));
const canonical = 'save:/saves/Single/IZ2SCIV.Civ7Save';
const pending = 'save:/iz2sciv-install.tmp';
function context() {
    const ctx = vm.createContext({window: {}, Uint8Array, DataView, setTimeout});
    vm.runInContext(source('save-codec'), ctx);
    return ctx;
}

// Exercise the actual SDK adapter, including fingerprint checks, short reads,
// chunk boundaries and close-on-failure. The native calls are modelled on PC.
function adapter(data, mode) {
    const ctx = context();
    const code = source('save-fs');
    const profile = vm.runInNewContext('(' + code.match(/var profile = (\{[\s\S]*?\n    \});/)[1] + ')');
    const words = new Map();
    for (const [offset, values] of Object.values(profile))
        values.forEach((word, i) => words.set(offset + i * 4, word));
    const memory = new Uint8Array(128 * 1024), view = new DataView(memory.buffer);
    let next = 1024, closed = 0, maximumRead = 0;
    const c = {
        sdk_base: 0x1000000n,
        malloc: size => { const p = next; next += size; return BigInt(p); }, free() {},
        read32: p => BigInt(words.get(Number(p - c.sdk_base)) ?? 0),
        read64: p => view.getBigUint64(Number(p), true),
        write32: (p, v) => view.setUint32(Number(p), Number(v), true),
        write8: (p, v) => { memory[Number(p)] = Number(v); },
        write_buffer: (p, bytes) => memory.set(bytes, Number(p)),
        read_buffer: (p, count) => { maximumRead = Math.max(maximumRead, count); return memory.slice(Number(p), Number(p) + count); },
        call_rop: (address, ...args) => {
            const name = Object.entries(profile).find(([, item]) => item[0] === Number(address - c.sdk_base))[0];
            if (name === 'open') view.setBigUint64(Number(args[0]), 1n, true);
            else if (name === 'size') view.setBigUint64(Number(args[0]), BigInt(data.length), true);
            else if (name === 'close') closed++;
            else if (name === 'read') {
                const [result, , offset, buffer, count] = args.map(Number);
                if (mode === 'error') return 0x202n;
                memory.set(data.subarray(offset, offset + count), buffer);
                view.setBigUint64(result, BigInt(count - (mode === 'short' ? 1 : 0)), true);
            } else throw Error('Unexpected native call ' + name);
            return 0n;
        }
    };
    vm.runInContext(code, ctx);
    return {fs: ctx.window.__cfsCreateSaveFS(c), stats: () => ({closed, maximumRead})};
}

for (const mode of ['ok', 'corrupt', 'size', 'short', 'error']) {
    test('bounded native save readback: ' + mode, async () => {
        const expected = new Uint8Array(65536 * 2 + 13).fill(42);
        let data = expected.slice();
        if (mode === 'corrupt') data[65537] ^= 1;
        if (mode === 'size') data = data.slice(0, -1);
        const a = adapter(data, mode);
        if (mode === 'ok') await a.fs.verify(canonical, expected);
        else await assert.rejects(a.fs.verify(canonical, expected));
        assert.equal(a.stats().closed, 1);
        assert(a.stats().maximumRead <= 65536);
        a.fs.dispose();
    });
}

for (const mode of ['ok', 'staged-corruption', 'staged-short', 'post-commit-corruption', 'commit-error']) {
    test('installer reports success only after readback: ' + mode, async () => {
        const ctx = context();
        vm.runInContext(source('installer'), ctx);
        const files = new Map([[canonical, template.slice()]]), events = [], states = [];
        const backend = {
            exists: p => files.has(p),
            read: async p => { if (!files.has(p)) throw Error('missing'); return files.get(p).slice(); },
            freeSpace: () => 32 * 1024 * 1024,
            writeNew: async (p, bytes) => {
                let data = bytes.slice();
                if (mode === 'staged-corruption') data[65537] ^= 1;
                if (mode === 'staged-short') data = data.slice(0, -1);
                files.set(p, data); events.push('write');
            },
            verify: async (p, expected) => {
                events.push('verify:' + p);
                assert.deepEqual(files.get(p), expected, 'readback mismatch');
            },
            mkdir() {},
            delete: p => { events.push('delete:' + p); files.delete(p); },
            rename: (a, b) => { files.set(b, files.get(a)); files.delete(a); events.push('rename'); },
            commit: () => {
                events.push('commit');
                if (mode === 'post-commit-corruption') files.get(canonical)[100] ^= 1;
                return mode === 'commit-error' ? {ok: false, error: 'commit failed'} : {ok: true};
            },
            dispose: () => events.push('dispose')
        };
        const installer = ctx.window.__cfsCreateInstaller({openFS: () => backend,
            exclusive: task => task(), status: s => states.push({...s})});
        installer.offer({profile: 'civ7-sdk-19f22ec5', version: 'test', stage: '/*test*/', bytes: 8});
        // The installer uses the real pristine template and its SHA256 gate.
        const base64 = Buffer.from(template).toString('base64');
        for (let offset = 0, i = 1; offset < base64.length; offset += 400000, i++)
            installer.stageChunk('test', i, offset, base64.slice(offset, offset + 400000));
        if (mode === 'ok') {
            const result = await installer.installFromTemplate();
            assert.equal(result.readbackVerified, true);
            assert.equal(installer.description().installed, true);
            assert(events.indexOf('verify:' + pending) < events.indexOf('delete:' + canonical));
            assert(events.indexOf('verify:' + canonical) > events.indexOf('commit'));
        } else {
            await assert.rejects(installer.installFromTemplate());
            assert.equal(installer.description().installed, false);
            assert(!states.some(s => s.message.startsWith('Installed.')));
            assert(states.at(-1).message.startsWith('Failure:'));
            if (mode.startsWith('staged-')) {
                assert.deepEqual(files.get(canonical), template);
                assert(!events.includes('commit'));
                assert(!events.includes('delete:' + canonical));
            }
        }
        assert.equal(events.at(-1), 'dispose');
    });
}
