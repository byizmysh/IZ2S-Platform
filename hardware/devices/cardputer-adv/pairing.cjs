'use strict';
const dns = require('node:dns/promises'), net = require('node:net'), path = require('node:path');
const fs = require('node:fs/promises');
const store = require('../../../core/services/device-pairings.cjs');
const normalize = require('./settings.cjs').endpoint;
const {privateIP} = require('../../../core/session/network-discovery.cjs');

async function resolveAll(host, lookup) {
  if (net.isIPv4(host)) return privateIP(host) ? [host] : [];
  let timer;
  try {
    const result = await Promise.race([lookup(host,{family:4,all:true}), new Promise((_,reject) => {
      timer = setTimeout(() => reject(Error('Name lookup timed out')), 1500);
    })]);
    return [...new Set([result].flat().map(row => row?.address).filter(privateIP))];
  } catch {
    return [];
  } finally {
    clearTimeout(timer);
  }
}

async function resolve(host, lookup = dns.lookup) {
  const addresses = await resolveAll(host, lookup);
  return addresses.length === 1 ? addresses[0] : null;
}

async function restore({root, profile = {}, fetchImpl = fetch, lookup = dns.lookup, registry} = {}) {
  let endpoint;
  try { endpoint = normalize(profile.cardputerEndpoint || 'http://iz2s-adv.local'); }
  catch { return {profile}; }
  const url = new URL(endpoint);
  const pinned = net.isIPv4(url.hostname);
  const resolved = await resolveAll(url.hostname, lookup);
  const saved = await store.candidates('cardputer',{...registry,root});
  const fallback = url.hostname === 'iz2s-adv.local' && !resolved.length;
  const savedAddress = row => privateIP(row.address) ? row.address : new URL(normalize(row.endpoint)).hostname;
  const known = saved.filter(row => {
    try {
      const prior = new URL(normalize(row.endpoint));
      return prior.port === url.port && (prior.origin === endpoint ||
        resolved.includes(prior.hostname) || resolved.includes(row.address) || (fallback && privateIP(savedAddress(row))));
    } catch { return false; }
  });
  const credentials = [];
  if (/^[a-f0-9]{64}$/.test(profile.cardputerToken || '')) {
    credentials.push(known.find(row => row.token === profile.cardputerToken) ||
      {token:profile.cardputerToken, source:'profile'});
  } else {
    try {
      const local = JSON.parse(await fs.readFile(path.join(root,'data/state/cardputer-pairing.json'),'utf8'));
      if (/^[a-f0-9]{64}$/.test(local.token || ''))
        credentials.push(known.find(row => row.token === local.token) || {...local,source:'local-pairing'});
    } catch {}
    credentials.push(...known);
  }
  const keys = [...new Map(credentials.map(row => [row.token,row])).values()];
  const targets = [...new Set([...resolved, ...(!pinned && resolved.length === 0 ? known.map(savedAddress) : [])])].filter(privateIP);
  const incomplete = targets.length > 8;
  let networkReachable = false;
  const verified = (await Promise.all(targets.slice(0,8).map(async address => {
    const candidate = new URL(endpoint);
    candidate.hostname = address;
    // Only an authenticated status at the observed address qualifies as a match.
    const matchingKeys = fallback && targets.length > 1 ? keys.filter(key => key.endpoint && savedAddress(key) === address) : keys;
    for (const key of matchingKeys.length ? matchingKeys : [null]) {
      try {
        const response = await fetchImpl(candidate.origin+'/v1/status',{
          headers:{...(key ? {authorization:'Bearer '+key.token} : {}),connection:'close'},
          signal:AbortSignal.timeout(1800),redirect:'error',
        });
        if (response.ok || response.status === 401 || response.status === 403) networkReachable = true;
        if (!key || !response.ok) continue;
        const text = await response.text();
        if (Buffer.byteLength(text) > 16384) continue;
        const state = JSON.parse(text);
        if (state.device !== 'iz2s-cardputer-adv' || state.apiVersion !== 2 ||
            !/^[a-f0-9]{32}$/.test(state.bootId || '') || (key.identity && key.identity !== state.usbSerial)) continue;
        return {address,endpoint:candidate.origin,token:key.token,identity:state.usbSerial,source:key.source};
      } catch {}
    }
    return null;
  }))).filter(Boolean);

  const discovery = {
    address: resolved.length === 1 ? resolved[0] : null,
    hostname:url.hostname, paired:false, networkReachable,
    pairingRequired:keys.length === 0 || (networkReachable && verified.length === 0),
    ambiguous:verified.length > 1, incomplete,
    candidates:verified.map(({address,endpoint}) => ({address,endpoint})),
  };
  if (incomplete || verified.length !== 1) return {profile,discovery};
  const chosen = verified[0];
  const next = {...profile,cardputerEndpoint:endpoint,cardputerToken:chosen.token,cardputerCapsLock:profile.cardputerCapsLock || 'off'};
  await store.remember({...chosen,kind:'cardputer',endpoint},registry).catch(() => {});
  await require('../../../core/services/json-write.cjs').write(path.join(root,'data/state/cardputer-pairing.json'),{token:chosen.token},{directoryMode:0o700});
  return {profile:next,discovery:{...discovery,address:chosen.address,endpoint:chosen.endpoint,paired:true,pairingRequired:false,automaticPairing:!pinned}};
}
module.exports = {restore,resolve};
