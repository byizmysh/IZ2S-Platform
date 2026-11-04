// Validate packaged launch resources before an input job or native owner starts.
const fs = require('node:fs');
const path = require('node:path');
const inventory = require('./runtime-files.json');
function verify(root = path.resolve(__dirname, '../..'), shell = 'izlauncher') {
  const resources = inventory.shells[shell];
  if (!resources) throw Error('Unsupported launcher: ' + shell);
  const files = [...inventory.common, ...resources];
  const missing = files.filter(name => {
    try {
      const stat = fs.statSync(path.join(root, name));
      return !stat.isFile() || stat.size === 0;
    } catch { return true; }
  });
  if (missing.length) {
    const error = Error('Launcher runtime is incomplete; no console commands were sent. Restore IZ2S files: ' + missing.join(', '));
    error.code = 'IZ2S_RUNTIME_INCOMPLETE';
    error.missing = missing;
    throw error;
  }
  return { verified: true, shell, files: files.length };
}
module.exports = { verify };
