const fs = require('node:fs'), path = require('node:path');
function configure(root, {env = process.env, platform = process.platform, exists = fs.existsSync} = {}) {
  const win = platform === 'win32', join = win ? path.win32.join : path.join;
  const bins = [join(root, win ? 'data/runtime/node' : 'data/runtime/node/bin'),
    join(root, win ? 'data/runtime/venv/Scripts' : 'data/runtime/venv/bin'),
    join(root, 'data/runtime/openssh'), join(root, 'data/runtime/openssh/OpenSSH-Win64'),
    join(root, 'data/runtime/openssh/OpenSSH-ARM64')].filter(exists);
  const key = Object.keys(env).find(k => k.toUpperCase() === 'PATH') || 'PATH';
  env[key] = [...bins, env[key] || ''].join(win ? ';' : path.delimiter);
  env.IZ2S_PYTHON = require('./python.cjs').command(root, {env, platform, exists});
  env.PYTHONUTF8 = '1';
  env.PYTHONNOUSERSITE = '1';
  return env;
}
module.exports = {configure};
