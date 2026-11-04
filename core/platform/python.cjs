// Invoke Python as an executable, without shell commands or a Windows Store alias.
const fs = require('node:fs'), path = require('node:path');
function command(root = require('../root.cjs'), {env = process.env, platform = process.platform, exists = fs.existsSync} = {}) {
  if (env.IZ2S_PYTHON) return env.IZ2S_PYTHON;
  const managed = (platform === 'win32' ? path.win32 : path).join(root, 'data/runtime/venv', platform === 'win32' ? 'Scripts/python.exe' : 'bin/python3');
  return exists(managed) ? managed : platform === 'win32' ? 'python.exe' : 'python3';
}
module.exports = {command};
