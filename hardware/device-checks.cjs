const fs = require('node:fs/promises'),
  path = require('node:path'),
  net = require('node:net'),
  os = require('node:os'),
  { spawn } = require('node:child_process');
const { verifyOriginals } = require('../core/session/integrity.cjs');
function invoke(
  file,
  args,
  { cwd, env = process.env, timeout = 18000, onLine } = {},
) {
  return new Promise((resolve, reject) => {
    const child = spawn(file, args, {
      cwd,
      env,
      windowsHide: true,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let stdout = '',
      stderr = '',
      pending = '',
      ended = false;
    const settle = (error, value) => {
      if (ended) return;
      ended = true;
      clearTimeout(deadline);
      error ? reject(error) : resolve(value);
    };
    const deadline = setTimeout(() => {
      child.kill('SIGTERM');
      settle(Error('Device operation timed out'));
    }, timeout);
    child.stdout.on('data', (b) => {
      stdout += b;
      pending += b;
      let at;
      while ((at = pending.indexOf('\n')) >= 0) {
        const line = pending.slice(0, at);
        pending = pending.slice(at + 1);
        try {
          onLine?.(line);
        } catch (e) {
          child.kill('SIGTERM');
          settle(e);
        }
      }
    });
    child.stderr.on('data', (b) => (stderr = (stderr + b).slice(-12000)));
    child.once('error', (e) => settle(e));
    child.once('close', (code, signal) =>
      settle(
        code === 0
          ? null
          : Error(
              stderr.trim() ||
                stdout.trim() ||
                'Device helper stopped (' + (signal || code) + ')',
            ),
        { stdout, stderr },
      ),
    );
  });
}
function createInputLink(root, settings = {}) {
  return require('./transports/index.cjs').create({root, configuration:{
    id:'pc-arm', inputAdapter:'ssh-gadget', settings,
  }});
}

function availablePort(address, port) {
  return new Promise((resolve) => {
    const s = net.createServer();
    s.once('error', () => resolve(false));
    s.listen(port, address, () => s.close(() => resolve(true)));
  });
}
async function inspectConsolePaths({
  connections,
  settings,
  hostInterface,
  routeLookup = require('../core/platform/network.cjs').routeTo,
  mode,
}) {
  const { modeName } = require('../core/session/connections.cjs');
  const candidates = connections?.candidates?.() || [
    {
      mode: mode('lan'),
      address: settings.consoleAddress,
      observedMode: connections?.observed('lan'),
    },
  ];
  const serverPaths = connections?.snapshot().devices?.server?.paths || [];
  const paths = await Promise.all(
    candidates.map(async (candidate) => {
      const { address, mode, observedMode } = candidate,
        start = Date.now();
      try {
        if (
          !address &&
          candidate.automatic
        ) {
          if (!hostInterface || !settings.consoleHost)
            throw Error(
              'No local server adapter for automatic console connection',
            );
          return {
            ...candidate,
            ready: true,
            pending: true,
            latencyMs: null,
            checkedAt: Date.now(),
            durationMs: Date.now() - start,
          };
        }
        if (!address)
          throw Error('Set the console ' + modeName(mode) + ' address');
        if (observedMode && observedMode !== mode)
          throw Error(
            'Console is on ' +
              modeName(observedMode) +
              ' · switch the connection or connect via ' +
              modeName(mode),
          );
        const route = await routeLookup(address);
        if (!route?.dev || route.dev === 'lo') throw Error('No route to the console');
        if (route.dev !== hostInterface && !serverPaths.some(path => path.address && path.address === route.address))
          throw Error('Server and console are on different networks');
        return {
          ...candidate,
          ready: true,
          pending: true,
          verification: 'route-only',
          localAddress: route.address || settings.consoleHost,
          latencyMs: null,
          checkedAt: Date.now(),
          durationMs: Date.now() - start,
        };
      } catch (error) {
        // Auto is an inbound connection, not a permanent pin to the previous
        // session's IP. A dock/portable transition can change that address.
        if (candidate.automatic && hostInterface && settings.consoleHost) {
          return {...candidate,address:'',observedMode:null,previousAddress:address,ready:true,pending:true,latencyMs:null,reason:'Awaiting the next console connection',checkedAt:Date.now(),durationMs:Date.now()-start};
        }
        return {
          ...candidate,
          ready: false,
          latencyMs: null,
          error: error.message,
          checkedAt: Date.now(),
        };
      }
    }),
  );
  connections?.reportPaths?.(paths);
  const active = paths.find((p) => p.ready&&!p.pending)||paths.find((p)=>p.ready);
  if (!active)
    throw Error(
      paths
        .map((p) => (paths.length > 1 ? modeName(p.mode) + ': ' : '') + p.error)
        .join(' · '),
    );
  return {
    ...active,
    detail: active.pending
      ? 'Network ready · waiting for Switch to connect to ' + (active.localAddress || settings.consoleHost) +
        (active.address ? ' · Switch ' + active.address : '')
      : 'Console connection verified · ' + modeName(active.mode) + ' · ' + active.address,
    paths,
  };
}
function createDeviceChecks({
  root,
  settings,
  node,
  arm,
  connections,
  ownsListener = () => false,
  requiresArmAddress = true,
  inputName = 'ARM PC',
  inputAddress = settings.armAddress || '',
  inputSetup = '',
  run = invoke,
  interfaces = () => os.networkInterfaces(),
  portAvailable = availablePort,
  routeLookup = address => require('../core/platform/network.cjs').routeTo(address, {interfaces}),
}) {
  let cachedTools;
  async function inspect() {
    await connections?.refresh();
    const { modeName } = require('../core/session/connections.cjs'),
      mode = (id) => connections?.mode(id) || 'lan';
    const serverNeedsChoice = !!connections?.snapshot().devices?.server?.needsChoice;
    const hostInterface = Object.entries(interfaces()).find(([, list]) =>
      list.some((a) => a.address === settings.consoleHost),
    )?.[0];
    const rows = [
      { id: 'server', label: 'Server', ready: false, detail: 'Checking host' },
      {
        id: 'arm',
        label: 'ARM PC · input service',
        ready: false,
        detail: 'Checking input service',
      },
      {
        id: 'usb',
        label: 'ARM Type C - Switch',
        ready: false,
        detail: 'Waiting for USB status',
      },
      {
        id: 'lan',
        label: 'LAN · console',
        ready: false,
        detail: 'Checking console network',
      },
    ];
    const tasks = await Promise.allSettled([
      (async () => {
        await verifyOriginals(root);
        if (serverNeedsChoice) return null;
        if (!hostInterface)
          throw Error(
            'No active ' + modeName(mode('server')) + ' address on this PC',
          );
        if (
          connections?.observed('server') &&
          connections.observed('server') !== mode('server')
        )
          throw Error(
            'This PC address uses ' + modeName(connections.observed('server')),
          );
        if (
          !ownsListener() &&
          !(await portAvailable(settings.consoleHost, settings.consolePort))
        )
          throw Error('Console port is already in use');
        if (!cachedTools) {
          await run(node, ['hardware/dependencies.cjs', '--host-only', ...(requiresArmAddress ? ['--ssh'] : [])], {
            cwd: root,
            env: { ...process.env, NODE: node },
            timeout: 20000,
          });
          cachedTools = true;
        }
        return (
          modeName(mode('server')) +
          ' · ' +
          settings.consoleHost +
          ' · ' +
          hostInterface
        );
      })(),
      inputSetup ? null : (async () => {
        if (requiresArmAddress && !settings.armAddress && connections)
          throw Error(
            connections.snapshot().devices.arm.needsChoice
              ? 'Choose the address of your ARM input device.'
              : 'Set the ARM PC ' +
                  modeName(mode('arm')) +
                  ' address, or install its IZ2S discovery service',
          );
        return arm.inspect();
      })(),
      serverNeedsChoice ? null : inspectConsolePaths({ connections, settings, hostInterface, routeLookup, mode }),
    ]);
    const host = tasks[0];
    rows[0].ready = host.status === 'fulfilled' && !serverNeedsChoice;
    rows[0].detail =
      host.status === 'fulfilled'
        ? (serverNeedsChoice ? 'Choose the PC network adapter that can reach Switch 2.' : host.value)
        : (
            host.reason.message
              .split('\n')
              .find((s) => s.startsWith('NEED ')) || host.reason.message
          ).replace(/^NEED\s+/, '');
    if (host.status === 'fulfilled' && serverNeedsChoice) rows[0].status = 'setup';
    const peer = tasks[1];
    if (inputSetup) {
      rows[1].status = 'setup';
      rows[1].detail = inputSetup;
      rows[2].status = 'waiting';
      rows[2].detail = 'Waiting for ' + inputName + ' Wi-Fi.';
    } else if (peer.status === 'fulfilled') {
      const h = peer.value,
        observed = h.connectionMode || connections?.observed('arm'),
        mismatch = observed && observed !== mode('arm'),
        switchKeyboard = h.service === 'switch-wifi';
      rows[1].error = h.error;
      rows[1].bootId = h.bootId;
      rows[1].firmwareVersion = h.firmwareVersion;
      if (h.usb) rows[2].usb = h.usb;
      if (h.pending) rows[1].status = 'waiting';
      rows[1].ready = !!(h.reachable && h.moduleReady && !mismatch);
      rows[1].detail = !h.reachable
        ? switchKeyboard ? 'Switch 1 is unavailable. Keep IZ2S SB4 Keyboard open on the same local network; its address is detected automatically.' : inputName + ' connection failed: ' + (h.error || 'Input service did not respond')
        : !h.moduleReady
          ? (h.error || 'Install or update the ' + inputName + ' input package')
          : mismatch
        ? inputName + ' is on ' + modeName(observed) + ' · select that connection'
        : (h.installed ? 'Input module installed' : 'Input module ready') +
          ' · ' +
          modeName(mode('arm')) +
          ' · ' +
          (h.address || inputAddress);
      rows[2].ready = !!h.reachable && !!h.keyboard && !h.keyboardBusy;
      if (!h.reachable || h.keyboardBusy || (switchKeyboard && !rows[2].ready)) rows[2].status = 'waiting';
      rows[2].detail = !h.reachable
        ? switchKeyboard ? 'Waiting for Switch 1 Wi-Fi.' : 'USB state unavailable until ' + inputName + ' reconnects'
        : h.keyboardBusy
        ? 'Input device is busy'
        : h.usb ? h.usb.detail
        : h.keyboard
          ? 'USB host configured'
          : switchKeyboard ? 'Connect the USB data cable to the Switch 2 dock.' : h.usbState || 'Connect the USB data cable';
    } else {
      rows[1].detail = /^(?:Set the ARM|Choose the address of your ARM)/.test(peer.reason.message)
        ? peer.reason.message
        : inputName + ' is not reachable' + (inputAddress ? ' · ' + inputAddress : '');
      rows[2].detail = 'Waiting for ' + inputName;
      rows[2].status = 'waiting';
    }
    const link = tasks[2];
    if (serverNeedsChoice) {
      rows[3].status = 'waiting';
      rows[3].detail = 'Waiting for a PC network adapter.';
    } else if (link.status === 'fulfilled') {
      rows[3].ready = true;
      rows[3].detail = link.value.detail;
      rows[3].latencyMs = link.value.latencyMs;
      rows[3].pending = !!link.value.pending;
    } else rows[3].detail = link.reason.message;
    return {
      at: new Date().toISOString(),
      ready: rows.every((r) => r.ready),
      items: rows,
    };
  }
  return { inspect };
}
module.exports = {
  invoke,
  availablePort,
  createInputLink,
  inspectConsolePaths,
  createDeviceChecks,
};
