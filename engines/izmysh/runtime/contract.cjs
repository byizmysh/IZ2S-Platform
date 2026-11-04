'use strict';
const path = require('node:path');
const description = require('../engine.json');
function options(value = {}) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw Error('Expected engine loader settings');
  const result = {};
  for (const key of ['rpcMode', 'fsRpcMode']) {
    if (value[key] !== undefined && typeof value[key] !== 'boolean') throw Error(key + ' must be a checkbox value');
    result[key] = value[key] === true;
  }
  return result;
}
function endpoint(host, value = description.consolePort) {
  const port = Number(value);
  if (!require('node:net').isIPv4(host || '')) throw Error('Choose the IPv4 address reachable by the console');
  if (!Number.isInteger(port) || port < 1024 || port > 65535) throw Error('Port must be 1024..65535');
  return {host, port, command:'V=START(' + [port, ...host.split('.')].join(',') + ')'};
}
function bootstrap() {
  return {file:path.resolve(__dirname, '..', description.bootstrap.file), saveName:description.bootstrap.saveName,
    sourceSlot:0, arity:description.bootstrap.arity, loaderId:description.id, recovery:false};
}
function verify() {
  const fs = require('node:fs'), root = path.resolve(__dirname,'..');
  const missing = require('./required-files.json').filter(name => {
    try {const stat=fs.statSync(path.join(root,name));return !stat.isFile()||stat.size===0;} catch {return true;}
  });
  if (missing.length) throw Error('IZ2S Core host runtime is incomplete; no console commands were sent. Missing: '+missing.join(', '));
  return require('../upstream/engine/check-runtime.cjs').verify();
}
module.exports = {options, endpoint, bootstrap, verify};
