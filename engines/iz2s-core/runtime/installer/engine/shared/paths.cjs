const path = require('node:path');

// Acquisition runs inside its run directory. Maintained host tools instead
// select that directory explicitly and keep executing source from engine/.
const runDirectory = process.env.NS2_RUN_DIR
  ? path.resolve(process.env.NS2_RUN_DIR)
  : path.resolve(__dirname, '..');

function runPath(...parts) {
  return path.join(runDirectory, ...parts);
}

module.exports = {runDirectory, runPath};
