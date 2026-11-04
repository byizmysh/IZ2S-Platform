#!/usr/bin/env node
// Compatibility entry point: use this engine's local Original FS runner.
const host = require('../upstream/run.cjs');
if (require.main === module)
  host.main().catch(error => {console.error(error.stack); process.exitCode = 1;});
module.exports = host;
