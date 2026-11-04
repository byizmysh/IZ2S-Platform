const path = require('node:path');
let handle,
  stopping = false;
process.on('message', async (message) => {
  if (message.action === 'start') {
    try {
      process.env.IZ2S_ROOT = message.root;
      process.env.IZ15_ROOT = message.root;
      process.env.IZ2S_MODULE_ID = message.id;
      handle = await require('./modules.cjs').activate(message.root, message.id, {
        session: message.session,
        services: require('../sdk/platform.cjs'),
      });
      await handle.start();
      process.send?.({ state: 'running' });
    } catch (e) {
      process.send?.({ state: 'failed', error: e.message });
      process.exitCode = 1;
      process.disconnect?.();
    }
  } else if (message.action === 'stop' && !stopping) {
    stopping = true;
    try {
      await handle?.stop();
      process.send?.({ state: 'disposed' });
    } finally {
      process.disconnect?.();
    }
  }
});
process.on('disconnect', () => {
  if (!stopping) {
    stopping = true;
    Promise.resolve(handle?.stop()).finally(() => process.exit());
  }
});
