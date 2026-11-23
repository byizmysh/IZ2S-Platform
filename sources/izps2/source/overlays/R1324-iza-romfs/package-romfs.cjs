'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const platform = path.resolve(process.env.IZ2S_ROOT);
const archive = require(path.join(platform, 'core/archive.cjs'));
const pkg = require(path.join(platform, 'core/console/sd-launcher/package.cjs'));
const assets = require(path.join(platform, 'core/console/sd-launcher/assets.cjs'));
const importer = require(path.join(platform, 'engines/iz2s-core/tools/prepare-sd-app.cjs'));
const name = 'izps2-emulator-0.5.0.iza';
const dir = __dirname;

async function main() {
    const linked = await fs.readFile(process.argv[2]);
    const exported = JSON.parse(await fs.readFile(path.join(dir, 'engine.izdata.json')));
    assert(exported.passed && exported.allAllocatedRelocationsChecked);
    const sha256 = archive.sha(linked);
    assert.equal(sha256, exported.sha256);
    const rom = await fs.readFile(path.join(dir, 'resources.romfs'));
    const resources = JSON.parse(await fs.readFile(path.join(dir, 'resource-manifest.json')));
    assert.equal(archive.sha(rom), resources.blobSha256);
    const romOffset = linked.indexOf(rom);
    assert(romOffset > 0 && linked.indexOf(rom, romOffset + 1) === -1);

    const reference = archive.decode(await fs.readFile(path.join(dir, 'reference-launcher.iza')));
    const original = reference.find(row => row.name === 'launcher.iza').bytes;
    const before = pkg.decode(original);
    assert.equal(before.id, 'izps2');
    assert.equal(before.name, 'IZPS2 emulator');
    assert.equal(before.nativeAbi, 7);
    const file = 'asset-' + sha256 + '.izdata';
    const engine = {file, path:file, bytes:linked.length, sha256};
    const binding = assets.binding([engine]);
    const launcher = Buffer.from(original);
    launcher.writeUInt32LE(binding.count, 160);
    launcher.writeUInt32LE(binding.bytes, 164);
    Buffer.from(binding.sha256, 'hex').copy(launcher, 368);
    const nativeData = Buffer.alloc(before.sections.nativeData.length);
    assert(file.length < nativeData.length);
    nativeData.write(file);
    const row = 176 + 16 * pkg.SECTION.indexOf('nativeData');
    assert.equal(launcher.readUInt32LE(row + 4), nativeData.length);
    nativeData.copy(launcher, launcher.readUInt32LE(row));
    launcher.writeUInt32LE(pkg.crc32(nativeData), row + 8);
    assert.deepEqual(pkg.decode(launcher), {...before, assetManifest:binding,
        sections:{...before.sections, nativeData}});

    const manifest = {format:'iz2s.sd-bundle', version:1, id:'izps2',
        launcher:{bytes:launcher.length, sha256:archive.sha(launcher)}, assets:[engine]};
    const bundle = archive.encode([
        {name:'launcher.iza', bytes:launcher},
        {name:'sd-bundle.json',bytes:Buffer.from(JSON.stringify(manifest) + '\n')},
        {name:file, bytes:linked},
    ]);
    const output = path.join(dir,name);
    await fs.writeFile(output,bundle);
    const scratch = await fs.mkdtemp(path.join(dir,'import-'));
    try {
        const prepared = await importer.prepare(output,scratch);
        assert.equal(prepared.id, 'izps2');
        assert.equal(prepared.assets, 1);
        assert.equal(prepared.abi, 7);
        assert.equal(prepared.sha256, manifest.launcher.sha256);
        assert.equal(archive.sha(await fs.readFile(path.join(prepared.folder,file))),sha256);
        assert.deepEqual((await fs.readdir(prepared.folder)).sort(), ['launcher.iza', 'sd-bundle.json', file].sort());
        const portable = await importer.portableApp(prepared);
        const byName = new Map(archive.decode(portable.bytes).map(entry => [entry.name,entry.bytes]));
        assert(byName.get('launcher.iza').equals(launcher));
        assert(byName.get(file).equals(linked));
        assert.deepEqual(JSON.parse(byName.get('sd-bundle.json')),manifest);
        await fs.writeFile(output,portable.bytes);
        const receipt = {output, bytes:portable.bytes.length, sha256:archive.sha(portable.bytes), engine,
            launcher:manifest.launcher, romOffset, resourceFiles:resources.files,
            resourceSha256:resources.blobSha256, cleanImport:true, separateResourceFilesRequired:false,
            stagedFiles:(await fs.readdir(prepared.folder)).sort(), deviceRunVerified:false};
        await fs.writeFile(path.join(dir,'package-verification.json'),JSON.stringify(receipt,null,2)+'\n');
        console.log(JSON.stringify(receipt));
    } finally { await fs.rm(scratch,{recursive:true,force:true}); }
}

main().catch(error=>{ console.error(error); process.exitCode = 1; });
