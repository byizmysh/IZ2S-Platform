#!/usr/bin/env node
// rebuild ftp native code. linux only
// bit weird to have it as a node script but this lets me call it from other code :)
// ^ i now no longer do this to make the base tool a bit more compatible but i can't be bothered to rework this for windows
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { execFileSync } = require('node:child_process');

function main() {
  const directory = path.join(__dirname, 'oz-code');
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'ns2-ftp-build-'));
  try {
    const object = path.join(temporary, 'native.o');
    const elf = path.join(temporary, 'native.elf');
    const binary = path.join(temporary, 'native.bin');
    execFileSync(
      'aarch64-linux-gnu-gcc',
      [
        '-c',
        '-Os',
        '-ffreestanding',
        '-fno-builtin',
        '-fPIC',
        '-fno-stack-protector',
        '-fno-asynchronous-unwind-tables',
        '-fno-unwind-tables',
        '-fno-jump-tables',
        '-ffunction-sections',
        '-mgeneral-regs-only',
        '-mno-outline-atomics',
        path.join(directory, 'native.c'),
        '-o',
        object,
      ],
      { stdio: 'inherit' },
    );
    execFileSync('aarch64-linux-gnu-ld', ['-T', path.join(directory, 'layout.ld'), object, '-o', elf], {
      stdio: 'inherit',
    });

    execFileSync('aarch64-linux-gnu-objcopy', ['-O', 'binary', elf, binary], { stdio: 'inherit' });
    fs.copyFileSync(binary, path.join(directory, 'native.bin'));
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}

if (require.main === module) {
  try {
    main();
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
