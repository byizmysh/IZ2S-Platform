// Build the loader payload: concatenate the classic core helpers, bundle the
// ESM payload entry, and expose the core bindings through the loader API.
//
// The remote loader evaluates one classic script. The core files in src/core
// are written as classic scripts so they can share the loader's global scope;
// the payload entry and its modules are ESM and are bundled by esbuild.

import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";
import esbuild from "esbuild";
import {iz2sRuntimePlugin} from "./iz2s-runtime.mjs";

export const ROOT = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)), "..");
export const SRC_DIR = path.join(ROOT, "src");
export const CORE_DIR = path.join(SRC_DIR, "core");
export const DEFAULT_ENTRY = path.join(SRC_DIR, "chain.mjs");
export const MAX_PAYLOAD_BYTES = 500_000;

// Loader globals that are always available to an injected payload.
export const CORE_GLOBALS = [
    "log", "read8", "read32", "read64",
    "write8", "write32", "write64", "call_rop",
    // Ambient loader-scope bindings (CivSwitch core.js eval scope): the heap
    // allocator pair, the module bases, and the sleep helper.
    "malloc", "free", "sdk_base", "main_base", "sleep"
];

// Dependency order matters: constants and low-level helpers must be declared
// before the modules that reference them.
export const CORE_FILES = [
    "offsets.js",
    "memory.js",
    "format.js",
    "time.js",
    "analysis/bytes.js",
    "analysis/cache-plan.js",
    "analysis/root-layout.js",
    "ipc/transport.js",
    "ipc/decode.js",
    "ipc/mount.js",
    "ipc/sessions.js",
    "ipc/domain.js",
    "ipc/filesystem.js",
    "ipc/storage.js",
    "rop.js",
    "threads.js",
    "ipc/scratch.js",
    "wave/reservation.js",
    "wave/orchestration.js",
    "wave/diagnostics.js"
];

export async function buildPayload({
    entry = DEFAULT_ENTRY,
    minify = false,
    debug = false,
    minifyIdentifiers = true
} = {}) {
    const entryPath = path.resolve(entry);
    const build = minifyBundle => esbuild.build({
        entryPoints: [entryPath],
        bundle: true,
        platform: "browser",
        format: "iife",
        globalName: "__CIVSWITCHFS_PAYLOAD__",
        target: ["es2020"],
        // ASCII-only output: non-ASCII string characters escape as
        // \uXXXX, so payloads survive byte-exact round trips through the
        // SD card (the split chain reads part 2 back byte-per-character).
        charset: "ascii",
        minifyWhitespace: !debug && minifyBundle,
        minifyIdentifiers: !debug && minifyIdentifiers,
        minifySyntax: !debug && minifyBundle,
        write: false,
        metafile: true,
        plugins: [iz2sRuntimePlugin],
        logLevel: "silent"
    });

    const bindings = [...new Set([
        ...coreBindingNames(),
        ...CORE_GLOBALS
    ])];
    const apiEntries = bindings
        .map(name => `${JSON.stringify(name)}: ${name}`)
        .join(",\n");

    let result = await build(minify);
    let payload = makePayload(result.outputFiles[0].text, apiEntries);
    if (!debug) payload = await minifyWhitespace(payload);
    let bundleMinified = minify && !debug;

    if (!debug && !bundleMinified &&
        Buffer.byteLength(payload, "utf8") > MAX_PAYLOAD_BYTES) {
        result = await build(true);
        payload = await minifyWhitespace(
            makePayload(result.outputFiles[0].text, apiEntries)
        );
        bundleMinified = true;
    }

    const bytes = Buffer.byteLength(payload, "utf8");
    if (bytes > MAX_PAYLOAD_BYTES)
        throw new Error(debug
            ? "unminified debug payload exceeds the 500 KB loader limit"
            : "bundled payload exceeds the 500 KB loader limit");

    const modules = Object.keys(result.metafile.inputs)
        .filter(file => !file.includes("node_modules"))
        .map(file => path.relative(ROOT, path.resolve(file)));

    return {
        payload,
        modules,
        minified: bundleMinified,
        identifiersMinified: !debug && minifyIdentifiers,
        whitespaceMinified: !debug,
        debugPayload: debug,
        bytes
    };
}

async function minifyWhitespace(source) {
    // Compact the complete loader program without changing its syntax or
    // identifiers. Identifier minification is confined to the ESM bundle.
    const result = await esbuild.transform(source, {
        loader: "js",
        minifyWhitespace: true,
        minifyIdentifiers: false,
        minifySyntax: false,
        legalComments: "inline"
    });
    return result.code;
}

export function loadCore() {
    return CORE_FILES
        .map(file => fs.readFileSync(path.join(CORE_DIR, file), "utf8"))
        .join("\n\n");
}

export function coreBindingNames() {
    const names = new Set();
    for (const file of CORE_FILES) {
        const source = fs.readFileSync(path.join(CORE_DIR, file), "utf8");
        // Core files use simple top-level declarations so their bindings can
        // be exposed to the ESM bundle without changing those legacy modules.
        const declaration =
            /^(?:async\s+)?function\s+([A-Za-z_$][\w$]*)|^(?:var|let|const)\s+([A-Za-z_$][\w$]*)/gm;
        for (const match of source.matchAll(declaration))
            names.add(match[1] || match[2]);
    }
    return [...names];
}

function wrapBundle(bundle, apiEntries) {
    return [
        `const __hadCoreApi = Object.prototype.hasOwnProperty.call(globalThis, "__CIVSWITCH_CORE_API__");`,
        `const __previousCoreApi = globalThis.__CIVSWITCH_CORE_API__;`,
        `globalThis.__CIVSWITCH_CORE_API__ = {\n${apiEntries}\n};`,
        "try {",
        bundle,
        "await __CIVSWITCHFS_PAYLOAD__.run();",
        "} finally {",
        `if (__hadCoreApi) globalThis.__CIVSWITCH_CORE_API__ = __previousCoreApi;`,
        `else delete globalThis.__CIVSWITCH_CORE_API__;`,
        "}"
    ].join("\n");
}

function makePayload(bundle, apiEntries) {
    return `(async () => {\ntry {\n${loadCore()}\n\n` +
        `${wrapBundle(bundle, apiEntries)}\n} catch (__e) {\n` +
        `await log("PAYLOAD ERROR: " + __e.message);\n` +
        `await log(__e.stack);\n}\n})()`;
}
