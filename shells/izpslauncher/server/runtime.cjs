const path = require('node:path');
async function start({launchStage, watchAuxiliary, closeSession}, directory) {
  const features = await require('../../../core/session/launcher-features.cjs').read(directory);
  if (!features.fsRpc) console.log('FS RPC disabled; starting the PC-backed launcher without SD or native recovery.');
  const stages = [
    ['core/services/inspect-display.cjs'],
    ['core/services/inspect-compatibility.cjs'],
    ['core/services/inspect-input.cjs'],
    ['core/services/vulkan-copy.cjs', 'display'],
    ['shells/izpslauncher/server/prepare.cjs', 'auto'],
    ['shells/izpslauncher/server/check-input.cjs', 'auto'],
  ];
  for (const [script, ...args] of stages) await launchStage(script, [directory, ...args]).done;
  // Recovery uses FS-backed executable mappings; FS-free sessions keep the
  // ordinary PC-backed renderer and the original native RPC connection.
  if(features.recovery && process.env.IZ2S_SD_LAUNCHER!=='1')await launchStage('core/console/recovery/prepare-launcher.cjs',[directory]).done;
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
  if(process.env.IZ2S_SD_LAUNCHER==='1'){console.log('SD supervisor owns discovery, input and application transitions.');return {done:graphics.done,close:closeSession};}
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
