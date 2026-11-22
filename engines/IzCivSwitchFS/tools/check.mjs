// Syntax-check every source file and verify the bundled payload size.

import {spawnSync} from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import {
    buildPayload,
    CORE_DIR,
    CORE_FILES,
    CORE_GLOBALS,
    coreBindingNames,
    MAX_PAYLOAD_BYTES,
    ROOT,
    SRC_DIR
} from "./bundle.mjs";

function walk(directory) {
    const files = [];
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
        const fullPath = path.join(directory, entry.name);
        if (entry.isDirectory()) {
            files.push(...walk(fullPath));
        } else if (entry.isFile() &&
            (entry.name.endsWith(".js") || entry.name.endsWith(".mjs"))) {
            files.push(fullPath);
        }
    }
    return files;
}

for (const file of CORE_FILES) {
    const fullPath = path.join(CORE_DIR, file);
    if (!fs.existsSync(fullPath))
        throw new Error(`missing core file: ${path.relative(SRC_DIR, fullPath)}`);
}

// Every coreRuntime property used by a payload module must exist in the
// injected core. This catches a removed core helper before it reaches the
// console.
const availableBindings = new Set([...coreBindingNames(), ...CORE_GLOBALS]);
const payloadFiles = [
    path.join(SRC_DIR, "core.js"),
    path.join(SRC_DIR, "core", "steps", "setup.mjs"),
    path.join(SRC_DIR, "core", "steps", "campaign.mjs"),
    path.join(SRC_DIR, "core", "steps", "alias.mjs"),
    path.join(SRC_DIR, "core", "steps", "discovery.mjs"),
    path.join(SRC_DIR, "core", "steps", "cleanup.mjs"),
    path.join(SRC_DIR, "core", "helpers", "index.mjs"),
    path.join(SRC_DIR, "core", "helpers", "util.mjs"),
    path.join(SRC_DIR, "core", "helpers", "scan.mjs"),
    path.join(SRC_DIR, "core", "helpers", "discovery.mjs"),
    path.join(SRC_DIR, "core", "helpers", "cache.mjs"),
    path.join(SRC_DIR, "core", "helpers", "wave.mjs"),
    path.join(SRC_DIR, "core", "promotion.mjs"),
    path.join(SRC_DIR, "core", "logger.mjs")
];
const usedBindings = new Set();
for (const file of payloadFiles) {
    const source = fs.readFileSync(file, "utf8");
    for (const match of source.matchAll(/coreRuntime\.([A-Za-z_$][\w$]*)/g))
        usedBindings.add(match[1]);
    for (const match of source.matchAll(
        /const\s*\{([\s\S]*?)\}\s*=\s*coreRuntime\s*;/g)) {
        for (const name of match[1].split(",")) {
            const trimmed = name.trim();
            if (/^[A-Za-z_$][\w$]*$/.test(trimmed))
                usedBindings.add(trimmed);
        }
    }
}
const missingBindings = [...usedBindings]
    .filter(name => !availableBindings.has(name))
    .sort();
if (missingBindings.length)
    throw new Error("payload uses missing core bindings: " +
        missingBindings.join(", "));

const sourceFiles = [
    ...walk(SRC_DIR),
    ...walk(path.join(ROOT, "persist")),
    ...walk(path.join(ROOT, "tools"))
];
for (const file of sourceFiles) {
    const result = spawnSync(process.execPath, ["--check", file], {
        encoding: "utf8"
    });
    if (result.status !== 0) {
        process.stderr.write(result.stderr || result.stdout || "");
        throw new Error(`syntax check failed: ${path.relative(process.cwd(), file)}`);
    }
}

const built = await buildPayload();
if (built.bytes > MAX_PAYLOAD_BYTES)
    throw new Error(`payload is too large: ${built.bytes} bytes`);

console.log(
    `OK: ${sourceFiles.length} source files; payload ${built.bytes} bytes` +
    (built.identifiersMinified ? " (ESM identifiers minified)" : "") +
    (built.minified ? " (full bundle minification for loader limit)" : "")
);
