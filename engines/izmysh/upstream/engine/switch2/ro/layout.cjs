// Use the current boot's captured heap as an index, then validate live objects.
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const directory = __dirname;
const check = (ok, message) => { if (!ok) throw Error(message); };
const ptr = (bytes, offset = 0) => Number(bytes.readBigUInt64LE(offset));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const save = (name, value) => fs.writeFile(path.join(directory, name), JSON.stringify(value, null, 2) + '\n', {mode: 0o600});

function capturedLayoutAtSdk(bytes, range, sdk, rootVptr = sdk - 0xf38be0) {
  const captured = (address, length) => {
    check(Number.isSafeInteger(address) && address >= range.address && address + length <= range.address + bytes.length,
      'Layout pointer left the verified heap capture');
    return bytes.subarray(address - range.address, address - range.address + length);
  };
  const domains = [];
  for (let i = 0; i + 0x40 <= bytes.length; i += 8)
    if (ptr(bytes, i) === rootVptr + 0x28e0 && ptr(bytes, i + 8) === rootVptr + 0x2938)
      domains.push({address: range.address + i, manager: ptr(bytes, i + 0x18)});
  const managers = [...new Set(domains.map(d => d.manager))];
  check(managers.length === 1, 'The captured domain manager is not unique');
  const manager = managers[0], dm = captured(manager, 0x20);
  const count = dm.readUInt32LE(0x10), table = ptr(dm, 0x18);
  check(count === 0x4000, 'Native domain capacity differs');
  const entries = captured(table, 0x4000), matches = [];
  for (let i = 0; i + 0x30 <= entries.length; i += 0x30)
    if (ptr(entries, i + 0x28) === rootVptr + 0x5880 && domains.some(d => d.address === ptr(entries, i + 0x18)))
      matches.push({entry: table + i, object: ptr(entries, i + 0x20), domain: ptr(entries, i + 0x18)});
  check(matches.length === 1, 'The captured Loader root is not unique');
  return {sdk, rootVptr, range, domains, manager, table, count, target: matches[0],
    holderVptr: rootVptr - 0x7d78, slots: manager + 0xd48,
    waiters: Array.from({length: 7}, (_, i) => manager + 0x410 + i * 0x110)};
}

function capturedLayout(bytes, range) {
  const nominalSdk = range.address + 0x500000;
  // The metadata capture identifies the SDK page. The distance to the
  // domain vtable changes across observed FS layouts, so infer that pointer
  // separately and accept exactly one complete structural match.
  const candidates = new Set([nominalSdk - 0xf38be0]);
  for (let i = 0; i + 0x40 <= bytes.length; i += 8) {
    const first = ptr(bytes, i), second = ptr(bytes, i + 8);
    if (second - first !== 0x58) continue;
    const rootVptr = first - 0x28e0;
    const oldDistanceSdk = rootVptr + 0xf38be0;
    if (Number.isSafeInteger(rootVptr) && oldDistanceSdk % 0x1000 === 0 &&
        Math.abs(oldDistanceSdk - nominalSdk) <= 0x10000) candidates.add(rootVptr);
  }
  check(candidates.size <= 64, 'Too many nearby domain vtable candidates');
  const matches = [];
  for (const rootVptr of candidates) {
    try { matches.push(capturedLayoutAtSdk(bytes, range, nominalSdk, rootVptr)); }
    catch { /* Other offsets must fail the structural checks. */ }
  }
  check(matches.length === 1, 'The captured domain layout is not unique');
  return {...matches[0], sdkDiscovery: {nominal: nominalSdk,
    rootVptrCandidates: [...candidates].sort((a, b) => a - b), selected: nominalSdk,
    selectedRootVptr: matches[0].rootVptr}};
}

function sdkPageHeader(bytes, sdk) {
  check(bytes.length >= 0x850 && ptr(bytes, 0x838) === sdk + 0x668 &&
    ptr(bytes, 0x830) >= 2 ** 32 && ptr(bytes, 0x830) < 2 ** 39 &&
    bytes.readUInt32LE(0x848) > 0 && bytes.readUInt32LE(0x848) <= 128,
  'The captured SDK page does not contain the expected thread-ring header');
  return {first: ptr(bytes, 0x830), count: bytes.readUInt32LE(0x848)};
}

async function validate(memory, layout) {
  check(layout.report === memory.reportFile && layout.session === memory.info.session, 'Layout belongs to another boot');
  const dm = await memory.read(layout.manager, 0x20);
  check(dm.readUInt32LE(0x10) === layout.count && ptr(dm, 0x18) === layout.table, 'Live domain manager changed');
  const entry = await memory.read(layout.target.entry, 0x30);
  check(ptr(entry, 0x18) === layout.target.domain && ptr(entry, 0x20) === layout.target.object &&
    ptr(entry, 0x28) === layout.rootVptr + 0x5880, 'Live Loader entry differs from the capture');
  const domain = await memory.read(layout.target.domain, 0x20);
  check(ptr(domain) === layout.rootVptr + 0x28e0 && ptr(domain, 8) === layout.rootVptr + 0x2938 &&
    ptr(domain, 0x18) === layout.manager, 'Live Loader domain changed');
  const object = await memory.read(layout.target.object, 0x40);
  check(ptr(object) === layout.rootVptr + 0x1ea0 && object.readBigUInt64LE(0x30) === 1n &&
    [1, 2].includes(object.readUInt32LE(0x38)), 'Live Loader object identity differs');
}

async function main() {
  const {connect} = require('../tools/fs-memory.cjs');
  const memory = await connect();
  const range = memory.report.retainedMemory.captures.find(c => c.length === 0x4d8000);
  check(range, 'The current bootstrap heap capture is missing');
  const bytes = await fs.readFile(range.outputFile);
  check(bytes.length === range.length && hash(bytes) === range.sha256, 'Bootstrap capture hash differs');
  const layout = {session: memory.info.session, report: memory.reportFile, ...capturedLayout(bytes, range),
    discovery: 'captured index with live identity checks'};
  const page = memory.report.retainedMemory.captures.find(c =>
    c.address === layout.sdk && [0x1000, 0x4000].includes(c.length));
  check(page, 'The verified SDK page capture is missing');
  const pageBytes = await fs.readFile(page.outputFile);
  check(pageBytes.length === page.length && hash(pageBytes) === page.sha256,
    'SDK page capture length or hash differs');
  layout.sdkPage = {address: page.address, sha256: page.sha256,
    ...sdkPageHeader(pageBytes, layout.sdk)};
  await validate(memory, layout);
  await save(memory.info.session + '-layout.json', layout);
  const sentinel = layout.sdk + 0x830, nodes = [], seen = new Set();
  let at = await memory.pointer(sentinel);
  while (at !== sentinel) {
    check(at && !seen.has(at) && nodes.length < 128, 'Native thread ring did not terminate');
    seen.add(at);
    const b = await memory.read(at, 0x1c0);
    nodes.push({address: at, next: ptr(b), state: b[0x40], aliased: b[0x41], originalStack: ptr(b, 0x48),
      stack: ptr(b, 0x50), size: ptr(b, 0x58), argument: ptr(b, 0x60), entry: ptr(b, 0x68),
      handle: b.readUInt32LE(0x1b0), id: ptr(b, 0x1b8), hex: b.toString('hex')});
    at = ptr(b);
  }
  await save(memory.info.session + '-thread-ring.json', {session: memory.info.session, sdk: layout.sdk, sentinel, nodes});
  return {session: memory.info.session, manager: layout.manager, threads: nodes.length};
}

module.exports = {main, validate, capturedLayout, sdkPageHeader, directory, check, ptr, save};
if (require.main === module) main().then(console.log).catch(error => { console.error(error.stack); process.exitCode = 1; });
