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
  const inputNames = {'cardputer-wifi': 'Cardputer ADV', 'switch-wifi': 'Switch 1'};
  const inputName = inputNames[configuration.inputAdapter];
  const inputSetup = inputName && configuration.setupRequired
    ? configuration.requirements.join(' ')
    : '';
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
                ? ['kprox-http','cardputer-wifi','switch-wifi'].includes(configuration.inputAdapter)
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
    ...(inputName && {
      inputName,
      inputAddress: configuration.inputAdapter === 'switch-wifi' ? settings.switchAddress : settings.cardputerEndpoint,
      inputSetup,
    }),
    ...options,
  });
  return {
    async inspect() {
      // Wi-Fi input settings gate the keyboard, but server and console routing are independent.
      if (configuration.setupRequired && !inputSetup)
        return require('./index.cjs').present(configuration,{
          at: new Date().toISOString(),
          configuration: configuration.id,
          setupRequired: true,
          requirements: configuration.requirements,
          ready: false,
          items: ['server', 'arm', 'usb', 'lan'].map((id) => ({
            id,
            ready: false,
            status: id === 'arm' ? 'setup' : 'waiting',
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
      if(input&&configuration.inputDiscovery){
        const found=configuration.inputDiscovery;
        input.address=found.address;input.pairingRequired=found.pairingRequired;
        input.networkReachable=found.networkReachable;
        if(found.ambiguous||found.incomplete){
          input.ready=false;input.status='setup';
          input.detail=found.ambiguous?'Several addresses match this input device. Choose the address shown on your device.':
            'Automatic detection was incomplete. Enter the address shown on your device.';
          if(usb){usb.ready=false;usb.status='waiting';usb.detail='Choose an input device address before checking USB.';}
        }else if(found.pairingRequired&&found.networkReachable){
          input.detail='The address responded, but pairing could not be verified. Check the address or prepare the pairing package.';
          if(usb)usb.detail='Pair the input device to read its USB status.';
        }
      }
      if (configuration.hardware?.combined) {
        if (input) input.label = 'ARM PC · local input service';
        if (usb) usb.label = 'ARM Type C - Switch';
      }
      if (['kprox-http','cardputer-wifi','switch-wifi'].includes(configuration.inputAdapter)) {
        if (input) input.label = (configuration.inputAdapter==='switch-wifi'?'Switch 1':'Cardputer ADV')+' · Wi-Fi input';
        if (usb) {usb.label = 'USB · '+(configuration.inputAdapter==='switch-wifi'?'Switch 1':'Cardputer ADV')+' → Switch 2';if(usb.ready)usb.detail='USB connected · keyboard ready.';}
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
