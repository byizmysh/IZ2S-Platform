'use strict';

const assert = require('node:assert/strict');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

if (process.argv.length !== 4) {
  console.error('Usage: node build.cjs <RUN32 directory> <output SD root>');
  process.exit(1);
}

const base = path.resolve(process.argv[2]);
const sdRoot = path.resolve(process.argv[3]);
assert.notEqual(base, sdRoot, 'Choose a separate output directory.');
assert(!sdRoot.startsWith(base + path.sep), 'Keep the RUN32 base separate from the build.');
const original = path.join(base, 'engines/iz2s-core/runtime/package/iz2s');
const { SECTION, crc32, decode } = require(path.join(base, 'core/console/sd-launcher/package.cjs'));
const input = fs.readFileSync(path.join(original, 'izlauncher.iza'));
const menu = decode(input);
assert.equal(menu.id, 'izlauncher');
assert.equal(menu.version, 1);
assert.equal(menu.flags, 0);
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'iz2s-ui-'));
try {
  const sections = { ...menu.sections };
  for (const [section, extension] of [['fragment', 'frag'], ['compute', 'comp']]) {
    const output = path.join(temporary, `app.${extension}.spv`);
    const source = path.join(__dirname, `shaders/app.${extension}`);
    execFileSync('glslangValidator', ['-V', '-Os', '-DIZ_CORE_LOCAL_ADD', '-o', output, source], { stdio: 'inherit' });
    execFileSync('spirv-val', ['--target-env', 'vulkan1.0', output], { stdio: 'inherit' });
    sections[section] = fs.readFileSync(output);
  }

  const header = Buffer.from(input.subarray(0, 512));
  const chunks = [header];
  let cursor = header.length;
  for (const [index, section] of SECTION.slice(0, 10).entries()) {
    const bytes = sections[section];
    if (!bytes.length) continue;
    const padding = (4 - cursor % 4) % 4;
    if (padding) chunks.push(Buffer.alloc(padding));
    cursor += padding;
    const entry = 176 + 16 * index;
    header.writeUInt32LE(cursor, entry);
    header.writeUInt32LE(bytes.length, entry + 4);
    header.writeUInt32LE(crc32(bytes), entry + 8);
    chunks.push(bytes);
    cursor += bytes.length;
  }
  header.writeUInt32LE(cursor, 16);
  const result = Buffer.concat(chunks, cursor);
  const { sections: verified } = decode(result);
  assert.deepEqual(verified, sections);
  const target = path.join(sdRoot, 'iz2s');
  fs.mkdirSync(target, { recursive: true });
  fs.writeFileSync(path.join(target, 'izlauncher.iza'), result, { flag: 'wx' });
  fs.copyFileSync(path.join(original, 'izlauncher.romfs'), path.join(target, 'izlauncher.romfs'), fs.constants.COPYFILE_EXCL);
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}
