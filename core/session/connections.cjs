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
  routeSource = require('../platform/network.cjs').routeSource,
  networkEnabled = !!run,
  discover = network.discover,
}) {
  const file = path.join(root, 'data/state/connections.json'),historyFile=path.join(root,'data/state/console-addresses.json');
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
  let defaultDevice = null,
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
    const target =
      preferences.lan.addresses[selectedMode('lan')] ||
      modeAddress('arm', selectedMode('arm'));
    return (
      candidates.find(
        (h) => target && network.sameSubnet(h.address, target, h.netmask),
      ) ||
      candidates.find((h) => h.name === defaultDevice) ||
      candidates[0]
    );
  }
  function selectedMode(id) {
    const pref = preferences[id];
    if (id === 'lan')
      return pref.modes.includes(activeConsoleMode)
        ? activeConsoleMode
        : pref.mode;
    if (!pref.automaticMode || pref.addresses[pref.mode]) return pref.mode;
    const choices =
      id === 'server' ? hosts : peers.filter((p) => p.kind === 'input');
    if (id === 'server') {
      const target =
        preferences.lan.addresses[selectedMode('lan')] ||
        modeAddress('arm', selectedMode('arm'));
      const matching = choices.find(
        (h) => target && network.sameSubnet(h.address, target, h.netmask),
      );
      if (matching) return matching.mode;
      if (choices.some((h) => h.name === defaultDevice))
        return choices.find((h) => h.name === defaultDevice).mode;
    }
    return (
      choices.find((p) => p.mode === pref.mode)?.mode ||
      choices[0]?.mode ||
      pref.mode
    );
  }
  function modeAddress(id, mode) {
    if (
      id === 'arm' &&
      !preferences[id].addresses[mode] &&
      new Set(peers.filter((p) => p.kind === 'input').map((p) => p.id)).size > 1
    )
      return '';
    return (
      preferences[id].addresses[mode] ||
      (id === 'server'
        ? automaticHost(mode)?.address
        : peers.find(
            (p) =>
              p.kind === (id === 'arm' ? 'input' : 'console') &&
              p.mode === mode,
          )?.address) ||
      (id === 'lan' && mode===preferences.lan.mode ? peers.find(p => p.kind === 'console' && p.mode === 'unknown')?.address : '') ||
      ''
    );
  }
  const effective = (id) => modeAddress(id, selectedMode(id));
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
      hosts = (
        await Promise.all(
          Object.entries(interfaces()).map(async ([name, list]) => {
            const mode = (await isWireless(name)) ? 'wifi' : 'lan';
            return list
              .filter(
                (a) =>
                  a.family === 'IPv4' &&
                  !a.internal &&
                  network.privateIP(a.address) &&
                  !/^(tailscale|docker|podman|veth|virbr|vnet|vmnet|lxc|br-|tun|wg)/.test(
                    name,
                  ),
              )
              .map((a) => ({
                address: a.address,
                netmask: a.netmask,
                name,
                mode,
              }));
          }),
        )
      ).flat();
      defaultDevice = null;
      errors = [];
      if (networkEnabled) {
        try {
          const target = preferences.lan.addresses[selectedMode('lan')] ||
            modeAddress('arm', selectedMode('arm')) || '192.0.2.1';
          const source = await routeSource(target);
          defaultDevice = hosts.find(host => host.address === source)?.name || null;
        } catch {
          // A directly connected LAN can work without a default route.
        }
        try {
          const result = await discover({
            hosts,
            targets: modes
              .map((m) => preferences.arm.addresses[m])
              .filter(Boolean),
          });
          peers = [
            ...peers.filter((p) => p.kind === 'console'),
            ...result.peers,
          ];
          errors = result.errors || [];
        } catch (e) {
          errors = [e.code || 'Discovery unavailable'];
          peers = peers.filter((p) => p.kind === 'console');
        }
      }
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
          return [
            id,
            {
              mode,
              address: value,
              automatic: !pref.addresses[mode],
              addresses: { ...pref.addresses },
              options:
                id === 'server'
                  ? hosts
                  : peers.filter(
                      (p) => p.kind === (id === 'arm' ? 'input' : 'console'),
                    ),
              observedMode: modes.includes(observed) ? observed : null,
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
      (id !== 'lan' ||
        !Array.isArray(selected) ||
        !selected.length ||
        selected.length > 2 ||
        new Set(selected).size !== selected.length ||
        selected.some((m) => !modes.includes(m)))
    )
      throw Error('Keep at least one console connection enabled');
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
    } else {
      next[id].mode = mode;
      next[id].automaticMode = automatic ?? (ip === '' ? true : false);
    }
    if (automatic === true) next[id].addresses = { lan: '', wifi: '' };
    else if (ip !== undefined) next[id].addresses[mode] = ip;
    await saveJson(file, next);
    Object.assign(preferences, next);
    consolePaths = [];
    activeConsoleMode = null;
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
