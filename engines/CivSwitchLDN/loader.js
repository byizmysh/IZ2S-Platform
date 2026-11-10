// Find and verify the Remote JS Loader on the restored Wi-Fi network.
const fs = require("fs");
const net = require("net");
const os = require("os");
const path = require("path");

const PORT = 9026;
const PROBE_TIMEOUT = 1500;
const SCAN_CONCURRENCY = 128;

async function discoverLoader(phy, deadline = Infinity) {
  const candidates = localCandidates(phy);
  let next = 0;
  let done = false;

  return new Promise((resolve) => {
    const tick = async () => {
      while (next < candidates.length && Date.now() < deadline && !done) {
        const ip = candidates[next++];
        if (await probeLoader(ip, Math.min(PROBE_TIMEOUT, deadline - Date.now()))) {
          done = true;
          return resolve([ip]);
        }
      }
      if (!done && ++finished === total) resolve([]);
    };
    const total = Math.min(SCAN_CONCURRENCY, candidates.length);
    if (!total) return resolve([]);
    let finished = 0;
    for (let i = 0; i < total; i++) tick();
  });
}

function probeLoader(ip, timeout = PROBE_TIMEOUT) {
  if (timeout <= 0) return Promise.resolve(false);
  return new Promise((resolve) => {
    let response = "";
    let settled = false;
    const sock = net.createConnection({ host: ip, port: PORT });
    const finish = (found) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      sock.destroy();
      resolve(found);
    };
    // Bound the entire probe, including peers that keep sending unrelated data.
    const timer = setTimeout(() => finish(false), timeout);
    sock.once("connect", () => sock.end("1"));
    sock.on("data", (chunk) => {
      response += chunk;
      if (response.includes("Executed successfully")) finish(true);
      else if (response.length > 4096) finish(false);
    });
    sock.once("error", () => finish(false));
    sock.once("close", () => finish(false));
  });
}

function localCandidates(phy) {
  const targets = new Set();
  for (const [name, addresses] of Object.entries(os.networkInterfaces())) {
    let radio;
    try {
      radio = path.basename(fs.realpathSync(`/sys/class/net/${name}/phy80211`));
    } catch {
      continue;
    }
    if (radio !== phy) continue;

    for (const address of addresses) {
      if (address.family !== "IPv4" || address.internal || !address.netmask) continue;
      const mask = addressNumber(address.netmask);
      const count = (~mask >>> 0) + 1;
      if (count < 4 || count > 512) continue;

      const network = addressNumber(address.address) & mask;
      for (let offset = 1; offset < count - 1; offset++) {
        const value = (network + offset) >>> 0;
        const ip = [24, 16, 8, 0].map((shift) => (value >>> shift) & 255).join(".");
        if (ip !== address.address) targets.add(ip);
      }
    }
  }
  return [...targets];
}

function addressNumber(ip) {
  return ip.split(".").reduce((value, part) => (value << 8) | Number(part), 0) >>> 0;
}

module.exports = { PORT, probeLoader, discoverLoader };
