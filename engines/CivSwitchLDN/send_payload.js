#!/usr/bin/env node
// Send a JS file to the CivSwitch Remote JS Loader (TCP port 9026).
//
// Usage: node send_payload.js <console-ip> [file.js] [port]
//   e.g. node send_payload.js 192.168.1.42 payloads/helloworld.js

const net = require("net");
const fs = require("fs");

const ip = process.argv[2];
const file = process.argv[3] || "payloads/helloworld.js";
const port = parseInt(process.argv[4] || "9026", 10);

if (!ip) {
  console.error("Usage: node send_payload.js <console-ip> [file.js] [port]");
  process.exit(1);
}

const payload = fs.readFileSync(file);
console.log(`Sending ${file} (${payload.length} bytes) to ${ip}:${port}`);

const client = net.createConnection({ host: ip, port }, () => {
  client.end(payload, () => {
    console.log("Sent. Waiting for response...");
  });
});

client.on("data", (data) => {
  process.stdout.write(data.toString());
});

client.on("end", () => {
  console.log("\nConnection closed.");
});

client.on("error", (err) => {
  console.error(`Connection failed: ${err.message}`);
  process.exit(1);
});
