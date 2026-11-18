#!/usr/bin/env node
// Run the regular exploit, then opt in to read-only system-program CodeFS
// capture before the upstream runner closes its SmileBASIC-backed RPC.
if (process.argv[2] === '--help') {
  console.log('Usage: node dump-firmware.cjs [computer-IP]\n' +
    'This launches the exploit first. See README.md for scope and safety notes.');
} else {
  require('./run.cjs').main(['--dump-firmware', ...process.argv.slice(2)]).catch(error => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
