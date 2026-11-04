'use strict';
// Rebuild the console resources and validate SPIR-V for both IZ2S shells.
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const run = (command, args) => execFileSync(command, args, {
  cwd: root, stdio: 'inherit', env: {...process.env, NODE: process.execPath},
});
async function main() {
  run('python3', ['core/packages/build-focus-assets.py']);
  for (const shell of ['izlauncher', 'izpslauncher']) {
    run('bash', ['shells/' + shell + '/server/build.sh']);
    if (shell === 'izpslauncher') run('python3', ['shells/izpslauncher/server/ps-assets.py']);
    await require('node:fs/promises').copyFile(
      path.join(root, 'shells', shell, 'server/shaders/launcher-text.glsl'),
      path.join(root, 'shells', shell, 'app/shaders/launcher-text.glsl'));
    // Both menus use the same search/overlay metrics and dedicated progress type.
    if (shell === 'izpslauncher') {
      await require('node:fs/promises').copyFile(
        path.join(root, 'shells/izlauncher/app/shaders/focus-type.glsl'),
        path.join(root, 'shells/izpslauncher/app/shaders/focus-type.glsl'));
    }
    await require('../shells/' + shell + '/server/build-app.cjs').build();
  }
}
if (require.main === module) main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
module.exports = {main};
