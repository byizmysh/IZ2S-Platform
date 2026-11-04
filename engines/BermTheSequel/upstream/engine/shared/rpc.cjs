const http = require('node:http');
const path = require('node:path');
const {port, addresses} = require('../connection.json');

function request(body) {
  return new Promise((resolve, reject) => {
    const req = http.request({host: '127.0.0.1', port, path: '/', method: 'POST', agent: false,
      headers: {'content-type': 'application/json'}}, res => {
      const parts = [];
      res.on('data', part => parts.push(part));
      res.on('error', reject);
      res.on('end', () => {
        try {
          const value = JSON.parse(Buffer.concat(parts));
          if (res.statusCode !== 200) throw Error(value.error || 'Native bridge failed');
          resolve(value);
        } catch (error) {reject(error);}
      });
    });
    req.setTimeout(180000, () => req.destroy(Error('Native request timed out; it was not replayed')));
    req.on('error', reject);
    req.end(JSON.stringify(body, (_, value) => typeof value === 'bigint' ? value.toString() : value));
  });
}

async function call(offset, args) {
  const reply = await request({command: 'call', offset, args});
  return {result: BigInt(reply.result), output: Buffer.from(reply.output, 'base64')};
}
async function pair(calls) {
  const reply = await request({command: 'pair', calls});
  return {results: reply.results.map(BigInt), output: Buffer.from(reply.output, 'base64')};
}
async function callAddress(address, args) {
  const reply = await request({command: 'call-address', address, args});
  return {result: BigInt(reply.result), output: Buffer.from(reply.output, 'base64')};
}
async function read(address, length) {
  if (!Number.isSafeInteger(address) || !Number.isInteger(length) || length < 0 || length > 0x4000000)
    throw Error('Invalid memory transfer');
  const parts = [];
  for (let offset = 0; offset < length; offset += 0x80000) {
    const size = Math.min(0x80000, length - offset);
    const bytes = Buffer.from((await request({command: 'read', address: address + offset, length: size})).data, 'base64');
    if (bytes.length !== size) throw Error('Memory read was truncated');
    parts.push(bytes);
  }
  return Buffer.concat(parts);
}
async function write(address, bytes) {
  if (!Number.isSafeInteger(address) || !Buffer.isBuffer(bytes) || bytes.length > 0x4000000)
    throw Error('Invalid memory transfer');
  for (let offset = 0; offset < bytes.length; offset += 0x80000)
    await request({command: 'write', address: address + offset,
      data: bytes.subarray(offset, offset + 0x80000).toString('base64')});
}
async function query(address) {
  const result = await request({command: 'query', address});
  return {...result, result: 0, address: Number(result.address), size: Number(result.size)};
}

module.exports = {request, call, callAddress, pair, read, write, query, a: addresses,
  output: addresses.scratch + 0x4040,
  directory: path.resolve(__dirname, '..', 'switch' + addresses.platform)};
