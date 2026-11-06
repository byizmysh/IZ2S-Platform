const fs = require('node:fs/promises'),
  path = require('node:path');
async function create({
  root,
  configuration,
  settings = configuration.settings,
  node,
  arm,
  connections,
  ownsListener,
  ...options
}) {
  arm ??= require('./transports/index.cjs').create({ root, configuration });
  let devices;
  for (const relative of ['hardware/device-checks.cjs'])
    if (
      await fs.access(path.join(root, relative)).then(
        () => true,
        () => false,
      )
    ) {
      devices = require(path.join(root, relative));
      break;
    }
  if (!devices) throw Error('IZlauncher common device checks are missing');
  // Input transport is not the server/console route. Cardputer Wi-Fi must not be
  // rejected because the old ARM address selector was previously set to Ethernet.
  const adaptedConnections = connections
    ? new Proxy(connections, {
        get(target, key) {
          if (key === 'observed')
            return (id) => (id === 'arm' ? null : target.observed(id));
          if (key === 'mode')
            return (id) =>
              id === 'arm'
                ? ['kprox-http','cardputer-wifi'].includes(configuration.inputAdapter)
                  ? 'wifi'
                  : target.mode(id)
                : target.mode(id);
          return typeof target[key] === 'function'
            ? target[key].bind(target)
            : target[key];
        },
      })
    : null;
  const base = devices.createDeviceChecks({
    root,
    settings,
    node,
    arm,
    connections: adaptedConnections,
    ownsListener,
    requiresArmAddress: configuration.inputAdapter === 'ssh-gadget',
    ...(configuration.inputAdapter === 'cardputer-wifi' ? {inputName:'Cardputer ADV', inputAddress:settings.cardputerEndpoint} : {}),
    ...options,
  });
  return {
    async inspect() {
      if (configuration.setupRequired)
        return require('./index.cjs').present(configuration,{
          at: new Date().toISOString(),
          configuration: configuration.id,
          setupRequired: true,
          requirements: configuration.requirements,
          ready: false,
          items: ['server', 'arm', 'usb', 'lan'].map((id) => ({
            id,
            ready: false,
            detail:
              id === 'arm'
                ? configuration.requirements.join(' ')
                : 'Finish hardware configuration to continue.',
          })),
        });
      const result = await base.inspect(),
        items = result.items.map((item) => ({ ...item }));
      const input = items.find((r) => r.id === 'arm'),
        usb = items.find((r) => r.id === 'usb');
      if (configuration.hardware?.combined) {
        if (input) input.label = 'ARM PC · local input service';
        if (usb) usb.label = 'ARM Type C - Switch';
      }
      if (['kprox-http','cardputer-wifi'].includes(configuration.inputAdapter)) {
        if (input) input.label = 'Cardputer ADV · Wi-Fi input';
        if (usb) {usb.label = 'USB · Cardputer ADV → Switch 2';if(usb.ready)usb.detail='USB connected · keyboard ready.';}
      }
      if (configuration.setupRequired && input) {
        input.ready = false;
        input.detail = configuration.requirements.join(' ');
      }
      return require('./index.cjs').present(configuration,{
        ...result,
        configuration: configuration.id,
        setupRequired: configuration.setupRequired,
        requirements: configuration.requirements,
        items,
        ready: items.every((r) => r.ready),
      });
    },
  };
}
module.exports = { create };
