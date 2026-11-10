#!/usr/bin/env node
'use strict';
// Panel adaptation: prepare dependencies without running upstream postinstall
// or granting capabilities to a general-purpose interpreter automatically.
const fs = require('node:fs/promises'), path = require('node:path');
const {spawn} = require('node:child_process');
async function setup(root = path.resolve(__dirname, '../..')) {
  if (process.platform !== 'linux') throw Error('CIV7 LAN requires native Linux');
  const p = require('./integration.cjs').paths(root);
  async function run(command, args, cwd = p.folder) {
    await new Promise((resolve, reject) => {
      const child = spawn(command, args, {cwd, stdio: 'inherit'});
      child.on('error', reject);
      child.on('close', (code, signal) => code === 0 ? resolve() : reject(Error('Dependency setup failed: ' + (signal || code))));
    });
  }
  await run('npm', ['ci', '--ignore-scripts', '--no-audit', '--no-fund']);
  await run(require('../../core/platform/python.cjs').command(root), ['-m', 'venv', p.runtime]);
  await run(p.python, ['-m', 'pip', 'install', '-r', path.join(p.folder, 'ldn/requirements.txt')]);
  const source = await fs.realpath(p.python);
  // Do not overwrite a configured helper on each preparation.
  const bytes = await fs.readFile(source);
  const same = await fs.readFile(p.helper).then(old => old.equals(bytes), () => false);
  if (!same) { await fs.copyFile(source, p.helper); await fs.chmod(p.helper, 0o755); }
  console.log('CIV7_LDN_DEPENDENCIES_READY');
  console.log('Network permissions must be enabled once using the command in step 1.');
}
if (require.main === module) setup().catch(error => {console.error(error.message); process.exitCode = 1;});
module.exports = {setup};
