'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');

const run25 = '${RUN25_ROOT}';
const archive = require(path.join(run25, 'core/archive.cjs'));
const pkg = require(path.join(run25, 'core/console/sd-launcher/package.cjs'));
const assets = require(path.join(run25, 'core/console/sd-launcher/assets.cjs'));
const installer = require(path.join(run25, 'engines/iz2s-core/tools/prepare-sd-app.cjs'));
const parent = path.join(__dirname, '../R1122-ee-loop-smc-epoch-charge');

function encodeLauncher(before, original, tile, engine, nativeCode) {
    const binding = assets.binding([engine]);
    const header = Buffer.from(original.subarray(0, 512));
    header.fill(0, 72, 136);
    header.write('IZPS2 R1220', 72);
    header.writeUInt32LE(binding.count, 160);
    header.writeUInt32LE(binding.bytes, 164);
    Buffer.from(binding.sha256, 'hex').copy(header, 368);
    header.fill(0, 176, 368);
    const nativeData = Buffer.alloc(before.sections.nativeData.length);
    assert(engine.file.length < nativeData.length);
    nativeData.write(engine.file, 0);
    const sections = {...before.sections, nativeCode, nativeData, tile};
    const chunks = [header];
    let end = header.length;
    for (const [i, name] of pkg.SECTION.entries()) {
        const bytes = sections[name];
        if (!bytes.length) continue;
        const padding = (4 - end % 4) % 4;
        if (padding) chunks.push(Buffer.alloc(padding));
        end += padding;
        const row = 176 + i * 16;
        header.writeUInt32LE(end, row);
        header.writeUInt32LE(bytes.length, row + 4);
        header.writeUInt32LE(pkg.crc32(bytes), row + 8);
        end += bytes.length;
        chunks.push(bytes);
    }
    header.writeUInt32LE(end, 16);
    const bytes = Buffer.concat(chunks);
    assert.deepEqual(pkg.decode(bytes), {
        ...before, name: 'IZPS2 R1220', assetManifest: binding, sections,
    });
    return bytes;
}

async function main() {
    const original = archive.decode(await fs.readFile(path.join(parent, 'IZPS2-R1122-app.zip')))
        .find(entry => entry.name === 'launcher.iza').bytes;
    const before = pkg.decode(original);
    assert(before.version === 2 && before.nativeAbi === 7 && before.id === 'izps2');
    if (process.argv.includes('--prepare-tile')) {
        const cleanStage = JSON.parse(await fs.readFile(path.join(
            __dirname, '../R1022-ee-vector-snapshot-branches/stage.json')));
        const cleanArchive = await fs.readFile(cleanStage.sdArchive.path);
        assert.equal(archive.sha(cleanArchive), cleanStage.sdArchive.sha256);
        const clean = archive.decode(cleanArchive, {allowDirectories: true})
            .find(entry => entry.name === 'iz2s/modules/izps2/launcher.iza').bytes;
        const tile = Buffer.from(before.sections.tile);
        pkg.decode(clean).sections.tile.copy(tile, 65536, 65536, 102400);
        await fs.writeFile(path.join(__dirname, 'input.tile'), tile);
        return;
    }
    const engineBytes = await fs.readFile(path.join(__dirname, 'engine.izdata'));
    const exported = JSON.parse(await fs.readFile(path.join(__dirname, 'engine.izdata.json')));
    const digest = archive.sha(engineBytes);
    assert(exported.passed && exported.allAllocatedRelocationsChecked && exported.sha256 === digest);
    const assetName = 'asset-' + digest + '.izdata';
    const engine = {file: assetName, path: assetName, bytes: engineBytes.length, sha256: digest};
    const scratch = await fs.mkdtemp(path.join(__dirname, 'prepare-'));
    try {
        const tileOutput = path.join(__dirname, 'output.tile');
        const nativeCode = await fs.readFile(path.join(__dirname, 'frontend/frontend.bin'));
        assert(!nativeCode.equals(before.sections.nativeCode), 'Standalone and embedded menu code must differ');
        const launcher = encodeLauncher(before, original, await fs.readFile(tileOutput), engine, nativeCode);
        const manifest = {format: 'iz2s.sd-bundle', version: 1, id: before.id,
            launcher: {bytes: launcher.length, sha256: archive.sha(launcher)}, assets: [engine]};
        const entries = [
            {name: 'launcher.iza', bytes: launcher},
            {name: 'sd-bundle.json', bytes: Buffer.from(JSON.stringify(manifest) + '\n')},
            {name: assetName, bytes: engineBytes},
        ];
        const packed = archive.encode(entries);
        const input = path.join(scratch, 'IZPS2-R1220.iza');
        await fs.writeFile(input, packed);
        const prepared = await installer.prepare(input, scratch);
        assert.equal(prepared.id, 'izps2');
        assert.equal(prepared.abi, 7);
        assert.equal(prepared.assets, 1);
        assert.equal(prepared.sha256, manifest.launcher.sha256);
        const installed = await fs.readFile(path.join(prepared.folder, assetName));
        assert.equal(archive.sha(installed), digest);
        const portable = await installer.portableApp(prepared);
        assert(portable.packed);
        const identities = bytes => archive.decode(bytes).filter(entry => entry.name !== 'sd-bundle.json').map(entry => [
            entry.name, entry.bytes.length, archive.sha(entry.bytes),
        ]);
        assert.deepEqual(identities(portable.bytes), identities(packed));
        const output = path.join(__dirname, path.basename(input));
        await fs.writeFile(output, portable.bytes);
        const receipt = {output, bytes: portable.bytes.length, sha256: archive.sha(portable.bytes),
            format: 'RUN25 packed IZA', abi: prepared.abi, launcher: manifest.launcher, engine};
        await fs.writeFile(path.join(__dirname, 'package-verification.json'), JSON.stringify(receipt, null, 2) + '\n');
        console.log(JSON.stringify(receipt));
    } finally {
        await fs.rm(scratch, {force: true, recursive: true});
    }
}

main().catch(error => {console.error(error); process.exitCode = 1;});
