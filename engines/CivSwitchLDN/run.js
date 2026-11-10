#!/usr/bin/env node
/**
 * Compress and inject over LDN, wait for the Remote JS Loader, then offer the installer.
 *
 * Usage: npm start -- [options]
 *   --keys <file>       Your prod.keys file (default: ./prod.keys)
 *   --phy <radio>       Wi-Fi radio (auto-detected if there is only one)
 *   --ifname <name>     Temporary LDN interface (default: civ7-ldn)
 *   --host <mac>        LDN host MAC when more than one network is found
 *   --switch-ip <ip>    Console's Wi-Fi IP; skips LAN discovery
 *   --chat-delay <ms>   Delay between chat messages in ms (default: 10)
 *   --handshake-scale <n> Multiply handshake delays by n (default: 1.0)
 *   --verbose           Show LDN packet and diagnostic logs
 *   --help              Show a short usage summary
 */
const { spawn } = require("child_process");
const fs = require("fs");
const net = require("net");
const path = require("path");
const readline = require("readline/promises");
const { setTimeout: sleep } = require("timers/promises");
const compress = require("./compress");
const { PORT, probeLoader, discoverLoader } = require("./loader");

const PAYLOAD_PATH = path.join(__dirname, "payloads", "core.js");
const PYTHON = path.join(__dirname, "ldn", ".venv", "bin", "python-ldn");
const INSTALLER_PYTHON = path.join(__dirname, "ldn", ".venv", "bin", "python");

async function main(args = process.argv.slice(2)) {
  const opts = parseArgs(args);
  if (opts.help) {
    console.log("Usage: npm start -- [--keys path] [--phy radio] [--ifname name] [--host ldn-host-mac] [--switch-ip console-ip] [--chat-delay ms] [--handshake-scale n] [--verbose]");
    return;
  }

  const prompt = readline.createInterface({ input: process.stdin, output: process.stdout });
  try {
    await injectViaLDN(opts);
    opts.switchIP = await waitForLoader(opts, prompt);
    console.log(`Remote JS Loader ready at ${opts.switchIP}:${PORT}`);

    const answer = await prompt.question("Run the persistent installer? [y/N] ");
    if (answer.trim().toLowerCase() === "y") {
      console.log("Installing the persistent loader...");
      await runCommand(INSTALLER_PYTHON, [
        path.join(__dirname, "installer", "install.py"), opts.switchIP, String(PORT), PAYLOAD_PATH,
      ]);
    }
  } finally {
    prompt.close();
  }
  console.log("Done.");
}

async function injectViaLDN(opts) {
  if (!fs.existsSync(opts.keys)) {
    throw new Error(`Place your own prod.keys, dumped from your Switch, at ${opts.keys}.`);
  }
  const phy = selectPhy(opts.phy);
  opts.phy = phy;
  console.log("Preparing the payload...");
  const lines = await compress(fs.readFileSync(PAYLOAD_PATH, "utf8"));
  console.log(`Payload ready (${lines.length} chat messages).`);
  const args = [
    path.join(__dirname, "bash", "network.sh"), "--phy", phy,
    PYTHON, path.join(__dirname, "ldn", "main.py"),
    "--keys", path.resolve(opts.keys), "--phy", phy, "--ifname", opts.ifname,
  ];
  if (opts.host) args.push("--host", opts.host);
  if (opts.chatDelay != null) args.push("--chat-delay", String(opts.chatDelay));
  if (opts.handshakeScale != null) args.push("--handshake-scale", String(opts.handshakeScale));
  if (opts.verbose) args.push("--verbose");
  await runCommand("bash", args, lines.join("\n") + "\n");
}

async function waitForLoader(opts, prompt) {
  const deadline = Date.now() + 40000;
  console.log(`Waiting for the Remote JS Loader on port ${PORT}...`);

  while (Date.now() < deadline) {
    if (opts.switchIP) {
      if (await probeLoader(opts.switchIP, Math.min(1500, deadline - Date.now()))) return opts.switchIP;
    } else {
      const matches = await discoverLoader(opts.phy, deadline);
      if (matches.length) return matches[0];
    }

    const remaining = deadline - Date.now();
    if (remaining > 0) await sleep(Math.min(500, remaining));
  }

  if (!opts.switchIP) {
    console.log("Automatic discovery timed out. Enter the console IP shown by the loader.");
    opts.switchIP = await askSwitchIP(prompt);
    return waitForLoader(opts, prompt);
  }
  throw new Error(`Remote JS Loader not found on port ${PORT}. Check the console and Wi-Fi connection.`);
}

async function askSwitchIP(prompt) {
  console.log("Wait for 'Awaiting payload' on the console. Use its Wi-Fi IP, after LDN has closed.");
  while (true) {
    const ip = (await prompt.question("Console IP: ")).trim();
    if (net.isIPv4(ip)) return ip;
    console.log("Enter a valid IPv4 address.");
  }
}

function parseArgs(args) {
  const opts = { keys: path.join(__dirname, "prod.keys"), ifname: "civ7-ldn" };
  const values = {
    "--keys": "keys", "--phy": "phy", "--ifname": "ifname",
    "--host": "host", "--switch-ip": "switchIP", "--chat-delay": "chatDelay", "--handshake-scale": "handshakeScale",
  };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (values[arg]) {
      if (!args[i + 1] || args[i + 1].startsWith("--")) {
        throw new Error(`Missing value for ${arg}`);
      }
      opts[values[arg]] = args[++i];
    } else if (arg === "--help" || arg === "-h") {
      opts.help = true;
    } else if (arg === "--verbose") {
      opts.verbose = true;
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  if (opts.switchIP && !net.isIPv4(opts.switchIP)) throw new Error("Invalid --switch-ip address.");
  return opts;
}

function selectPhy(phy) {
  if (phy) return phy;
  const directory = "/sys/class/ieee80211";
  if (!fs.existsSync(directory)) throw new Error("No Wi-Fi radio found. LDN requires Linux with direct Wi-Fi access.");
  const radios = fs.readdirSync(directory);
  if (radios.length !== 1) {
    throw new Error(`Available Wi-Fi radios: ${radios.join(", ") || "none"}. Select one with --phy.`);
  }
  return radios[0];
}

function runCommand(command, args, input) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, {
      cwd: __dirname,
      stdio: [input === undefined ? "inherit" : "pipe", "inherit", "inherit"],
    });
    // Keep Node alive until bash/network.sh has restored networking on interruption.
    const interrupt = () => child.kill("SIGTERM");
    process.on("SIGINT", interrupt);
    process.on("SIGTERM", interrupt);
    const cleanup = () => {
      process.off("SIGINT", interrupt);
      process.off("SIGTERM", interrupt);
    };
    child.once("error", (error) => { cleanup(); reject(error); });
    child.once("close", (code, signal) => {
      cleanup();
      if (code === 0) resolve();
      else reject(new Error(`${path.basename(command)} exited with ${signal || `code ${code}`}`));
    });
    if (input !== undefined) {
      child.stdin.on("error", (error) => {
        if (error.code !== "EPIPE") reject(error);
      });
      child.stdin.end(input);
    }
  });
}

if (require.main === module) {
  main().catch((err) => {
    console.error(`Error: ${err.message || err}`);
    process.exitCode = 1;
  });
}
