'use strict';
// Builds and tests a launcher-only candidate; never changes the installed package.
const fs = require('node:fs/promises'), path = require('node:path');
const assert = require('node:assert/strict'), crypto = require('node:crypto');
const Module = require('node:module');
const ROOT = path.resolve(__dirname, '..'), W = path.join(ROOT, 'data/ps2-cold-work');
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
async function main() {
  const out = path.join(ROOT, 'data/build-cache/ps2-startup-transport-' + Date.now());
  await fs.mkdir(out);
  const originalFile = path.join(W, 'build/r324-os-JIT-package-source-v1/module/server/loader-native-batch-v1.cjs');
  const original = await fs.readFile(originalFile, 'utf8');
  const old = "await rpc.write(base,input);pending=true;native.uncertain=true;history.requests++;const reply=await rpc.callAddress(destination,[base,0,0,0]);";
  const replacement = "pending=true;native.uncertain=true;history.requests++;const reply=typeof rpc.writeCall==='function'?await rpc.writeCall(base,input,destination,[base,0,0,0]):await (async()=>{await rpc.write(base,input);return rpc.callAddress(destination,[base,0,0,0]);})();";
  assert.equal(original.split(old).length, 2, 'Qualified baseline changed; inspect before adapting');
  const candidate = original.replace(old, replacement);
  const candidateFile = path.join(out, 'loader-native-batch-v1.cjs');
  await fs.writeFile(candidateFile, candidate, {flag: 'wx'});
  function load(text, name) {
    const loaded = new Module(path.join(out, name), module);
    loaded.filename = path.join(out, name);
    loaded.paths = Module._nodeModulePaths(out);
    loaded.require = id => id === 'node:fs/promises' ? {...fs, mkdtemp: () => fs.mkdtemp(path.join(out, 'helper-'))} : require(id);
    loaded._compile(text, loaded.filename);
    return loaded.exports;
  }
  const sources = {baseline: load(original, 'baseline.cjs'), candidate: load(candidate, 'candidate.cjs')};
  async function world(source, mode) {
    let next = 0x20000000, input, raw, dispatched = 0, transportRequests = 0;
    const native = {session: 'launcher-localModel', thread: null, uncertain: false, allocations: [], mappings: [], evidence: {images: []}};
    const pid = 207, roPid = 209;
    const allocate = (label, bytes) => {const address = next; next += Math.ceil(bytes / 4096) * 4096 + 4096;
      native.allocations.push({label, bytes, address}); return address;};
    native.allocate = async (label, bytes) => allocate(label, bytes);
    native.target = {pid, client: {pending: false, pid: roPid, handle: 19,
      info: async () => ({pid, freeAddress: 0x08000000}), unmap: async () => ({result: 0})},
      query: async address => ({result: 0, address, size: 4096, type: 0, permissions: 0, attributes: 0, ipcReferences: 0, deviceReferences: 0}),
      free: async () => {}};
    const call = async () => {
      assert(input && input.length >= 80); dispatched++;
      const count = input.readUInt32LE(36); assert(count > 0 && count <= 64);
      raw = Buffer.alloc(528 + count * 256); raw.writeUInt32LE(count);
      for (let i = 0; i < count; i++) {
        const at = 528 + i * 256;
        raw.writeUInt32LE(0x80000000, at + 4); raw.writeUInt32LE(1, at + 8);
        raw.writeBigUInt64LE(BigInt(roPid), at + 12); raw.write('SFCO', at + 32);
        input.copy(raw, at + 48, 56 + i * 24, 56 + (i + 1) * 24);
      }
      if (mode === 'lost-call') throw Error('Lost acknowledgement after dispatch');
      if (mode === 'nonzero-call') return {result: 9n};
      if (mode === 'bad-pid') raw.writeBigUInt64LE(210n, 528 + 12);
      if (mode === 'bad-result') raw.writeUInt32LE(0x999, 528 + 40);
      if (mode === 'bad-count') raw.writeUInt32LE(count - 1);
      return {result: 0n};
    };
    const rpc = {a: {session: native.session, protocol: 1, sdkCrc32: 3925528160, sdk: 0x30000000},
      request: async () => ({}),
      write: async (_address, bytes) => {transportRequests++; input = Buffer.from(bytes);},
      callAddress: async () => {transportRequests++; return call();},
      read: async (address, bytes) => {
        if (address === rpc.a.sdk + 0x15845c) return Buffer.from('410400d4c0035fd6', 'hex');
        transportRequests++;
        if (mode === 'lost-read') throw Error('Lost result read');
        if (mode === 'short-read') return raw.subarray(0, 15);
        assert.equal(bytes, raw.length); return Buffer.from(raw);
      }};
    if (mode !== 'legacy') rpc.writeCall = async (_address, bytes) => {transportRequests++; input = Buffer.from(bytes); return call();};
    native.rpc = rpc;
    native.mapImage = async folder => {
      const code = await fs.readFile(path.join(folder, 'code.bin'));
      assert.equal(sha(code), source.CODE_SHA, 'Production native helper bytes must stay identical');
      const image = JSON.parse(await fs.readFile(path.join(folder, 'report.json')));
      const sourceAddress = allocate('helper-code', 4096);
      native.mappings.push({source: sourceAddress, destination: image.codeBase, pages: 1, mappedPages: 1});
      native.evidence.images.push({image}); native.image = image; native.evidence.image = image;
      return image;
    };
    const helper = await source.create(native);
    const rows = n => Array.from({length: n}, (_, i) => [pid, 0x40000000 + i * 4096, 0x50000000 + i * 4096]);
    const before = transportRequests;
    if (mode === 'normal' || mode === 'legacy') {
      const replies = [];
      for (const n of [64, 1, 27]) replies.push(...await helper.request(0x524f0012, rows(n)));
      await helper.close();
      assert.equal(native.uncertain, false); assert.equal(native.mappings.length, 0); assert.equal(native.allocations.length, 0);
      return {requests: transportRequests - before, dispatched, replySha256: sha(Buffer.concat(replies.map(r => r.data))), closed: true};
    }
    if (mode === 'invalid-row') {
      await assert.rejects(helper.request(0x524f0012, [[pid + 1, 4096, 8192]]));
      assert.equal(dispatched, 0); assert.equal(native.uncertain, false); await helper.close();
      return {rejectedBeforeDispatch: true};
    }
    await assert.rejects(helper.request(0x524f0012, rows(64)));
    assert.equal(dispatched, 1, 'Never replay a lost or malformed outcome');
    assert(native.uncertain); assert.equal(native.mappings.length, 1); assert.equal(native.allocations.length, 2);
    await assert.rejects(helper.close());
    return {retained: true, dispatched, requests: transportRequests - before};
  }
  const cases = [];
  for (const mode of ['normal', 'legacy', 'invalid-row', 'lost-call', 'nonzero-call', 'bad-pid', 'bad-result', 'bad-count', 'lost-read', 'short-read']) {
    const baseline = await world(sources.baseline, mode), candidate = await world(sources.candidate, mode);
    if (mode === 'normal') {
      assert.equal(baseline.requests, 9); assert.equal(candidate.requests, 6);
      assert.equal(candidate.replySha256, baseline.replySha256);
    } else if (mode === 'legacy') assert.deepEqual(candidate, baseline);
    cases.push({mode, baseline, candidate});
  }
  assert.equal(sha(await fs.readFile(originalFile)), sha(Buffer.from(original)));
  const report = {format: 'ps2-launcher-combined-loader-transport-local/1', passed: true,
    change: 'Combine existing SDK write and call in each 64-page loader batch',
    metric: 'physical bridge round trips for identical loader requests', before: 9, after: 6,
    performanceGainPercent: null, commandCountReductionPercent: 100 / 3,
    status: 'Latency gain has not been measured on hardware',
    scenario: 'Same native helper bytes; 64-page, one-page and 27-page batches plus lost/malformed outcomes',
    environment: 'Local modeled transport; no console command or engine edit',
    evidence: {baseline: originalFile, baselineSha256: sha(Buffer.from(original)), candidate: candidateFile, candidateSha256: sha(Buffer.from(candidate))},
    cases, nativeHelperBytesChanged: false, installedPackageChanged: false, consoleCommands: 0,
    limitations: ['Candidate is not installed. Console startup latency and SD bandwidth remain unmeasured for this change.']};
  await fs.writeFile(path.join(out, 'report.json'), JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
  console.log(JSON.stringify({passed: true, report: path.join(out, 'report.json'), cases: cases.length, candidate: candidateFile}));
}
main().catch(e => {console.error(e.stack); process.exitCode = 1;});
