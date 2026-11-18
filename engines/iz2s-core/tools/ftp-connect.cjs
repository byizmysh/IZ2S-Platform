'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const net = require('node:net');
const {DEFAULT_FOLDER} = require('./pc-link.cjs');

const FTP_PORT = 1425;

async function credentials(host){
  if(!host){
    const live=await fetch('http://127.0.0.1:39525/status',{signal:AbortSignal.timeout(700)})
      .then(r=>r.ok?r.json():null).catch(()=>null);
    const config=await fs.readFile(path.join(DEFAULT_FOLDER,'connection.json'),'utf8').then(JSON.parse)
      .catch(error=>{if(error.code==='ENOENT')return {};throw error;});
    host=live?.online&&live.service?.root===path.resolve(__dirname,'../../..')&&net.isIPv4(live.consoleAddress)
      ?live.consoleAddress:config.consoleIp??'10.42.0.20';
  }
  assert(net.isIPv4(host),'Expected the Switch IPv4 address');
  return {
    host, port:FTP_PORT, user:'anonymous',password:'',
  };
}

if (require.main === module) {
  const args = process.argv.slice(2);
  const option = name => args.includes(name) ? args[args.indexOf(name) + 1] : undefined;
  credentials(option('--host')).then(async value => {
    if (args.includes('--exchange')) value.port = 2121;
    else if (args.includes('--port')) {
      const port = Number(option('--port'));
      assert(port===FTP_PORT, 'Invalid IZ2S FTP port');
      value.port = port;
    }
    process.stdout.write(JSON.stringify(value, null, 2) + '\n');
  }).catch(error => {console.error(error.message);process.exitCode=1;});
}
module.exports={credentials, FTP_PORT};
