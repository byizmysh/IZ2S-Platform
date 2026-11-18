const fs = require('node:fs/promises'),
  path = require('node:path'),
  os = require('node:os');
const { readJson, saveJson } = require('./ledger.cjs');
const network = require('./network-discovery.cjs');
const ids = ['server', 'arm', 'lan'],
  modes = ['lan', 'wifi'];
const modeName = (mode) => (mode === 'wifi' ? 'Wi-Fi' : 'LAN');
function address(value) {
  if (!network.privateIP(value))
    throw Error('Use a private local IPv4 address');
  return value;
}
async function createConnections({
  root,
  settings,
  run,
  interfaces = () => os.networkInterfaces(),
  isWireless = require('../platform/network.cjs').isWireless,
  isTunnel = require('../platform/network.cjs').isTunnel,
  routeSource = require('../platform/network.cjs').routeSource,
  platform = process.platform,
  windowsAdapterModes = require('../platform/network.cjs').windowsAdapterModes,
  networkEnabled = !!run,
  discover = network.discover,
}) {
  const file = path.join(root, 'data/state/connections.json'),historyFile=path.join(root,'data/state/console-addresses.json');
  const previousPairingFile = path.join(root, 'data/log/pc-link/connection.json');
  const storedHistory=await readJson(historyFile,{});
  let consoleHistory = [], historySave = Promise.resolve(), historyInFlight = null;
  let historyRevision = 0, savedHistoryRevision = 0, historyRetryAt = 0;
  for(const row of Array.isArray(storedHistory.observations)?storedHistory.observations:[]){if(!network.privateIP(row?.address)||!modes.includes(row.mode)||!Number.isFinite(Date.parse(row.verifiedAt)))continue;consoleHistory=consoleHistory.filter(old=>old.mode!==row.mode&&old.address!==row.address);consoleHistory.push({address:row.address,mode:row.mode,verifiedAt:row.verifiedAt});}
  const saved = await readJson(file, {}),
    preferences = {};
  const defaults = {
    server: settings.consoleHost,
    arm: settings.armAddress,
    lan: settings.consoleAddress,
  };
  let hosts = [],
    peers = [],
    errors = [],
    consolePaths = [],
    activeConsoleMode = null;
  let defaultAddress = '', consoleRouteAddress = '', previousPairedConsole = '',
    checking = null;
  for (const id of ids) {
    const old = saved[id] || {},
      mode = modes.includes(old.mode) ? old.mode : 'lan';
    // Loopback is the old empty/default value, not a usable address for Switch.
    const initial = network.privateIP(defaults[id]) ? defaults[id] : '';
    preferences[id] = {
      mode,
      automaticMode:
        old.automaticMode ??
        !network.privateIP(old.addresses?.[mode] || initial),
      addresses: {
        lan: initial,
        wifi:
          id === 'lan' && network.privateIP(settings.consoleWifiAddress)
            ? settings.consoleWifiAddress
            : '',
      },
    };
    if (id === 'server')
      preferences[id].modes = Array.isArray(old.modes) && old.modes.length &&
        old.modes.every(value => modes.includes(value)) ? [...new Set(old.modes)] : [...modes];
    if (id === 'lan')
      preferences[id].modes =
        Array.isArray(old.modes) &&
        old.modes.length &&
        old.modes.every((m) => modes.includes(m))
          ? [...new Set(old.modes)]
          : saved[id]
            ? [mode]
            : [...modes];
    for (const mode of modes)
      if (Object.hasOwn(old.addresses || {}, mode))
        preferences[id].addresses[mode] =
          old.addresses[mode] &&
          !(id === 'server' && old.addresses[mode] === '127.0.0.1')
            ? address(old.addresses[mode])
            : '';
  }
  function automaticHost(mode) {
    const candidates = hosts.filter((h) => h.mode === mode);
    const target = consoleTarget();
    if (target) {
      const routed = candidates.find(h => h.address === consoleRouteAddress);
      if (routed) return routed;
      const matching = candidates.filter(h => network.sameSubnet(h.address, target, h.netmask));
      if (matching.length === 1) return matching[0];
    }
    return candidates.length === 1 ? candidates[0] : undefined;
  }
  function consoleTarget(includePreviousPairing = true) {
    const mode = selectedMode('lan');
    return preferences.lan.addresses[mode] ||
      consolePaths.find(path => path.ready && path.mode === mode)?.address ||
      peers.find(peer => peer.kind === 'console' && peer.mode === mode)?.address ||
      consoleHistory.find(row => row.mode === mode)?.address || (includePreviousPairing ? previousPairedConsole : '');
  }
  function selectedMode(id) {
    const pref = preferences[id];
    if (id === 'lan')
      return pref.modes.includes(activeConsoleMode)
        ? activeConsoleMode
        : pref.mode;
    if (id === 'server' && pref.modes.length === 1) return pref.modes[0];
    if (!pref.automaticMode || pref.addresses[pref.mode]) return pref.mode;
    const choices = (id === 'server' ? hosts : peers.filter(p => p.kind === 'input'))
      .filter(row => modes.includes(row.mode));
    if (id === 'server') {
      // The input keyboard can be on Wi-Fi while Switch 2 uses LAN.
      // Select the server interface from the console path, not the keyboard.
      const target = consoleTarget();
      const matching = choices.filter(h => target && network.sameSubnet(h.address, target, h.netmask));
      const routed = choices.find(h => target && h.address === consoleRouteAddress);
      if (routed || matching.length === 1) return (routed || matching[0]).mode;
      const consoleMode = selectedMode('lan');
      if (choices.some((h) => h.mode === consoleMode)) return consoleMode;
      const defaultHost = choices.find(h => h.address === defaultAddress);
      if (defaultHost) return defaultHost.mode;
    }
    return (
      choices.find((p) => p.mode === pref.mode)?.mode ||
      choices[0]?.mode ||
      pref.mode
    );
  }
  function modeAddress(id, mode) {
    if (id === 'arm') {
      const input = peers.filter(p => p.kind === 'input');
      const matching = input.filter(p => p.mode === mode);
      return preferences.arm.addresses[mode] ||
        (new Set(input.map(p => p.id)).size === 1 && matching.length === 1 ? matching[0].address : '');
    }
    return (
      preferences[id].addresses[mode] ||
      (id === 'server'
        ? automaticHost(mode)?.address
        : peers.find(
            (p) =>
              p.kind === 'console' &&
              p.mode === mode,
          )?.address) ||
      (id === 'lan' && mode===preferences.lan.mode ? peers.find(p => p.kind === 'console' && p.mode === 'unknown')?.address : '') ||
      ''
    );
  }
  function needsPrimaryServerChoice(mode) {
    const pref = preferences.server;
    if (pref.addresses[mode]) return false;
    const candidates = hosts.filter(host => host.mode === 'unknown' || pref.modes.includes(host.mode));
    const target = consoleTarget();
    if (target) {
      const routed = candidates.find(host => host.address === consoleRouteAddress);
      if (routed) return routed.mode === 'unknown';
      const matching = candidates.filter(host => network.sameSubnet(host.address,target,host.netmask));
      if (matching.length > 1) return true;
      if (matching.length === 1 && matching[0].mode === 'unknown') return true;
    }
    return !target && preferences.lan.modes.length > 1 && pref.modes.length > 1 && candidates.length > 1;
  }
  const effective = (id) => {
    const mode = selectedMode(id);
    return id === 'server' && needsPrimaryServerChoice(mode) ? '' : modeAddress(id, mode);
  };
  function serverPaths() {
    return preferences.server.modes.map(mode => {
      const ip = modeAddress('server', mode);
      return {mode, address: hosts.some(host => host.address === ip &&
        (host.mode === mode || (host.mode === 'unknown' && preferences.server.addresses[mode] === ip))) ? ip : ''};
    });
  }
  function sync() {
    settings.consoleHost = effective('server');
    settings.armAddress = effective('arm');
    settings.consoleAddress = effective('lan');
    settings.consoleWifiAddress = modeAddress('lan', 'wifi');
  }
  function candidates() {
    return modes
      .filter((m) => preferences.lan.modes.includes(m))
      .map((mode) => {
        const ip = modeAddress('lan', mode);
        return {
          mode,
          address: ip,
          observedMode: peers.find((p) => p.address === ip && modes.includes(p.mode))?.mode || null,
          automatic: !preferences.lan.addresses[mode],
          lastKnownAddress:consoleHistory.find(row=>row.mode===mode)?.address||'',
          lastVerifiedAt:consoleHistory.find(row=>row.mode===mode)?.verifiedAt||null,
        };
      });
  }
  function reportPaths(paths) {
    consolePaths = paths;
    const stale = new Set(paths.filter(p => p.automatic && p.previousAddress && !paths.some(other => other.ready && other.address === p.previousAddress)).map(p => p.previousAddress));
    peers = peers.filter(p => p.kind !== 'console' || !stale.has(p.address));
    activeConsoleMode = paths.find((p) => p.ready && p.address && p.observedMode === p.mode)?.mode || null;
    sync();
  }
  async function refresh() {
    if (checking) return checking;
    checking = (async () => {
      const adapterModes = platform === 'win32' && networkEnabled ? await windowsAdapterModes() : null;
      const availableHosts = (
        await Promise.all(
          Object.entries(interfaces()).map(async ([name, list]) => {
            if(await isTunnel(name))return [];
            const mode = platform === 'win32' ? 'unknown' : (await isWireless(name)) ? 'wifi' : 'lan';
            return (list || [])
              .filter(
                (a) =>
                  (a.family === 'IPv4' || a.family === 4) &&
                  !a.internal &&
                  network.privateIP(a.address) &&
                  !/^(tailscale|docker|podman|veth|virbr|vnet|vmnet|lxc|br-|tun|wg)/i.test(
                    name,
                  ),
              )
              .map((a) => ({
                address: a.address,
                netmask: a.netmask,
                name,
                mode: adapterModes?.get(a.address) || mode,
              })).filter(a => a.mode !== 'excluded');
          }),
        )
      ).flat();
      let nextDefault = '', nextRoute = '', nextPairedConsole = '', nextErrors = [], inputPeers = [];
      if (networkEnabled) {
        try {
          let target = consoleTarget(false);
          let hint;
          if (!target && preferences.lan.modes.length === modes.length &&
              !modes.some(mode => preferences.lan.addresses[mode])) {
            const savedPair = await readJson(previousPairingFile, {}).catch(() => null);
            // A previous pairing can pick a local adapter. It does not tell us
            // that Switch is still reachable or how Switch itself is connected.
            if (network.privateIP(savedPair?.consoleIp) && savedPair.consoleIp !== savedPair.pcIp &&
                availableHosts.some(host => host.address === savedPair.pcIp && preferences.server.modes.includes(host.mode) &&
                  network.sameSubnet(host.address, savedPair.consoleIp, host.netmask))) {
              hint = savedPair;
              target = hint.consoleIp;
            }
          }
          const source = await routeSource(target || '192.0.2.1');
          if (availableHosts.some(host => host.address === source)) {
            if (hint && source === hint.pcIp) {
              nextPairedConsole = hint.consoleIp;
              nextRoute = source;
            } else if (target && !hint) nextRoute = source;
            else if (!target) nextDefault = source;
          }
        } catch {
          // A directly connected LAN can work without a default route.
        }
        try {
          const result = await discover({
            hosts: availableHosts,
            targets: modes
              .map((m) => preferences.arm.addresses[m])
              .filter(Boolean),
          });
          inputPeers = result.peers;
          nextErrors = result.errors || [];
        } catch (e) {
          nextErrors = [e.code || 'Discovery unavailable'];
        }
      }
      // The UI can request a snapshot while route lookup and discovery await I/O.
      hosts = availableHosts;
      defaultAddress = nextDefault;
      consoleRouteAddress = nextRoute;
      previousPairedConsole = nextPairedConsole;
      errors = nextErrors;
      if (networkEnabled) peers = [...peers.filter(p => p.kind === 'console'), ...inputPeers];
      sync();
      return snapshot();
    })();
    try {
      return await checking;
    } finally {
      checking = null;
    }
  }
  function snapshot() {
    return {
      dualConsoleSupported: true,
      inventoryAvailable: peers.some((p) => p.kind === 'input'),
      discovery: {
        enabled: networkEnabled,
        port: network.PORT,
        errors,
        ambiguous:
          new Set(peers.filter((p) => p.kind === 'input').map((p) => p.id))
            .size > 1,
      },
      devices: Object.fromEntries(
        ids.map((id) => {
          const pref = preferences[id],
            mode = selectedMode(id),
            value = effective(id),
            observed = (id === 'server' ? hosts : peers).find(p => p.address === value)?.mode;
          const options = id === 'server' ? hosts : peers.filter(p => p.kind === (id === 'arm' ? 'input' : 'console'));
          return [
            id,
            {
              mode,
              address: value,
              automatic: !pref.addresses[mode],
              addresses: { ...pref.addresses },
              options,
              needsChoice: !value && (id === 'server'
                ? needsPrimaryServerChoice(mode) || options.some(item => item.mode === 'unknown') || options.filter(item => item.mode === mode).length > 1
                : id === 'arm' && options.length > 0),
              observedMode: modes.includes(observed) ? observed : null,
              ...(id === 'server' ? {modes: [...pref.modes], paths: serverPaths()} : {}),
              ...(id === 'lan'
                ? {
                    modes: [...pref.modes],
                    activeMode: activeConsoleMode,
                    paths: consolePaths,
                    candidates: candidates(),
                    lastKnownAddresses:Object.fromEntries(consoleHistory.map(row=>[row.mode,{address:row.address,verifiedAt:row.verifiedAt,source:'last-verified-session',current:false}])),
                  }
                : {}),
            },
          ];
        }),
      ),
    };
  }
  async function update({ id, mode, modes: selected, ip, automatic }) {
    if (!ids.includes(id) || (mode !== undefined && !modes.includes(mode)))
      throw Error('Choose a device and LAN or Wi-Fi');
    if (automatic !== undefined && typeof automatic !== 'boolean')
      throw Error('Invalid automatic connection setting');
    if (
      selected !== undefined &&
      (!['lan', 'server'].includes(id) ||
        !Array.isArray(selected) ||
        !selected.length ||
        selected.length > 2 ||
        new Set(selected).size !== selected.length ||
        selected.some((m) => !modes.includes(m)))
    )
      throw Error('Keep at least one network connection enabled');
    if ((selected === undefined && !mode) || (ip !== undefined && !mode))
      throw Error('Choose LAN or Wi-Fi for this address');
    if (ip !== undefined && ip !== '') address(ip);
    const next = JSON.parse(JSON.stringify(preferences));
    if (id === 'lan') {
      next[id].modes = selected
        ? modes.filter((m) => selected.includes(m))
        : [mode];
      next[id].mode = next[id].modes.includes(next[id].mode)
        ? next[id].mode
        : next[id].modes[0];
    } else if (id === 'server' && selected) {
      next.server.modes = modes.filter(value => selected.includes(value));
      if (!next.server.modes.includes(next.server.mode)) next.server.mode = next.server.modes[0];
      if (next.server.modes.length === 2) next.server.automaticMode = true;
    } else {
      next[id].mode = mode;
      if (id === 'server' && !next.server.modes.includes(mode)) next.server.modes = [mode];
      next[id].automaticMode = automatic ?? (ip === '' ? true : false);
    }
    if (automatic === true && id === 'server') next.server.addresses[mode] = '';
    else if (automatic === true) next[id].addresses = { lan: '', wifi: '' };
    else if (ip !== undefined) next[id].addresses[mode] = ip;
    await saveJson(file, next);
    Object.assign(preferences, next);
    consolePaths = [];
    activeConsoleMode = null;
    consoleRouteAddress = previousPairedConsole = '';
    sync();
    return snapshot();
  }
  function saveHistory(force = false) {
    if (historyInFlight) return historyInFlight;
    if (historyRevision === savedHistoryRevision || (!force && Date.now() < historyRetryAt))
      return historySave;
    const pending = (async () => {
      do {
        const revision = historyRevision;
        const record = {version: 1, observations: consoleHistory.map(row => ({...row}))};
        try {
          await saveJson(historyFile, record);
        } catch (error) {
          historyRetryAt = Date.now() + 10000;
          throw error;
        }
        savedHistoryRevision = revision;
        historyRetryAt = 0;
        // Coalesce observations received during the write into the next save.
      } while (savedHistoryRevision !== historyRevision);
    })().finally(() => {
      if (historyInFlight === pending) historyInFlight = null;
    });
    historyInFlight = historySave = pending;
    // Observations may be fire-and-forget; callers/flush still see the rejection.
    pending.catch(() => {});
    return pending;
  }
  // Called only after an engine has qualified the incoming native session.
  function observeConsole(ip, mode = 'unknown') {
    if (!network.privateIP(ip)) return;
    if (!modes.includes(mode)) mode = 'unknown';
    peers = [
      ...peers.filter((p) => p.kind !== 'console' || (p.mode !== mode && p.address !== ip)),
      {
        kind: 'console',
        name: 'Switch · connected session',
        address: ip,
        mode,
        source: 'session',
      },
    ];
    activeConsoleMode = modes.includes(mode) ? mode : null;
    if(modes.includes(mode)){
      const before=consoleHistory.find(row=>row.mode===mode&&row.address===ip);
      if(!before){consoleHistory=consoleHistory.filter(row=>row.mode!==mode&&row.address!==ip);consoleHistory.push({address:ip,mode,verifiedAt:new Date().toISOString()});historyRevision++;}
    }
    sync();return saveHistory();
  }
  sync();
  return {
    refresh,
    snapshot,
    update,
    candidates,
    reportPaths,
    observeConsole,
    flush:()=>saveHistory(true),
    mode: selectedMode,
    observed: (id) => snapshot().devices[id].observedMode,
  };
}
module.exports = { createConnections, modeName, address };
