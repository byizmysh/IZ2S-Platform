// Offline SD kit: no console, FTP, keys or previous launcher installation needed.
import fs from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";
import {createRequire} from "node:module";
import {randomUUID} from "node:crypto";
import {loadIz2sRuntime, RUNTIME_FILES} from "./iz2s-runtime.mjs";
import {buildSdApplications} from "./sd-apps.mjs";
import policy from "../src/runtime-policy.cjs";
const require = createRequire(import.meta.url);
const archive = require("../../../core/archive.cjs");
const engine = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const platform = path.resolve(engine, "../..");
const runtime = path.resolve(engine, "../iz2s-core/runtime");
export const FILENAME = "iz2sciv-core-sd.zip";

export async function buildSdPackage() {
    const {manifest} = loadIz2sRuntime();
    const provenance = JSON.parse(await fs.readFile(path.join(runtime, "provenance.json"), "utf8"));
    const profile = JSON.parse(await fs.readFile(path.join(platform, "distribution/profile.json"), "utf8"));
    const entries = [];
    async function add(source, target, expected) {
        const bytes = await fs.readFile(path.join(runtime, "package", source));
        if (bytes.length !== expected.size || archive.sha(bytes) !== expected.sha256)
            throw Error("IZ2S runtime integrity check failed: " + source);
        entries.push({name: target, bytes});
    }
    for (const [name, [source, target]] of Object.entries(RUNTIME_FILES))
        await add(source, target.slice(1), manifest[name]);
    const records = JSON.parse(await fs.readFile(path.join(runtime, "package-manifest.json"), "utf8"));
    // NRO dependencies are separate SD files, loaded only when a program asks
    // for them. Bind both the declared set and each file to the package hashes.
    const libraries = records.filter(row => row.path.startsWith("iz2s/lib/"));
    const declaredLibraries = provenance.launcher.sharedLibraries || [];
    if (JSON.stringify(libraries.map(row => row.path).sort()) !==
        JSON.stringify([...declaredLibraries].sort()))
        throw Error("NRO shared libraries do not match runtime provenance");
    for (const record of libraries) {
        if (!/^iz2s\/lib\/lib[a-zA-Z0-9+.-]+\.so\.[0-9]+$/.test(record.path))
            throw Error("Invalid NRO shared library path");
        await add(record.path, record.path, {...record, size: record.bytes});
    }
    const exefs = records.find(row => row.path === 'game/exefs.nsp');
    if (!exefs) throw Error('Missing game executable package');
    await add(exefs.path, 'iz2s/core/bin/' + exefs.path, {...exefs, size:exefs.bytes});
    // The native menu is embedded in game/main.
    for (const record of records.filter(row => row.path.startsWith("licenses/")))
        await add(record.path, "iz2s/" + record.path, {...record, size: record.bytes});
    if (provenance.launcher.splitRuntime) {
        const expected = provenance.launcher.splitRuntime;
        const record = records.find(row => row.path === "iz2s/modules/split-runtime.izdata");
        if (!record || expected.path !== record.path || expected.bytes !== record.bytes ||
            expected.sha256 !== record.sha256)
            throw Error("IZ2S split runtime does not match its package manifest");
        await add(record.path, record.path, {...record, size: record.bytes});
    }
    if (provenance.launcher.splitUiFont === "DejaVu Sans Bold")
        entries.push({name: "iz2s/licenses/DejaVu-Split-UI.txt",
            bytes: await fs.readFile(path.resolve(engine, "../../language/fonts/LICENSE.txt"))});
    const applications = await buildSdApplications();
    entries.push(...applications.entries);
    entries.push({name: "iz2s/README-IZ2SCiv.txt", bytes: Buffer.from(
        "Extract the contents of the ZIP to the root of the SD card. If the iz2s folder already exists, merge the folders\n" +
        "and replace matching files from the archive. Keep all other applications.\n" +
        "Included apps: IZ2S Cheese, IZPSX, Foldrift, and NX2 BreRUN.\n" +
        "Insert the SD card back into the powered-off console.\n" +
        `PC panel ${profile.version}; launcher ${provenance.launcher.version}, ${provenance.launcher.revision}. The native launcher is embedded in game/main; no standalone IZ2S.nro is required.\n` +
        "Install the SD-flex launch save once. Later compatible game/main, izlauncher.iza and izlauncher.romfs updates only need the SD files.\n" +
        "A change to bootstrap.bin, ro/main, core/main or the boot ABI still requires a matching launch save.\n" +
        (declaredLibraries.length ? "The launch save must match the installed RO version. Updating only the menu does not change the boot compatibility contract.\n" +
        "Shared NRO libraries are in iz2s/lib and load on demand. IZA Split remains on the existing embedded runtime.\n" : "") +
        "Copy a complete verified update set; the boot header checks do not replace the package SHA-256 inventory.\n")});
    const contents = entries.map(({name, bytes}) => ({path: name, bytes: bytes.length, sha256: archive.sha(bytes)}));
    entries.push({name: "iz2s/iz2sciv-sd-manifest.json", bytes: Buffer.from(JSON.stringify({
        name: "IZ2SCiv core", format: "iz2sciv.sd.v1", packageVariant: "four-apps",
        panelVersion: profile.version, buildId: profile.buildId, launcherVersion: provenance.launcher.version,
        bootCompatibility: policy.compatibilityContract(records),
        revision: provenance.launcher.revision, menuId: provenance.launcher.packagedMenuId,
        apps: applications.apps, files: contents
    }, null, 2) + "\n")});
    const bytes = archive.encode(entries);
    return {filename: FILENAME, bytes, sha256: archive.sha(bytes)};
}

export async function getSdPackage() {
    const inventory = JSON.parse(await fs.readFile(path.join(platform, "distribution/files.json"), "utf8"));
    const record = inventory.files[FILENAME];
    if (!record) return buildSdPackage();
    const bytes = await fs.readFile(path.join(platform, FILENAME));
    const sha256 = archive.sha(bytes);
    if (bytes.length !== record.bytes || sha256 !== record.sha256)
        throw Error("Bundled SD archive integrity check failed: " + FILENAME);
    return {filename: FILENAME, bytes, sha256};
}

export async function writeSdPackage() {
    const result = await buildSdPackage();
    const output = path.join(engine, "dist", result.filename);
    await fs.mkdir(path.dirname(output), {recursive: true});
    const temporary = output + "." + randomUUID();
    try { await fs.writeFile(temporary, result.bytes, {flag: "wx"}); await fs.rename(temporary, output); }
    finally { await fs.rm(temporary, {force: true}); }
    return {...result, path: output};
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    writeSdPackage().then(result => console.log(result.path + "\nSHA-256: " + result.sha256))
        .catch(error => { console.error(error.message); process.exitCode = 1; });
}
