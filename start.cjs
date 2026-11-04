#!/usr/bin/env node
const fs = require('node:fs/promises'),
  path = require('node:path');
async function main() {
  require('./core/platform/runtime.cjs').configure(__dirname);
  process.env.IZ2S_ROOT = __dirname;
  process.env.IZ15_ROOT = __dirname;
  process.env.IZ2S_PANEL_INSTANCE = require('node:crypto').randomUUID();
  const root = __dirname,
    args = process.argv.slice(2),
    preview = args.includes('--preview'),
    noOpen = args.includes('--no-open'),
    restart = args.includes('--restart');
  if (!process.env.HB_FONT)
    for (const font of [
      path.join(root, 'language/fonts/DejaVuSans.ttf'),
      '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
      '/usr/share/fonts/TTF/DejaVuSans.ttf',
    ])
      if (
        await fs.stat(font).then(
          () => true,
          () => false,
        )
      ) {
        process.env.HB_FONT = font;
        break;
      }
  process.env.HB_BROWSER_PROFILE = path.join(root, 'data/state/browser-profile');
  const { readJson, saveJson } = require('./core/session/ledger.cjs'),
    settings = await readJson(path.join(root, 'panel/profile.json')),
    node = process.execPath;
  const stateFile = path.join(
      root,
      preview ? 'data/state/preview-window.json' : 'data/state/window.json',
    ),
    previous = await readJson(stateFile);
  let running = false;
  if (previous?.pid && previous.url)
    try {
      process.kill(previous.pid, 0);
      const url = new URL(previous.url),
        health = await fetch(url.origin + '/health', { signal: AbortSignal.timeout(1500) }).then(
          (r) => r.json(),
        );
      if (health.version !== 21 || health.pid !== previous.pid || health.preview !== preview)
        throw Error('Stale IZ2S Platform window');
      running = true;
    } catch {}
  if (running) {
    if (restart) {
      process.kill(previous.pid, 'SIGTERM');
      const deadline = Date.now() + 20000;
      for (;;) {
        try { process.kill(previous.pid, 0); }
        catch (error) { if (error.code === 'ESRCH') break; throw error; }
        if (Date.now() >= deadline) throw Error('Previous server is still stopping. Wait before restarting.');
        await new Promise(resolve => setTimeout(resolve, 100));
      }
    } else {
      if (!noOpen)
        require('./core/services/open-panel.cjs').openPanel(previous.url, {
          main: true,
          env: { ...process.env, HB_OPEN_PANEL: '1' },
        });
      console.log('IZ2S Platform is already running.');
      return;
    }
  }
  if (restart && !preview && !running)
    await require('./core/session/panel-handover.cjs').handover({ root, port: settings.panelPort });
  if (!preview) {
    const publication = await require('./sdk/publish-modules.cjs').publish(root);
    await saveJson(path.join(root, 'data/state/module-publish.json'), publication);
    for (const error of publication.errors) console.error('Module source not published: ' + error.id + ': ' + error.error);
  }
  const manager = await require('./panel/controller.cjs').create({ root, settings, node, preview });
  let previousToken;
  try {
    const old = new URL(previous?.url);
    if (old.origin === 'http://127.0.0.1:' + settings.panelPort && /^#[a-f0-9]{48}$/.test(old.hash))
      previousToken = old.hash.slice(1);
  } catch {}
  const authFile=path.join(root,preview?'data/state/preview-auth.json':'data/state/panel-auth.json');
  const auth=await readJson(authFile,{});
  if(/^[a-f0-9]{48}$/.test(auth.token||''))previousToken=auth.token;
  previousToken ||= require('node:crypto').randomBytes(24).toString('hex');
  await saveJson(authFile,{token:previousToken});
  const window = await require('./panel/http.cjs').createWindowServer({
    root,
    manager,
    port: preview ? 0 : settings.panelPort,
    ...(previousToken ? { token: previousToken } : {}),
  });
  await saveJson(stateFile, {
    pid: process.pid,
    url: window.url,
    preview,
    version: 21,
    started: new Date().toISOString(),
    workspaceInstance: process.env.IZ2S_PANEL_INSTANCE,
  });
  console.log('IZ2S Platform');
  console.log(window.url);
  if (!noOpen)
    require('./core/services/open-panel.cjs').openPanel(window.url, {
      main: true,
      env: { ...process.env, HB_OPEN_PANEL: '1' },
    });
  const shutdown = require('./core/session/desktop-shutdown.cjs').createDesktopShutdown({
    manager,
    closeModules: () => Promise.all([require('./core/host-modules.cjs').close(), require('./core/module-settings.cjs').close(root)]),
    closeWindow: () => window.close(),
    removeState: async () => {
      const current = await readJson(stateFile, {});
      if (current.pid === process.pid) await fs.rm(stateFile, { force: true });
    },
    onDeferred: () => console.error('Session shutdown remains pending; retaining the panel until its process actually closes.'),
    onError: error => console.error(error),
  });
  process.on('SIGINT', () => shutdown.finish().catch(console.error));
  process.on('SIGTERM', () => shutdown.finish().catch(console.error));
}
if (require.main === module)
  main().catch((e) => {
    console.error('IZ2S Platform: ' + e.message);
    process.exitCode = 1;
  });
module.exports = { main };
