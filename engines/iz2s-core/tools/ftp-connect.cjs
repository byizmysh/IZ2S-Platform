'use strict';
const assert = require('node:assert/strict');
const {createHmac} = require('node:crypto');
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const {DEFAULT_FOLDER} = require('./pc-link.cjs');

const FTP_PORT = 1425;
const PREVIOUS_FTP_PORT = 1337;

function reachable(host, port) {
  return new Promise(resolve => {
    const socket = net.connect({host, port});
    const finish = ok => { socket.destroy(); resolve(ok); };
    socket.setTimeout(650, () => finish(false));
    socket.once('connect', () => finish(true));
    socket.once('error', () => finish(false));
  });
}

async function credentials(file=path.join(DEFAULT_FOLDER, 'pc-link.key'), host){
  const key=await fs.readFile(file);
  assert.equal(key.length,32,'Expected the existing switch pairing key');
  if(!host){
    const live=await fetch('http://127.0.0.1:39525/status',{signal:AbortSignal.timeout(700)})
      .then(r=>r.ok?r.json():null).catch(()=>null);
    const config=await fs.readFile(path.join(path.dirname(file),'connection.json'),'utf8').then(JSON.parse)
      .catch(error=>{if(error.code==='ENOENT')return {};throw error;});
    host=live?.online&&live.service?.root===path.resolve(__dirname,'../../..')&&net.isIPv4(live.consoleAddress)
      ?live.consoleAddress:config.consoleIp??'10.42.0.20';
  }
  assert(net.isIPv4(host),'Expected the Switch IPv4 address');
  return {
    host, port:FTP_PORT, user:'iz2s',
    password:createHmac('sha256',key).update('IZ2S FTP password v1').digest('hex'),
  };
}

if (require.main === module) {
  const args = process.argv.slice(2);
  const option = name => args.includes(name) ? args[args.indexOf(name) + 1] : undefined;
  credentials(undefined, option('--host')).then(async value => {
    if (args.includes('--exchange')) value.port = 2121;
    else if (args.includes('--port')) {
      const port = Number(option('--port'));
      assert([FTP_PORT, PREVIOUS_FTP_PORT].includes(port), 'Invalid paired IZ2S FTP port');
      value.port = port;
    } else if (!await reachable(value.host, FTP_PORT) && await reachable(value.host, PREVIOUS_FTP_PORT)) {
      value.port = PREVIOUS_FTP_PORT;
    }
    process.stdout.write(JSON.stringify(value, null, 2) + '\n');
  }).catch(error => {console.error(error.message);process.exitCode=1;});
}
module.exports={credentials, FTP_PORT};
