'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const [platform, candidate, previous] = process.argv.slice(2);
const archive = require(path.join(platform, 'core/archive.cjs'));
const pkg = require(path.join(platform, 'core/console/sd-launcher/package.cjs'));
const installer = require(path.join(platform, 'engines/iz2s-core/tools/prepare-sd-app.cjs'));
const release = path.dirname(__dirname);
const dist = path.join(release, 'dist');
const prefix = 'iz2s/modules/izps2/';
const title = 'IZPS2 emulator';
const revision = 'R1322';

const describe = bytes => ({bytes: bytes.length, sha256: archive.sha(bytes)});
const read = file => fs.readFile(file);
const json = async file => JSON.parse(await read(file));
const entry = (rows, name) => {
    const matches = rows.filter(row => row.name === name);
    assert.equal(matches.length, 1, name);
    return matches[0];
};

async function main() {
    const input = await read(path.join(candidate, 'IZPS2-R1322.iza'));
    const proof = await json(path.join(candidate, 'package-verification.json'));
    assert.equal(archive.sha(input), proof.sha256);
    const currentManifest = await json(path.join(previous, 'release-manifest.json'));
    const previousManifest = currentManifest.version === 'R1249' ? currentManifest :
        await json(path.join(previous, 'release-manifest-r1249.json'));
    assert.equal(previousManifest.version, 'R1249');
    const old = await read(path.join(previous, 'dist', 'izps2-emulator-r1249.iza'));
    const oldSD = await read(path.join(previous, 'dist', 'izps2-emulator-r1249-sd.zip'));
    for (const [file, bytes] of [['.iza', old], ['-sd.zip', oldSD]]) {
        const asset = previousManifest.assets.find(row => row.path === 'dist/izps2-emulator-r1249' + file);
        assert.equal(asset.sha256, archive.sha(bytes));
    }

    const entries = archive.decode(input);
    const engine = entry(entries, proof.engine.file);
    assert.equal(archive.sha(engine.bytes), proof.engine.sha256);
    const launcher = Buffer.from(entry(entries, 'launcher.iza').bytes);
    const before = pkg.decode(launcher);
    const oldLauncher = pkg.decode(entry(archive.decode(old), 'launcher.iza').bytes);
    assert.equal(before.id, 'izps2');
    assert.equal(before.name, 'IZPS2 ' + revision);
    assert.equal(oldLauncher.name, title);
    const {tile} = oldLauncher.sections;
    const icon = bytes => bytes.subarray(65536, 102400);
    assert(icon(tile).equals(icon(before.sections.tile)));
    launcher.fill(0, 72, 136);
    launcher.write(title, 72, 'ascii');
    const tileRow = 176 + pkg.SECTION.indexOf('tile') * 16;
    assert.equal(launcher.readUInt32LE(tileRow + 4), tile.length);
    tile.copy(launcher, launcher.readUInt32LE(tileRow));
    launcher.writeUInt32LE(pkg.crc32(tile), tileRow + 8);
    assert.deepEqual(pkg.decode(launcher), {...before, name: title, sections: {...before.sections, tile}});

    const bundle = JSON.parse(entry(entries, 'sd-bundle.json').bytes);
    bundle.launcher = describe(launcher);
    const files = [
        {name:'launcher.iza', bytes:launcher},
        {name:'sd-bundle.json', bytes:Buffer.from(JSON.stringify(bundle, null, 2) + '\n')},
        engine,
    ];
    const app = archive.encode(files);
    const appName = title + '.iza';
    const sdName = 'izps2-emulator-r1322-sd.zip';
    const target = path.join(dist, appName);
    await fs.writeFile(target, app);
    const oldFiles = archive.decode(oldSD);
    const resources = oldFiles.filter(row => row.name.startsWith(prefix+'resources/'));
    assert.equal(oldFiles.length - resources.length, 3);
    assert(resources.some(row => row.name === prefix + 'resources/shaders/vulkan/tfx.glsl'));
    const mounted = files.map(row => ({...row, name:prefix + row.name}));
    const sdFiles = [...mounted, ...resources];
    const sd = archive.encode(sdFiles);
    await fs.writeFile(path.join(dist, sdName), sd);

    const scratch = await fs.mkdtemp(path.join(release, 'qa', 'import-'));
    try {
        const verifyApp = async prepared => {
            assert.equal(prepared.id, 'izps2');
            assert.equal(prepared.abi, 7);
            assert.equal(prepared.assets, 1);
            assert.equal(prepared.sha256, bundle.launcher.sha256);
            assert.equal(pkg.decode(await read(path.join(prepared.folder, 'launcher.iza'))).name, title);
            assert.equal(archive.sha(await read(path.join(prepared.folder, engine.name))), proof.engine.sha256);
        };
        await verifyApp(await installer.prepare(target, scratch));
        await verifyApp(await installer.prepareBundle(mounted, scratch));
        const extracted = path.join(scratch, 'mounted-sd');
        await archive.writeFiles(extracted, archive.decode(sd));
        for (const row of sdFiles)
            assert.deepEqual(describe(await read(path.join(extracted, row.name))), describe(row.bytes));
    } finally {
        await fs.rm(scratch, {recursive:true, force:true});
    }
    await fs.writeFile(path.join(release,'qa','package-verification.json'),JSON.stringify({
        title,revision,appId:before.id,abi:before.nativeAbi, launcher:bundle.launcher,
        engine:proof.engine, nativeCodeUnchanged:true, graphicalResourcesReused:resources.length,
        offlineIzaImportVerified:true, offlineSdApplicationIdentityVerified:true,
        sdArchiveCheckedByExtraction:true,
        packages:{[appName]:describe(app),[sdName]:describe(sd)}
    }, null, 2) + '\n');
}

main().catch(error => { console.error(error); process.exitCode = 1; });
