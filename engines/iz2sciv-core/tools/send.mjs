// Build and send the payload to a running CivSwitch Remote JS Loader.
//
// Usage:
//   node tools/send.mjs [--entry <file>] <console-ip> [port] [entry]
//   npm run send -- <console-ip> [port]
//   npm run example:user-storage -- <console-ip> [port]

import net from "node:net";
import {closeSync, mkdirSync, openSync, writeSync} from "node:fs";
import path from "node:path";
import {StringDecoder} from "node:string_decoder";
import {buildPayload, DEFAULT_ENTRY, ROOT} from "./bundle.mjs";

// Whole-payload receive timeout. A full acquisition→promotion run can take
// several minutes after the canary flips; raise via env for full-chain runs.
const TIMEOUT_MS = Number(process.env.CIVSWITCHFS_SEND_TIMEOUT_MS) || 240_000;

function formatLogTimestamp(date = new Date()) {
    const pad = (value, width = 2) => String(value).padStart(width, "0");
    return `[${pad(date.getHours())}:${pad(date.getMinutes())}:` +
        `${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}] `;
}

function writeFileFully(fileDescriptor, text) {
    const bytes = Buffer.from(text, "utf8");
    let offset = 0;

    while (offset < bytes.length) {
        const written = writeSync(fileDescriptor, bytes, offset,
            bytes.length - offset);
        if (written <= 0) {
            throw new Error("Could not write the log file.");
        }

        offset += written;
    }
}

function createTimestampedLogFile() {
    const logDirectory = path.join(ROOT, "logs");
    mkdirSync(logDirectory, {recursive: true});

    const timestamp = new Date().toISOString()
        .replace(/[-:]/g, "")
        .replace("T", "-")
        .replace(".", "-");

    for (let suffix = 0; ; suffix++) {
        let filename = timestamp + ".log";
        if (suffix > 0) {
            filename = timestamp + "-" + suffix + ".log";
        }

        const filePath = path.join(logDirectory, filename);
        try {
            return {fileDescriptor: openSync(filePath, "wx"), filePath};
        } catch (error) {
            if (error?.code !== "EEXIST") {
                throw error;
            }
        }
    }
}

function send(host, port, code, logFileDescriptor = null) {
    return new Promise(resolve => {
        const decoder = new StringDecoder("utf8");
        let pendingLine = "";
        let settled = false;
        let socket;
        let logFileError = null;

        function writeOutput(text) {
            if (logFileDescriptor === null) {
                process.stdout.write(text);
                return;
            }

            if (logFileError) {
                return;
            }

            try {
                writeFileFully(logFileDescriptor, text);
            } catch (error) {
                logFileError = error;
                process.stderr.write("log file write error: " +
                    String(error?.message || error) + "\n");
                process.exitCode = 1;
                socket?.destroy();
                finish();
            }
        }

        function writeTimestamped(text) {
            pendingLine += text;
            let newlineIndex;
            const timestampedLines = [];
            while ((newlineIndex = pendingLine.indexOf("\n")) >= 0) {
                const line = pendingLine.slice(0, newlineIndex + 1);
                timestampedLines.push(formatLogTimestamp() + line);
                pendingLine = pendingLine.slice(newlineIndex + 1);
            }
            if (timestampedLines.length) {
                writeOutput(timestampedLines.join(""));
            }
        }

        function finish() {
            if (settled) return;
            settled = true;
            writeTimestamped(decoder.end());
            if (pendingLine) {
                writeOutput(formatLogTimestamp() + pendingLine);
                pendingLine = "";
            }
            resolve(logFileError === null);
        }

        socket = net.createConnection({host, port}, () => {
            socket.write(code, () => socket.end());
        });
        socket.setTimeout(TIMEOUT_MS);
        socket.on("data", chunk => {
            writeTimestamped(decoder.write(chunk));
        });
        socket.on("end", finish);
        socket.on("timeout", () => {
            console.error("receive timeout");
            process.exitCode = 1;
            socket.destroy();
            finish();
        });
        socket.on("error", error => {
            console.error("connection error:", error.message);
            process.exitCode = 1;
            finish();
        });
        socket.on("close", finish);
    });
}

function usage() {
    return "Usage: npm run send -- [--save-logs] [--entry <file>] " +
        "<console-ip> [port] [entry]";
}

function parseArgs(args) {
    let entry = DEFAULT_ENTRY;
    let entryOptionProvided = false;
    let saveLogs = false;
    const positional = [];

    for (let index = 0; index < args.length; index++) {
        const arg = args[index];
        if (arg === "--save-logs") {
            saveLogs = true;
            continue;
        }

        if (arg === "--entry") {
            const value = args[++index];
            if (!value) {
                throw new Error("Missing value for --entry");
            }
            if (entryOptionProvided) {
                throw new Error("Use --entry only once");
            }
            entry = path.resolve(value);
            entryOptionProvided = true;
            continue;
        }

        if (arg.startsWith("--")) {
            throw new Error("Unknown option: " + arg);
        }

        positional.push(arg);
    }

    if (positional.length < 1) {
        throw new Error("Missing console IP");
    }
    if (positional.length > 3) {
        throw new Error("Too many positional arguments");
    }

    const host = positional[0];
    let port = 9026;
    let positionalEntry = null;

    if (positional[1] && /^\d+$/.test(positional[1])) {
        port = Number.parseInt(positional[1], 10);
        if (positional[2]) {
            positionalEntry = positional[2];
        }
    } else if (positional[1]) {
        if (positional[2]) {
            throw new Error("Unexpected positional argument");
        }
        positionalEntry = positional[1];
    }

    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        throw new Error("Invalid port");
    }
    if (entryOptionProvided && positionalEntry) {
        throw new Error("Use either --entry or the trailing entry path");
    }
    if (positionalEntry) {
        entry = path.resolve(positionalEntry);
    }

    return {entry, host, port, saveLogs};
}

let options;
try {
    options = parseArgs(process.argv.slice(2));
} catch (error) {
    console.error(String(error?.message || error));
    console.error(usage());
    process.exit(1);
}

const {entry, host, port, saveLogs} = options;
const built = await buildPayload({entry});
console.log(
    `-- sending ${path.relative(ROOT, entry)} to ${host}:${port} ` +
    `(${built.bytes} bytes) --`
);
if (built.identifiersMinified)
    console.log("-- whitespace compacted; ESM locals shortened; core/API names preserved --");
if (built.minified)
    console.log("-- ESM syntax minified further to fit the 500 KB loader limit --");
if (built.modules.length)
    console.log("-- bundled modules: " + built.modules.join(", ") + " --");

let logFile = null;
if (saveLogs) {
    logFile = createTimestampedLogFile();
}

let logsWritten = true;
try {
    logsWritten = await send(host, port, built.payload,
        logFile?.fileDescriptor ?? null);
} finally {
    if (logFile) closeSync(logFile.fileDescriptor);
}

if (logFile && logsWritten) {
    console.log("-- logs saved to " + path.relative(ROOT, logFile.filePath) +
        " --");
}
console.log("\n-- done --");
