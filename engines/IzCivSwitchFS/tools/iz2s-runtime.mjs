// Use the exact runtime shipped with RUN25's SB4 engine. Never stage kaiHEN.
import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";
import {createHash} from "node:crypto";

const runtime = path.resolve(path.dirname(fileURLToPath(import.meta.url)),
    "../../iz2s-core/runtime");
export const CORE_PACKAGE_PATH = "@Sdcard:/iz2s/core/bin/core/exefs.nsp";
export const RUNTIME_FILES = {
    bootstrap: ["bootstrap.bin", "/iz2s/core/bin/bootstrap.bin"],
    ro: ["ro/main", "/iz2s/core/bin/ro/main"],
    core: ["core/main", "/iz2s/core/bin/core/main"],
    game: ["game/main", "/iz2s/core/bin/game/main"],
    launcher: ["iz2s/izlauncher.iza", "/iz2s/izlauncher.iza"],
    resources: ["iz2s/izlauncher.romfs", "/iz2s/izlauncher.romfs"]
};

export function loadIz2sRuntime() {
    const records = JSON.parse(fs.readFileSync(path.join(runtime, "package-manifest.json")));
    function read(relative) {
        const record = records.find(item => item.path === relative);
        const bytes = fs.readFileSync(path.join(runtime, "package", relative));
        const sha256 = createHash("sha256").update(bytes).digest("hex");
        if (!record || record.bytes !== bytes.length || record.sha256 !== sha256)
            throw Error("IZ2S runtime integrity check failed: " + relative);
        return {bytes, sha256};
    }
    const manifest = {};
    for (const [name, [source, target]] of Object.entries(RUNTIME_FILES)) {
        const file = read(source);
        manifest[name] = {path: target, size: file.bytes.length, sha256: file.sha256};
    }
    const bootstrap = read("bootstrap.bin");
    if (!bootstrap.bytes.includes(Buffer.from("/iz2s/")) ||
        bootstrap.bytes.includes(Buffer.from("/kaihen/")))
        throw Error("Expected the IZ2S bootstrap, not kaiHEN");
    return {manifest, packagePath: CORE_PACKAGE_PATH};
}

export const iz2sRuntimePlugin = {
    name: "iz2s-runtime",
    setup(build) {
        build.onResolve({filter: /^iz2s:runtime-data$/}, () =>
            ({path: "runtime", namespace: "iz2s-runtime"}));
        build.onLoad({filter: /.*/, namespace: "iz2s-runtime"}, () => ({
            contents: "export default " + JSON.stringify(loadIz2sRuntime()) + ";",
            loader: "js"
        }));
    }
};
