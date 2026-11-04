const fs = require('node:fs');
const path = require('node:path');
const required = require('./runtime-files.json');

// These are production pipeline checks and payloads, including files named
// test-sm, test-gate and test-function. Validate before any console operation.
function verify(root = path.resolve(__dirname, '..')) {
  const missing = required.filter(name => {
    try { const file = fs.statSync(path.join(root, name)); return !file.isFile() || !file.size; }
    catch { return true; }
  });
  if (missing.length)
    throw Error('Engine runtime is incomplete; no console commands were sent. Missing: ' + missing.join(', '));
  return {verified: true, files: required.length};
}

module.exports = {verify, required};
