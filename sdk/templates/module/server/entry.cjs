module.exports = {
  async createModule(context) {
    let running = false;
    // PC-local data stays here. Console files belong to context.sdStorage.path.
    // With your writable console SD adapter, call await context.sdStorage.ensure(storage).
    return {
      async init() {await require('node:fs/promises').mkdir(context.dataDirectory, {recursive: true});},
      async start() {running = true;},
      async stop() {running = false;},
      async dispose() {},
      status: () => ({running}),
    };
  },
};
