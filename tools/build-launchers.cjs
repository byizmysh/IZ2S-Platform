'use strict';
// Rebuild the console resources and validate SPIR-V for both IZ2S shells.
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const run = (command, args) => execFileSync(command, args, {
  cwd: root, stdio: 'inherit', env: {...process.env, NODE: process.execPath},
});
async function main() {
  // These companions share native-app.h. Rebuild them together so a header
  // update cannot leave a valid supervisor paired with stale SD applications.
  await require('../core/console/sd-launcher/build.cjs').main();
  await require('../core/console/recovery/build.cjs').main();
  await require('./sd-launcher/build-reconnect.cjs').main();
  await require('./sd-launcher/build-reader.cjs').main();
  require('../core/console/sd-launcher/check.cjs').verifyRuntime();
  run('python3', ['core/packages/build-focus-assets.py']);
  // IZPS consumes the shared base atlas before IZ2Launcher paints its own
  // brand and activity labels. Keep each shell's typography independent.
  for (const shell of ['izpslauncher', 'izlauncher']) {
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
      await require('node:fs/promises').copyFile(
        path.join(root, 'shells/izlauncher/app/shaders/menu-install.glsl'),
        path.join(root, 'shells/izpslauncher/app/shaders/menu-install.glsl'));
    }
    await require('../shells/' + shell + '/server/build-app.cjs').build();
  }
}
if (require.main === module) main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
module.exports = {main};
