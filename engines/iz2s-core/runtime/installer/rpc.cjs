#!/usr/bin/env node
// Send one request to a live --rpc run. The bridge is local to this computer.
const fs = require('node:fs/promises');
const path = require('node:path');
const http = require('node:http');

async function request(run, body) {
  const {port} = JSON.parse(await fs.readFile(path.join(run, 'connection.json'), 'utf8'));
  if (!Number.isInteger(port) || port < 1 || port > 65535)
    throw Error('This run has no live RPC connection');
  return new Promise((resolve, reject) => {
    const req = http.request({host: '127.0.0.1', port, path: '/', method: 'POST',
      headers: {'content-type': 'application/json'}}, res => {
      const parts = [];
      res.on('data', part => parts.push(part));
      res.on('error', reject);
      res.on('end', () => {
        try {
          const raw = Buffer.concat(parts).toString('utf8');
          if (res.statusCode !== 200 && !raw)
            throw Error(`RPC failed: HTTP ${res.statusCode}`);
          const reply = JSON.parse(raw);
          if (res.statusCode !== 200) throw Error(reply.error || `RPC failed: HTTP ${res.statusCode}`);
          resolve(reply);
        } catch (error) {reject(error);}
      });
    });
    req.setTimeout(180000, () => req.destroy(Error('RPC timed out; the native request may have completed. Do not replay it.')));
    req.on('error', reject);
    req.end(JSON.stringify(body));
  });
}

async function main(args = process.argv.slice(2)) {
  if (args.length !== 2 || args[0] === '--help')
    throw Error('Usage: node rpc.cjs RUN-FOLDER status|JSON-REQUEST');
  const body = args[1] === 'status' ? {command: 'status'} : JSON.parse(args[1]);
  if (!body || Array.isArray(body) || typeof body !== 'object' || typeof body.command !== 'string')
    throw Error('Expected a JSON object with a command');
  console.log(JSON.stringify(await request(path.resolve(args[0]), body), null, 2));
}

module.exports = {request, main};
if (require.main === module) main().catch(error => {console.error(error.message); process.exitCode = 1;});
