const path=require('node:path');
const {invoke,bundle,plan}=require('../common.cjs');
function create({root,configuration,mode='ssh'}) {
  const settings = configuration.settings,
    folder =
      mode === 'local'
        ? path.join(root, 'data/state/local-input-service')
        : settings.deviceFolder;
  async function run(action, body, onEvent, timeout = 30000) {
    // The installed input protocol is independent of the launcher release.
    // Only explicit installation needs the PC's deployment package.
    const files = action === 'ensure' ? await bundle(root) : undefined;
    return invoke(
      require('../../../core/platform/python.cjs').command(root),
      [path.join(__dirname, 'python-bridge.py'), mode, action],
      {
        input: {
          root,
          files,
          folder,
          hidDevice: settings.hidDevice,
          armAddress: settings.armAddress || settings.consoleHost,
          body,
        },
        env: { ...process.env, ...configuration.env },
        onEvent,
        timeout,
      },
    );
  }
  async function submit(body, onEvent = () => {}) {
    const status=JSON.parse(await run('status'));
    if(!status.moduleReady)throw Error('Install the ARM input package using First-time setup');
    await run('enqueue', body);
    let result;
    await run(
      'follow',
      { requestId: body.requestId },
      (value) => {
        if (value.done) result = value.done;
        else onEvent(value);
      },
      7300000,
    );
    if (!result?.ok)
      throw Error(
        result?.error ||
          (result?.stopped
            ? 'Input job cancelled'
            : 'Input job completion is unconfirmed'),
      );
    return result;
  }
  return {
    adapter: configuration.inputAdapter,
    async install() {
      return JSON.parse(await run('ensure', undefined, undefined, 60000));
    },
    async inspect() {
      try {
        return JSON.parse(await run('status'));
      } catch (e) {
        return {
          reachable: false,
          moduleReady: false,
          keyboard: false,
          setupRequired: true,
          error: e.message,
        };
      }
    },
    performPlan: async (value, onEvent) =>
      submit(await plan(root, value), onEvent),
    async perform() {
      throw Error('Input actions must be supplied as an engine-provided typed plan');
    },
    async halt(id) {
      const result = JSON.parse(await run('halt', { requestId: id }));
      if (!result.stopped) throw Error('Input cancellation is unconfirmed');
      return result;
    },
  };
}
module.exports={create};
