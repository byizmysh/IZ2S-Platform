// PC module archive: ZIP (stored/deflate), UTF-8 paths, per-file SHA-256 and CRC32.
const fs = require('node:fs/promises'),
  path = require('node:path'),
  zlib = require('node:zlib'),
  crypto = require('node:crypto');
const { assert, relative, moduleDescriptor } = require('./descriptors.cjs');
// Portable packages use ZIP32. 0xffffffff is the ZIP64 sentinel, not a size.
const MAX_ARCHIVE = 0xfffffffe,
  MAX_EXPANDED = 0xfffffffe,
  MAX_FILE = 0xfffffffe,
  MAX_FILES = 4096;
const table = Array.from({ length: 256 }, (_, n) => {
  for (let i = 0; i < 8; i++) n = n & 1 ? 0xedb88320 ^ (n >>> 1) : n >>> 1;
  return n >>> 0;
});
function crc32(b) {
  let c = 0xffffffff;
  for (const v of b) c = table[(c ^ v) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
const sha = (b) => crypto.createHash('sha256').update(b).digest('hex');
function encode(entries) {
  assert(entries.length > 0 && entries.length <= MAX_FILES, 'Too many package files');
  const locals = [],
    centrals = [],
    seen = new Set();
  let offset = 0,
    total = 0;
  for (const { name, bytes } of [...entries].sort((a, b) => a.name.localeCompare(b.name))) {
    relative(name);
    assert(!seen.has(name.toLowerCase()), 'Duplicate package path');
    seen.add(name.toLowerCase());
    assert(Buffer.isBuffer(bytes) && bytes.length <= MAX_FILE, 'Package file too large');
    total += bytes.length;
    assert(total <= MAX_EXPANDED, 'Expanded package too large');
    const n = Buffer.from(name),
      packed = zlib.deflateRawSync(bytes),
      crc = crc32(bytes),
      h = Buffer.alloc(30);
    h.writeUInt32LE(0x04034b50);
    h.writeUInt16LE(20, 4);
    h.writeUInt16LE(0x800, 6);
    h.writeUInt16LE(8, 8);
    h.writeUInt32LE(crc, 14);
    h.writeUInt32LE(packed.length, 18);
    h.writeUInt32LE(bytes.length, 22);
    h.writeUInt16LE(n.length, 26);
    locals.push(h, n, packed);
    const c = Buffer.alloc(46);
    c.writeUInt32LE(0x02014b50);
    c.writeUInt16LE(0x314, 4);
    c.writeUInt16LE(20, 6);
    c.writeUInt16LE(0x800, 8);
    c.writeUInt16LE(8, 10);
    c.writeUInt32LE(crc, 16);
    c.writeUInt32LE(packed.length, 20);
    c.writeUInt32LE(bytes.length, 24);
    c.writeUInt16LE(n.length, 28);
    c.writeUInt32LE((0o100644 << 16) >>> 0, 38);
    c.writeUInt32LE(offset, 42);
    centrals.push(c, n);
    offset += h.length + n.length + packed.length;
    assert(offset <= MAX_ARCHIVE, 'Package exceeds ZIP32 offset range');
  }
  const central = Buffer.concat(centrals),
    end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(entries.length, 8);
  end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(central.length, 12);
  end.writeUInt32LE(offset, 16);
  assert(offset + central.length + end.length <= MAX_ARCHIVE, 'Package exceeds ZIP32 size range');
  const out = Buffer.concat([...locals, central, end]);
  assert(out.length <= MAX_ARCHIVE, 'Package archive too large');
  return out;
}
function decode(bytes, { allowDirectories = false } = {}) {
  assert(
    Buffer.isBuffer(bytes) && bytes.length >= 22 && bytes.length <= MAX_ARCHIVE,
    'Invalid archive size',
  );
  let end = -1;
  for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 65557); i--)
    if (
      bytes.readUInt32LE(i) === 0x06054b50 &&
      i + 22 + bytes.readUInt16LE(i + 20) === bytes.length
    ) {
      end = i;
      break;
    }
  assert(end >= 0, 'Invalid ZIP end record');
  assert(
    bytes.readUInt16LE(end + 4) === 0 && bytes.readUInt16LE(end + 6) === 0,
    'Multi-disk ZIP unsupported',
  );
  const count = bytes.readUInt16LE(end + 10),
    length = bytes.readUInt32LE(end + 12),
    start = bytes.readUInt32LE(end + 16);
  assert(
    count > 0 &&
      count <= MAX_FILES &&
      count === bytes.readUInt16LE(end + 8) &&
      start + length === end,
    'Invalid ZIP directory',
  );
  const entries = [],
    names = new Set(),
    ranges = [],
    directories = [];
  let at = start,
    total = 0;
  for (let i = 0; i < count; i++) {
    assert(at + 46 <= end && bytes.readUInt32LE(at) === 0x02014b50, 'Invalid ZIP entry');
    const flags = bytes.readUInt16LE(at + 8),
      method = bytes.readUInt16LE(at + 10),
      crc = bytes.readUInt32LE(at + 16),
      size = bytes.readUInt32LE(at + 20),
      expanded = bytes.readUInt32LE(at + 24),
      n = bytes.readUInt16LE(at + 28),
      extra = bytes.readUInt16LE(at + 30),
      comment = bytes.readUInt16LE(at + 32),
      mode = bytes.readUInt32LE(at + 38) >>> 16,
      offset = bytes.readUInt32LE(at + 42);
    assert(
      !(flags & ~0x800) && [0, 8].includes(method),
      'Encrypted/streamed/unsupported ZIP entry',
    );
    assert(
      expanded <= MAX_FILE && size <= MAX_ARCHIVE && at + 46 + n + extra + comment <= end,
      'Oversized ZIP entry',
    );
    const name = bytes.subarray(at + 46, at + 46 + n).toString('utf8');
    assert(!name.includes('\ufffd'), 'Invalid UTF-8 path');
    const directory = allowDirectories && name.endsWith('/');
    const type = mode & 0xf000;
    assert(!type || type === (directory ? 0x4000 : 0x8000), 'Links and special files are forbidden');
    if (directory) assert(expanded === 0, 'ZIP directory contains file data');
    const member = directory ? name.slice(0, -1) : name;
    relative(member);
    const folded = member.toLowerCase();
    assert(!names.has(folded), 'Duplicate ZIP path');
    names.add(folded);
    assert(
      offset + 30 <= start && bytes.readUInt32LE(offset) === 0x04034b50,
      'Invalid local ZIP header',
    );
    assert(
      bytes.readUInt16LE(offset + 6) === flags &&
        bytes.readUInt16LE(offset + 8) === method &&
        bytes.readUInt32LE(offset + 14) === crc &&
        bytes.readUInt32LE(offset + 18) === size &&
        bytes.readUInt32LE(offset + 22) === expanded,
      'ZIP headers disagree',
    );
    const ln = bytes.readUInt16LE(offset + 26),
      le = bytes.readUInt16LE(offset + 28),
      begin = offset + 30 + ln + le;
    assert(
      bytes.subarray(offset + 30, offset + 30 + ln).toString('utf8') === name &&
        begin + size <= start,
      'Invalid local ZIP path/range',
    );
    assert(!ranges.some(([a, b]) => offset < b && begin + size > a), 'Overlapping ZIP entries');
    ranges.push([offset, begin + size]);
    total += expanded;
    assert(total <= MAX_EXPANDED, 'Expanded package too large');
    const packed = bytes.subarray(begin, begin + size),
      data =
        method === 8
          ? zlib.inflateRawSync(packed, { maxOutputLength: Math.max(1, expanded) })
          : Buffer.from(packed);
    assert(data.length === expanded && crc32(data) === crc, 'ZIP integrity check failed');
    if (directory) directories.push(folded);
    else entries.push({ name, bytes: data });
    at += 46 + n + extra + comment;
  }
  assert(at === end, 'Invalid ZIP directory size');
  if (directories.length) {
    const parents = new Set();
    for (const {name} of entries) {
      const folded = name.toLowerCase();
      for (let slash = folded.indexOf('/'); slash !== -1; slash = folded.indexOf('/', slash + 1))
        parents.add(folded.slice(0, slash));
    }
    assert(directories.every(name => parents.has(name)), 'ZIP directory has no file entries');
  }
  return entries;
}
function inspect(bytes) {
  const inspected = inspectFiles(decode(bytes));
  return { ...inspected, sha256: sha(bytes), packageBytes: bytes.length };
}
// Compression bytes differ across supported Node/zlib versions. Validate both
// archives before comparing payloads; generated checksum JSON ordering is not content.
function sameContents(left, right) {
  const a = inspect(left).files.filter(file => file.name !== 'checksums.json');
  const b = new Map(inspect(right).files.filter(file => file.name !== 'checksums.json')
    .map(file => [file.name, file.bytes]));
  return a.length === b.size && a.every(file => b.get(file.name)?.equals(file.bytes));
}
// The same content validation for already extracted, session-pinned files.
// Do not ZIP/compress/decompress them merely to reconstruct an inventory.
function inspectFiles(files) {
  assert(Array.isArray(files) && files.length > 0 && files.length <= MAX_FILES, 'Too many package files');
  const seen = new Set(); let total = 0;
  for (const {name, bytes} of files) {
    relative(name);
    assert(!seen.has(name.toLowerCase()), 'Duplicate package path'); seen.add(name.toLowerCase());
    assert(Buffer.isBuffer(bytes) && bytes.length <= MAX_FILE, 'Package file too large');
    total += bytes.length; assert(total <= MAX_EXPANDED, 'Expanded package too large');
  }
  const byName = new Map(files.map((f) => [f.name, f.bytes]));
  assert(byName.has('module.json') && byName.has('checksums.json'), 'Invalid PC module archive');
  assert(
    byName.get('module.json').length <= 65536 && byName.get('checksums.json').length <= 1024 * 1024,
    'Metadata too large',
  );
  const descriptor = moduleDescriptor(JSON.parse(byName.get('module.json'))),
    checks = JSON.parse(byName.get('checksums.json'));
  assert(
    checks.algorithm === 'sha256' &&
      checks.files &&
      typeof checks.files === 'object' &&
      !Array.isArray(checks.files),
    'Invalid checksum manifest',
  );
  const keys = Object.keys(checks.files);
  assert(keys.length === files.length - 1, 'Checksum file inventory differs');
  for (const f of files)
    if (f.name !== 'checksums.json')
      assert(
        Object.hasOwn(checks.files, f.name) && checks.files[f.name] === sha(f.bytes),
        'Checksum mismatch: ' + f.name,
      );
  for (const name of keys)
    assert(name !== 'checksums.json' && byName.has(name), 'Unknown checksum file');
  for (const name of descriptor.switch?.files || []) {
    assert(name.startsWith('switch/') && byName.has(name), 'Missing/invalid Switch file: ' + name);
  }
  if (descriptor.server)
    assert(
      byName.has(descriptor.server.entry) && descriptor.server.entry.startsWith('server/'),
      'Missing server entry',
    );
  if (descriptor.server?.settings)
    assert(byName.has(descriptor.server.settings.entry), 'Missing module settings entry');
  for (const name of descriptor.server?.assets || [])
    assert(byName.has(name), 'Missing server asset: ' + name);
  for (const target of Object.values(descriptor.server?.implementations || {})) {
    if (target.startsWith('module:')) continue;
    const prefix = 'modules/' + descriptor.id + '/';
    if (target.startsWith(prefix))
      assert(byName.has(target.slice(prefix.length)), 'Missing server implementation: ' + target);
  }
  assert(byName.has('README.md') && byName.has('LICENSE'), 'README and license notice required');
  return {
    descriptor,
    files,
    expandedBytes: files.reduce((n, f) => n + f.bytes.length, 0),
  };
}
async function pack(folder) {
  const entries = [];
  async function walk(dir, prefix = '') {
    for (const e of await fs.readdir(dir, { withFileTypes: true })) {
      if (
        e.name.startsWith('.') ||
        ['node_modules', 'runtime', 'data', 'test-data', '__pycache__'].includes(e.name) ||
        e.name.endsWith('.module.zip') ||
        e.name.endsWith('.izm') ||
        e.name === 'checksums.json'
      )
        continue;
      assert(!e.isSymbolicLink(), 'Links cannot be packaged');
      const name = prefix + e.name;
      if (e.isDirectory()) await walk(path.join(dir, e.name), name + '/');
      else if (e.isFile()) entries.push({ name, bytes: await fs.readFile(path.join(dir, e.name)) });
    }
  }
  await walk(folder);
  const checks = {
    algorithm: 'sha256',
    files: Object.fromEntries(entries.map((f) => [f.name, sha(f.bytes)])),
  };
  entries.push({
    name: 'checksums.json',
    bytes: Buffer.from(JSON.stringify(checks, null, 2) + '\n'),
  });
  const bytes = encode(entries);
  inspect(bytes);
  return bytes;
}
async function unpack(bytes, destination) {
  const pkg = inspect(bytes);
  await fs.mkdir(destination, { recursive: true });
  assert((await fs.readdir(destination)).length === 0, 'Extraction destination must be empty');
  try {
    await writeFiles(destination, pkg.files);
  } catch (e) {
    await fs.rm(destination, { recursive: true, force: true });
    throw e;
  }
  return pkg;
}

// Use the declared manifest's directory as the base for Switch assets.
function switchApp({descriptor, files}) {
  const entry = descriptor.switch?.entry;
  assert(entry?.startsWith('switch/'), 'This package has no Switch app.');
  const prefix = path.posix.dirname(entry) + '/';
  const source = files.find(file => file.name === entry);
  assert(source, 'The Switch app manifest is missing.');
  const manifest = JSON.parse(source.bytes);
  assert(manifest?.id === descriptor.id, 'Switch application identity differs.');
  return {entry, prefix, manifest, files: files.filter(file => file.name.startsWith(prefix))
    .map(file => ({name: file.name.slice(prefix.length), bytes: file.bytes}))};
}

async function writeFiles(destination, files) {
  for (const file of files) {
    const target = path.join(destination, relative(file.name));
    await fs.mkdir(path.dirname(target), { recursive: true });
    await fs.writeFile(target, file.bytes, { flag: 'wx', mode: 0o600 });
  }
}

module.exports = {
  MAX_ARCHIVE,
  MAX_EXPANDED,
  MAX_FILE,
  MAX_FILES,
  crc32,
  sha,
  encode,
  decode,
  inspect,
  sameContents,
  inspectFiles,
  switchApp,
  writeFiles,
  pack,
  unpack,
};
