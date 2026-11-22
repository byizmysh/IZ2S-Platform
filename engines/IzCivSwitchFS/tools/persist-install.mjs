#!/usr/bin/env node
/** Install the built CivSwitchFS image into the Civ VII trigger save. */

import {createHash, randomUUID} from "node:crypto";
import {readFile} from "node:fs/promises";
import net from "node:net";
import path from "node:path";
import {fileURLToPath} from "node:url";
import {buildPersistentImage} from "./persist-build.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const BOOTSTRAP = path.join(ROOT, "dist/persist/persist-bootstrap.js");
const TEMPLATE = path.join(ROOT, "dist/persist/save-template.bin");
const TEMPLATE_SHA256 =
    "32fa8a0d6dd0a480d8e541e63ea44c4619bc6f7be4282cfc3cb6a4f6d305dfae";
const CHUNK_BYTES = 400_000;
const MAX_LOADER_BYTES = 500_000;

function sha256(bytes) {
    return createHash("sha256").update(bytes).digest("hex");
}

function send(host, port, code, timeoutMs = 60_000) {
    const payload = Buffer.isBuffer(code) ? code : Buffer.from(code, "ascii");
    if (!payload.length || payload.length >= MAX_LOADER_BYTES)
        return Promise.reject(new Error("Payload empty or over 500,000 bytes"));
    return new Promise((resolve, reject) => {
        const chunks = [];
        let settled = false;
        const socket = net.createConnection({host, port});
        const timeout = setTimeout(() => {
            finish(new Error("The loader did not finish the payload in time"));
            socket.destroy();
        }, timeoutMs);
        function finish(error, value) {
            if (settled) return;
            settled = true;
            clearTimeout(timeout);
            if (error) reject(error);
            else resolve(Buffer.concat(chunks).toString("utf8"));
        }
        socket.once("connect", () => socket.end(payload));
        socket.on("data", chunk => chunks.push(chunk));
        socket.once("end", () => finish(null));
        socket.once("error", error => finish(error));
        socket.once("close", hadError => {
            if (!settled && hadError) finish(new Error("Connexion au loader interrompue"));
        });
    });
}

function requireMarker(output, markers) {
    for (const line of output.split(/\r?\n/)) {
        if (line.startsWith("Installation failed: "))
            throw new Error(line.slice("Installation failed: ".length));
        if (markers.includes(line)) return line;
    }
    throw new Error("The loader did not acknowledge the operation: " + output.slice(-240));
}

async function runTask(host, port, expression, completion, markers, cleanup = false) {
    const dispose = cleanup
        ? "finally{window.__cfsPersistInstaller.dispose();delete window.__cfsPersistInstaller;}"
        : "";
    const code = "(async function(){try{await " + expression +
        ";await log(" + JSON.stringify(completion) + ");}" +
        "catch(error){await log('Installation failed: '+error.message);}" +
        dispose + "})()";
    return requireMarker(await send(host, port, code, 300_000), markers);
}

async function install(host, port, bootstrapPath, templatePath) {
    const [bootstrap, template] = await Promise.all([
        readFile(bootstrapPath),
        readFile(templatePath)
    ]);
    if (!bootstrap.length || bootstrap.length >= MAX_LOADER_BYTES ||
        bootstrap.some(byte => byte > 0x7f))
        throw new Error("Bootstrap invalide; relancez npm run persist:build");
    if (sha256(template) !== TEMPLATE_SHA256)
        throw new Error("Empreinte du template Civ VII invalide");

    const base64 = template.toString("base64");
    const token = randomUUID().replaceAll("-", "");
    console.log("Preparing the persistent image…");
    requireMarker(
        await send(host, port, bootstrap.toString("ascii") + '\nlog("Installer ready.");'),
        ["Installer ready."]
    );

    const count = Math.ceil(base64.length / CHUNK_BYTES);
    for (let offset = 0, index = 1; offset < base64.length; offset += CHUNK_BYTES, index++) {
        const part = base64.slice(offset, offset + CHUNK_BYTES);
        const marker = `Template ${index}/${count} received.`;
        const code = "window.__cfsPersistInstaller.stageChunk(" +
            JSON.stringify(token) + "," + index + "," + offset + "," +
            JSON.stringify(part) + ");log(" + JSON.stringify(marker) + ");";
        let acknowledged = false;
        for (let attempt = 0; attempt < 3; attempt++) {
            try {
                requireMarker(await send(host, port, code), [marker]);
                acknowledged = true;
                break;
            } catch (error) {
                if (attempt === 2) throw error;
            }
        }
        if (!acknowledged) throw new Error("Template chunk not acknowledged; installation cancelled");
        console.log(`Template ${index}/${count} received.`);
    }

    const installed = "Installed. Restart the game, then open Load Game menu.";
    const result = await runTask(
        host,
        port,
        "window.__cfsPersistInstaller.installFromTemplate()",
        installed,
        [installed],
        true
    );
    console.log(result);
}

function parseArgs(args) {
    const positional = [];
    const options = {
        port: 9026,
        bootstrap: BOOTSTRAP,
        template: TEMPLATE,
        rebuild: true,
        debugPayload: false,
        // Readable build is the default (minified save image fails to eval;
        // root cause open).
        minifyPayload: false,
        chain: false,
        split: false,
        payloadModeSpecified: false
    };
    for (let index = 0; index < args.length; index++) {
        const arg = args[index];
        if (arg === "--no-build") {
            options.rebuild = false;
            continue;
        }
        if (arg === "--debug-payload") {
            options.debugPayload = true;
            options.minifyPayload = false;
            options.payloadModeSpecified = true;
            continue;
        }
        if (arg === "--minify-payload") {
            // Opt-in: the minified chain stage fails to eval in the save
            // path ("Unexpected identifier", root cause open).
            options.minifyPayload = true;
            options.payloadModeSpecified = true;
            continue;
        }
        if (arg === "--no-minify-payload") {
            // Readable build is the default now; the flag stays accepted
            // as an explicit no-op.
            options.minifyPayload = false;
            continue;
        }
        if (arg === "--chain") {
            options.chain = true;
            continue;
        }
        if (arg === "--split") {
            // Split chain: the save carries only the promotion; the
            // IZ2S chain is embedded just like --chain.
            options.split = true;
            continue;
        }
        if (arg === "--port" || arg === "--bootstrap" || arg === "--template") {
            const value = args[++index];
            if (!value) throw new Error(`Missing value for ${arg}`);
            const key = arg.slice(2);
            options[key] = key === "port" ? Number(value) : path.resolve(value);
        } else if (arg.startsWith("--")) {
            throw new Error(`Unknown option: ${arg}`);
        } else positional.push(arg);
    }
    if (positional.length !== 1) {
        throw new Error("Usage: npm run persist:install -- <IP_SWITCH> " +
            "[--port 9026] [--no-build] [--debug-payload] [--chain|--split] " +
            "[--minify-payload] [--bootstrap path] [--template path]");
    }
    if (options.chain && options.split)
        throw new Error("Choose either --chain (embedded) or --split (SD-loaded)");
    if (!Number.isInteger(options.port) || options.port < 1 || options.port > 65535)
        throw new Error("Port invalide");
    if (options.debugPayload && options.minifyPayload)
        throw new Error("Choose either --debug-payload or --minify-payload");
    if (options.payloadModeSpecified && !options.rebuild)
        throw new Error("Payload mode requires a rebuild; remove --no-build");
    return {host: positional[0], ...options};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    try {
        const options = parseArgs(process.argv.slice(2));
        if (options.rebuild) {
            console.log("Rebuilding the persistent image…");
            await buildPersistentImage({
                debugPayload: options.debugPayload,
                minifyPayload: options.minifyPayload,
                chain: options.chain,
                split: options.split
            });
        }
        await install(options.host, options.port, options.bootstrap, options.template);
    } catch (error) {
        console.error("Installation failed: " + error.message);
        process.exitCode = 1;
    }
}
