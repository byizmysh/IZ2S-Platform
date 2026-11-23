'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const run25 = '${RUN25_ROOT}';
const archive = require(path.join(run25, 'core/archive.cjs'));
const installer = require(path.join(run25, 'engines/iz2s-core/tools/prepare-sd-app.cjs'));

async function main() {
    const report = JSON.parse(await fs.readFile(path.join(__dirname, 'package-verification.json')));
    const bytes = await fs.readFile(report.output);
    assert.equal(archive.sha(bytes), report.sha256);
    assert.equal(bytes.length, report.bytes);
    assert.deepEqual(archive.decode(bytes).map(entry => entry.name).sort(),
        [report.engine.file, 'launcher.iza', 'sd-bundle.json'].sort());
    const root = await fs.mkdtemp(path.join(__dirname, 'verify-'));
    try {
        const prepared = await installer.prepare(report.output, root);
        assert.equal(prepared.sha256, report.launcher.sha256);
        assert.equal(prepared.assets, 1);
        assert.equal(prepared.abi, 7);
        const engine = await fs.readFile(path.join(prepared.folder, report.engine.file));
        assert.equal(archive.sha(engine), report.engine.sha256);
        console.log(JSON.stringify({verified: true, id: prepared.id, abi: prepared.abi,
            packageSha256: report.sha256, engineSha256: report.engine.sha256, writesConsole: false}));
    } finally {
        await fs.rm(root, {recursive: true, force: true});
    }
}

main().catch(error => {console.error(error); process.exitCode = 1;});
