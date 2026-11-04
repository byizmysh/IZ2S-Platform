const path = require('node:path');
async function start({launchStage, watchAuxiliary, closeSession}, directory) {
  const stages = [
    ['core/services/inspect-display.cjs'],
    ['core/services/inspect-compatibility.cjs'],
    ['core/services/inspect-input.cjs'],
    ['core/services/vulkan-copy.cjs', 'display'],
    ['shells/izpslauncher/server/prepare.cjs', 'auto'],
    ['shells/izpslauncher/server/check-input.cjs', 'auto'],
  ];
  for (const [script, ...args] of stages) await launchStage(script, [directory, ...args]).done;
  const graphics = launchStage(
    'shells/izpslauncher/server/frame-loop.cjs',
    [directory, 'auto'],
    'IZ launcher game event source ready.',
  );
  await graphics.ready;
  const serviceExit = async (name, error) => {
    console.error(name + ' stopped: ' + error.message + '. Console session remains active.');
    await require('node:fs/promises').appendFile(
      path.join(directory, 'runtime-service-errors.jsonl'),
      JSON.stringify({ name, error: error.message, at: new Date().toISOString() }) + '\n',
    );
  };
  const ftp = launchStage('core/console/host/ftp-service.cjs', [directory], 'SDK FTP service ready.');
  await watchAuxiliary(ftp, 'FTP', serviceExit);
  const completions = [graphics.done];
  const catalog = JSON.parse(
    await require('node:fs/promises').readFile(path.join(directory, 'app-runtime.json')),
  );
  if (process.env.IZ18_RESIDENT_GRAPHICS!=='1'&&catalog.catalog.apps.some((app) => Object.keys(app.effects || {}).length)) {
    const effects = launchStage(
      'core/console/audio/game-audio.cjs',
      [directory],
      'Game sound effects ready.',
    );
    await watchAuxiliary(effects, 'Game sound effects', serviceExit);
  } else console.log('No package sound effects: native audio polling is disabled.');
  console.log('IZPSLauncher runtime is running.');
  return { done: Promise.race(completions), close: closeSession };
}

module.exports={start};
