// Host-side batching and measurements for an already connected session.
const {performance} = require('node:perf_hooks');

function check(ok, message) { if (!ok) throw Error(message); }

function packPages(base, size, writes) {
  check(Number.isSafeInteger(base) && base % 4096 === 0 && Number.isInteger(size) &&
    size > 0 && size % 4096 === 0, 'Invalid page staging range');
  const bytes = Buffer.alloc(size), occupied = new Set();
  for (const item of writes) {
    const offset = item.address - base;
    check(Number.isInteger(offset) && offset % 4096 === 0 && offset >= 0 && offset < size &&
      Buffer.isBuffer(item.bytes) && item.bytes.length > 0 && item.bytes.length <= 4096,
      'Packet is outside its staging page');
    check(!occupied.has(offset), 'Two packets share a staging page');
    occupied.add(offset);
    item.bytes.copy(bytes, offset);
  }
  return bytes;
}

function install(s, live, bulk) {
  if (s.metrics) return s;
  const rpc = s.rpc;
  const counters = {requests: 0, elapsedMs: 0, methods: {}};
  for (const name of Object.keys(rpc).filter(name => typeof rpc[name] === 'function')) {
    const original = rpc[name].bind(rpc);
    rpc[name] = async (...args) => {
      const started = performance.now();
      counters.requests++;
      counters.methods[name] = (counters.methods[name] || 0) + 1;
      try { return await original(...args); }
      finally {
        counters.elapsedMs += performance.now() - started;
      }
    };
  }
  s.metrics = counters;
  s.measure = async (stage, work) => {
    const started = performance.now(), requests = counters.requests;
    let passed = false;
    try { const value = await work(); passed = true; return value; }
    finally { await s.record({operation: 'timing', stage, passed,
      elapsedMs: performance.now() - started,
      requests: counters.requests - requests}); }
  };

  // The caller must wait until every producer of these ranges is finished.
  s.readOwnedMany = async ranges => {
    check(Array.isArray(ranges) && ranges.length > 0 && ranges.length <= 64,
      'Expected 1..64 owned ranges');
    const outputs = [];
    for (const range of ranges) outputs.push(await s.readOwned(range.address, range.bytes));
    return outputs;
  };

  // The allocation was already checked by initialize and remains owned until
  // cleanup. Avoid querying the same mapping before every small hash/result.
  const originalRead = s.read.bind(s);
  s.read = (address, bytes) => {
    if (s.base && Number.isSafeInteger(address) && Number.isInteger(bytes) && bytes > 0 && bytes <= 524288 &&
        address >= s.base && address + bytes <= s.base + s.bytes)
      return s.readOwned(address, bytes);
    return originalRead(address, bytes);
  };

  // Keep scalar stores for small words; use the existing authenticated upload
  // channel for buffers. This changes transport only, not their destinations.
  const originalWrite = s.write.bind(s);
  s.uploadThreshold = 512;
  s.write = (address, bytes) => {
    if (s.bulkUploads && bytes.length >= s.uploadThreshold && s.base &&
        address >= s.base && address + bytes.length <= s.base + s.bytes)
      return bulk.write(s, address, bytes);
    return originalWrite(address, bytes);
  };
  return s;
}

module.exports = {install, packPages};
