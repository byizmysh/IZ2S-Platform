// Use the same verified, prebuilt applications as the panel's library exports.
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const require = createRequire(import.meta.url);
const archive = require('../../../core/archive.cjs');
const {directImage} = require('../../iz2s-core/sd-compat.cjs');
const storage = require('../../../core/module-storage.cjs');
const {moduleProgram} = require('../../iz2s-core/tools/nro-package.cjs');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const APP_IDS = Object.freeze(['iz2s-cheese', 'izpsx', 'iz-file-deck', 'nx2-brerun']);

export async function buildSdApplications(base = root) {
    const profile = JSON.parse(await fs.readFile(path.join(base, 'distribution/profile.json'), 'utf8'));
    const selected = new Map();
    for (const directory of profile.moduleDirectories) {
        if (!/^[A-Za-z0-9][A-Za-z0-9-]{0,63}$/.test(directory)) throw Error('Invalid bundled module directory');
        const home = path.join(base, 'modules', directory);
        const descriptor = JSON.parse(await fs.readFile(path.join(home, 'module.json'), 'utf8'));
        if (!APP_IDS.includes(descriptor.id)) continue;
        if (selected.has(descriptor.id)) throw Error('Duplicate SD application: ' + descriptor.id);
        const checked = archive.inspect(await fs.readFile(path.join(home, descriptor.id + '.module.zip')));
        if (checked.descriptor.id !== descriptor.id || checked.descriptor.version !== descriptor.version)
            throw Error('Bundled SD application identity differs: ' + descriptor.id);
        selected.set(descriptor.id, checked);
    }
    const entries = [], apps = [];
    for (const id of APP_IDS) {
        const checked = selected.get(id);
        if (!checked) throw Error('Bundled SD application is missing: ' + id);
        const prefix = storage.paths(id).path.slice(1) + '/';
        const version = checked.descriptor.version;
        for (const file of checked.files) {
            if (file.name.startsWith('licenses/') || /^(?:LICENSE|COPYING|COPYRIGHT|NOTICE|THIRD_PARTY|FONT-LICENSE)(?:[.-]|$)/i.test(file.name))
                entries.push({name:'iz2s/licenses/applications/' + id + '/' + file.name, bytes:file.bytes});
        }
        const image = directImage(checked.descriptor, checked.files);
        if (!image) {
            const program = await moduleProgram(checked, archive.switchApp(checked));
            if (!program) throw Error('Bundled SD application has no prebuilt launcher: ' + id);
            entries.push({name:prefix + 'launcher.nro', bytes:program});
            apps.push({id, version, path:prefix, format:'.nro',
                launcher:{bytes:program.length, sha256:archive.sha(program)}, assets:[]});
            continue;
        }
        const launcher = {bytes: image.bytes.length, sha256: archive.sha(image.bytes)};
        const assets = image.companions.map(({path, bytes, sha256}) => ({file:path, path, bytes, sha256}));
        entries.push({name:prefix + 'launcher.iza', bytes:image.bytes});
        for (const row of image.companions) entries.push({name:prefix + row.path, bytes:row.contents});
        entries.push({name:prefix + 'sd-bundle.json', bytes:Buffer.from(JSON.stringify({
            format:'iz2s.sd-bundle', version:1, id, launcher, assets
        }, null, 2) + '\n')});
        apps.push({id, version:checked.descriptor.version, path:prefix, launcher, assets});
    }
    return {entries, apps};
}
