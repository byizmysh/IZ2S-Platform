// One entry point for IZ2SCiv setup and launch. Discovery confirms the loader
// protocol on 9026; an FTP connection is only an address hint.
import path from "node:path";
import net from "node:net";
import {fileURLToPath} from "node:url";
import {createRequire} from "node:module";
import {spawn} from "node:child_process";
const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function child(file, args = []) {
    return new Promise((resolve, reject) => {
        const shell = file.endsWith(".sh"), python = file.endsWith(".py");
        const task = spawn(shell ? "bash" : python ? require("../../../core/platform/python.cjs").command(path.resolve(ROOT,"../..")) : process.execPath,
            [path.join(ROOT, file), ...args], {cwd: ROOT, stdio: "inherit"});
        task.once("error", reject);
        task.once("exit", (code, signal) => code === 0 ? resolve() :
            reject(Error(file + " stopped: " + (signal || code))));
    });
}

export async function main(args = process.argv.slice(2)) {
    if (args.includes("--help") || args.includes("-h")) {
        console.log("IZ2SCiv core — Civilization VII entry for IZ2S\n" +
            "Usage: ./iz2sciv.sh [launch|install|setup|prepare-sd|build|check] [--host IPv4]\n" +
            "prepare-sd: build an offline ZIP to extract to the SD card root\n" +
            "launch: find Awaiting payload and start IZ2S using the existing /iz2s files\n" +
            "install: install the IZ2S chain into the Civ7 trigger save\n" +
            "setup: legacy alias; use the PC panel for initial Wi-Fi setup\n" +
            "build/check: local only, no console connection\n" +
            "After installation, restart Civ7 and open Load Game. After IZ2S starts,\n" +
            "close Civ7 and hold R while opening a Switch 2 game; repeat once if needed.");
        return;
    }
    const options = [...args];
    const mode = options[0] && !options[0].startsWith("--") ? options.shift() : "launch";
    let host;
    if (options[0] === "--host" && options.length === 2) host = options[1];
    else if (options.length) throw Error("Unknown arguments; use --help");
    if (host && !net.isIPv4(host)) throw Error("Expected a console IPv4 address");
    if (!["launch", "install", "setup", "prepare-sd", "build", "check"].includes(mode))
        throw Error("Unknown mode; use --help");
    if (mode === "check") return child("tools/check.mjs");
    if (mode === "build") {
        await child("tools/build.mjs");
        return child("tools/persist-build.mjs", ["--chain"]);
    }
    if (mode === "prepare-sd") {
        return child("tools/sd-package.mjs");
    }
    console.log("IZ2SCiv core — existing IZ2s at /iz2s/izlauncher.iza");
    if (mode === "setup") {
        if (process.platform !== "linux") throw Error("Initial LDN delivery requires native Linux. Windows can install the IZ2S save once Awaiting payload is open.");
        await child("start-wifi.sh");
    }
    const discovery = require("../../CivSwitch/runtime/discovery.cjs");
    if (host) {
        if (!await discovery.probe(host)) throw Error("CivSwitch loader not ready at " + host);
    } else {
        const ftp = await require("../../iz2s-core/tools/ftp-connect.cjs").credentials();
        console.log("Finding the CivSwitch loader on the local network…");
        const found = await discovery.discover({known: [ftp.host]});
        if (found.length !== 1) throw Error(found.length ?
            "Multiple consoles found; select one with --host: " + found.join(", ") :
            "No CivSwitch loader found. Open Civ7 → Load Game → Awaiting payload.");
        host = found[0];
    }
    console.log("Console: " + host);
    return mode === "launch" ? child("tools/send.mjs", [host]) :
        child("tools/persist-install.mjs", ["--chain", host]);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
    main().catch(error => { console.error(error.message); process.exitCode = 1; });
