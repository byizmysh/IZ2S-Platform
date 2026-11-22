/** Build a standalone CivSwitchFS image and its save installer. */

import {deflateRawSync, inflateRawSync} from "node:zlib";
import {mkdir, readFile, writeFile} from "node:fs/promises";
import path from "node:path";
import {fileURLToPath} from "node:url";
import esbuild from "esbuild";
import {buildPayload, ROOT} from "./bundle.mjs";

const PROFILE = "civ7-sdk-19f22ec5";
const MAX_LOADER_BYTES = 500_000;
const MAX_STAGE_BYTES = 200_000;
const BOOTSTRAP_ANCHOR = "            let setupFails = 0;";
const LOG_RELAY_ANCHOR = "        const _loader_old_log = window.log;";
const PERSIST_DIR = path.join(ROOT, "persist");

export async function buildPersistentImage(options = {}) {
    const debugPayload = options.debugPayload === true;
    const minifyPayload = options.minifyPayload === true;
    if (debugPayload && minifyPayload)
        throw new Error("Choose either --debug-payload or --minify-payload");
    const corePath = options.corePath || path.join(PERSIST_DIR, "core.js");
    const templatePath = options.templatePath || path.join(PERSIST_DIR, "save_template.bin");
    const decoderPath = options.decoderPath || path.join(PERSIST_DIR, "compress_stage.js");
    const outputDir = path.join(ROOT, "dist/persist");

    const [core, template, decoderSource, upstreamLicense, localLicense, packageJson] = await Promise.all([
        readFile(corePath, "utf8"),
        readFile(templatePath),
        readFile(decoderPath, "utf8"),
        readFile(path.join(PERSIST_DIR, "LICENSE"), "utf8"),
        readFile(path.join(ROOT, "LICENSE"), "utf8"),
        readFile(path.join(ROOT, "package.json"), "utf8")
    ]);
    if (!upstreamLicense.includes("Copyright (c) 2026 Gezine") ||
        !upstreamLicense.includes("Permission is hereby granted"))
        throw new Error("CivSwitch bootstrap license not recognized");

    const decoderMatch = decoderSource.match(/const DECODER = `([\s\S]*?)`;/);
    if (!decoderMatch) throw new Error("Stage decoder not found");

    const payload = await buildPayload({
        entry: options.chain ? path.join(ROOT, "src/chain.mjs")
            : options.split ? path.join(ROOT, "src/chain-sd.mjs")
            : undefined,
        debug: debugPayload,
        minifyIdentifiers: minifyPayload
    });
    const assembled = injectPayload(
        injectPersistentLogRelay(core), payload.payload);
    // Keep every outer lexical name visible to the direct eval in the payload.
    // Whitespace-only minification keeps the bootstrap's compatibility contract.
    const transformed = await esbuild.transform(assembled, {
        loader: "js",
        target: "es2020",
        minifyWhitespace: true,
        minifyIdentifiers: false,
        minifySyntax: false,
        charset: "ascii",
        legalComments: "none"
    });
    const standalone = transformed.code.trim() + "\n";
    const compressed = deflateRawSync(Buffer.from(standalone, "ascii"), {level: 9});
    if (inflateRawSync(compressed).toString("ascii") !== standalone)
        throw new Error("Stage compression round-trip failed");

    const decoder = decoderMatch[1];
    const encoded = compressed.toString("base64");
    const decodeExpression = "str(inflate(B64(" + JSON.stringify(encoded) + ")))";
    const stage =
        "/*civswitchfs profile=" + PROFILE + "*/\n" +
        licenseBlock("CivSwitchFS", localLicense) +
        licenseBlock("CivSwitch", upstreamLicense) +
        decoder + "\neval(" + decodeExpression + ");\n";
    const stageBytes = Buffer.from(stage, "ascii");
    if (!/^[\x00-\x7f]*$/.test(stage) || stageBytes.length > MAX_STAGE_BYTES)
        throw new Error(
            "Persistent image too large or contains non-ASCII characters");

    const image = {
        profile: PROFILE,
        version: "iz2sciv-0.3.0" +
            "+iz2s",
        stage,
        bytes: stageBytes.length,
        chain: options.chain === true,
        split: options.split === true,
        triggerTiming: false,
        hideTriggerText: !options.showText,
        debugPayload,
        minifyPayload
    };
    const sources = await Promise.all(
        ["save-codec", "save-fs", "installer"].map(name =>
            readFile(path.join(PERSIST_DIR, name + ".js"), "utf8"))
    );
    const persistCore = `var __cfsPersistCore = {
        sdk_base:sdk_base, read32:read32, read64:read64, read8:read8,
        write32:write32, write8:write8, read_buffer:read_buffer,
        write_buffer:write_buffer, malloc:malloc, free:free, call_rop:call_rop
    };`;
    const installerEntry = `
        var __cfsPersistMessage = '';
        window.__cfsPersistInstaller = window.__cfsCreateInstaller({
            openFS:function(){return window.__cfsCreateSaveFS(__cfsPersistCore);},
            exclusive:function(task){return task();},
            status:function(state){
                if(state.busy && state.message.indexOf('Failure:') !== 0 &&
                    state.message.indexOf('Installed.') !== 0 &&
                    state.message !== __cfsPersistMessage){
                    __cfsPersistMessage = state.message;
                    log(state.message);
                }
            }
        });
        window.__cfsPersistInstaller.offer(${JSON.stringify(image)});
        log('IZ2SCiv persistence installer ready.');
    `;
    const bootstrap = await esbuild.transform(
        sources.join("\n;\n") + "\n" + persistCore + "\n" + installerEntry,
        {
            loader: "js",
            target: "es2020",
            minifyWhitespace: true,
            minifyIdentifiers: false,
            minifySyntax: false,
            charset: "ascii",
            legalComments: "none"
        }
    );
    const bootstrapText = bootstrap.code.trim() + "\n";
    if (!/^[\x00-\x7f]*$/.test(bootstrapText) ||
        Buffer.byteLength(bootstrapText, "ascii") >= MAX_LOADER_BYTES)
        throw new Error(
            "Installer bootstrap too large or contains non-ASCII characters");

    const probe = decoder + "\nvar decoded = " + decodeExpression + ";\n" +
        "log('STAGE DECODE ' + (decoded.length === " + standalone.length +
        " && decoded.slice(0, 16) === " + JSON.stringify(standalone.slice(0, 16)) +
        " ? 'OK' : 'FAILED'));\n";
    const manifest = {
        profile: PROFILE,
        version: image.version,
        bytes: image.bytes,
        hideTriggerText: image.hideTriggerText,
        debugPayload: image.debugPayload,
        minifyPayload: image.minifyPayload
    };

    await mkdir(outputDir, {recursive: true});
    await Promise.all([
        writeFile(path.join(outputDir, "standalone.js"), stage, "ascii"),
        writeFile(path.join(outputDir, "persist-bootstrap.js"), bootstrapText, "ascii"),
        writeFile(path.join(outputDir, "save-template.bin"), template),
        writeFile(path.join(outputDir, "stage-probe.js"), probe, "ascii"),
        writeFile(path.join(outputDir, "LICENSE-CivSwitchFS.txt"), localLicense),
        writeFile(path.join(outputDir, "LICENSE-CivSwitch.txt"), upstreamLicense),
        writeFile(path.join(outputDir, "install-image.json"), JSON.stringify(manifest, null, 2) + "\n")
    ]);
    let payloadMode;
    if (image.debugPayload) {
        payloadMode = "diagnostic, not minified";
    } else if (image.minifyPayload) {
        payloadMode = "minified (shortened ESM names)";
    } else {
        payloadMode = "whitespace compacted, names preserved";
    }
    console.log(
        `Persistent image: ${image.bytes} bytes; bootstrap: ` +
        `${Buffer.byteLength(bootstrapText)} bytes; payload: ${payload.bytes}` +
        ` bytes (${payloadMode}).`
    );
    console.log("No installation was started.");
    return {image, standalone, bootstrap: bootstrapText, manifest};
}

function standaloneEntry(payload) {
    const payloadWithSourceName = payload +
        "\n//# sourceURL=IZ2SCiv-payload.js";
    return `
            {
                const __cfsStandalone = true;
                let sock_fd = null;
                const client_fd = null;
                const network_str = '';
                try {
                    await eval(${JSON.stringify(payloadWithSourceName)});
                    await log('Executed successfully');
                }
                catch (error) {
                    let position = '';
                    if (error && (error.lineNumber || error.columnNumber)) {
                        const lineNumber = error.lineNumber || '?';
                        const columnNumber = error.columnNumber || '?';
                        position = ' (line=' + lineNumber +
                            ', column=' + columnNumber + ')';
                    }
                    await log(
                        'IZ2SCiv: ' + error.message + position);
                    if (error && error.stack) await log(error.stack);
                }
                finally { if (sock_fd !== null) close(sock_fd); }
            }
`;
}

// Keep PC forwarding in front of the source logger and expose its clear API.
function injectPersistentLogRelay(core) {
    const relayStart = core.indexOf(LOG_RELAY_ANCHOR);
    if (relayStart < 0 ||
        core.indexOf(LOG_RELAY_ANCHOR, relayStart + 1) >= 0) {
        throw new Error(
            "CivSwitch bootstrap not recognized: " +
            "log relay anchor is ambiguous");
    }

    const relayEndMarker = "        };\n\n        function create_socket() {";
    const relayEnd = core.indexOf(relayEndMarker, relayStart);
    if (relayEnd < 0)
        throw new Error(
            "CivSwitch bootstrap not recognized: log relay end missing");

    const relay = `        globalThis.__CIVSWITCHFS_PERSIST_LOG_FORWARDER__ = function (message) {
            if (_log_socket_fd === null) return;
            try {
                const bytes = text_encode(String(message) + "\\n");
                let offset = 0;
                while (offset < bytes.length) {
                    const chunkEnd = Math.min(
                        offset + LOG_RELAY_CHUNK_BYTES, bytes.length);
                    while (offset < chunkEnd) {
                        const remaining = bytes.subarray(
                            offset, chunkEnd);
                        write_buffer(line_buf, remaining);
                        const sent = send(_log_socket_fd, line_buf,
                            remaining.length, 0);
                        if (sent <= 0n) return;
                        offset += Number(sent);
                    }
                }
            } catch (e) {}
        };

        const _loader_initial_log = window.log;
        let _loader_native_log = _loader_initial_log;
        let _loader_relay_log;
        _loader_relay_log = async function (message) {
            globalThis.__CIVSWITCHFS_PERSIST_LOG_FORWARDER__(message);

            let result;
            try {
                result = _loader_native_log(message);
            } finally {
                const installed_log = window.log;
                if (_loader_native_log === _loader_initial_log &&
                    typeof installed_log === "function" &&
                    installed_log !== _loader_relay_log) {
                    _loader_native_log = installed_log;
                }
                window.log = _loader_relay_log;
            }

            return result;
        };
        _loader_relay_log.clear = function () {
            const clearLog = _loader_native_log.clear;
            if (typeof clearLog !== "function") return Promise.resolve();
            return Reflect.apply(clearLog, _loader_native_log, []);
        };
        window.log = _loader_relay_log;

        function create_socket() {`;

    return core.slice(0, relayStart) + relay +
        core.slice(relayEnd + relayEndMarker.length);
}

function injectPayload(core, payload) {
    if (core.split(BOOTSTRAP_ANCHOR).length !== 2)
        throw new Error(
            "CivSwitch bootstrap not recognized: " +
            "payload anchor is ambiguous");
    return core.replace(BOOTSTRAP_ANCHOR, standaloneEntry(payload) + BOOTSTRAP_ANCHOR);
}

function licenseBlock(label, license) {
    return `/* ${label}\n${license.replaceAll("*/", "* /")}\n*/\n`;
}

function readOptions(args) {
    const options = {
        showText: false,
        debugPayload: false,
        // Readable build is the default: the minified chain stage fails to
        // eval in the save path ("Unexpected identifier", root cause open).
        minifyPayload: false,
        chain: false,
        split: false
    };
    for (let index = 0; index < args.length; index++) {
        const arg = args[index];
        if (arg === "--chain") {
            options.chain = true;
            continue;
        }
        if (arg === "--split") {
            options.split = true;
            continue;
        }
        if (arg === "--show-text") {
            options.showText = true;
            continue;
        }
        if (arg === "--debug-payload") {
            options.debugPayload = true;
            options.minifyPayload = false;
            continue;
        }
        if (arg === "--minify-payload") {
            // Opt-in: the minified chain stage fails to eval in the save
            // path ("Unexpected identifier", root cause open).
            options.minifyPayload = true;
            continue;
        }
        if (arg === "--no-minify-payload") {
            // Readable build is the default now; the flag stays accepted
            // as an explicit no-op.
            options.minifyPayload = false;
            continue;
        }
        if (["--core", "--template", "--decoder"].includes(arg)) {
            const value = args[++index];
            if (!value) throw new Error(`Missing value for ${arg}`);
            options[arg.slice(2) + "Path"] = path.resolve(value);
            continue;
        }
        throw new Error(`Unknown option: ${arg}`);
    }
    if (options.debugPayload && options.minifyPayload)
        throw new Error("Choose either --debug-payload or --minify-payload");
    return options;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        const options = readOptions(process.argv.slice(2));
        if (options.chain && options.split)
            throw new Error("Choose either --chain (embedded) or --split (SD-loaded)");
        await buildPersistentImage(options);
    } catch (error) {
        console.error("Persistence build failed: " + error.message);
        process.exitCode = 1;
    }
}
