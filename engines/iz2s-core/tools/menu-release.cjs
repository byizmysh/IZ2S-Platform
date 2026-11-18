'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');
const {createHash, createHmac} = require('node:crypto');
const root = path.join(__dirname,'../runtime');
const ABI = 1;
const assets = ['iz2s/izlauncher.iza','iz2s/izlauncher.romfs'];
const limits = [16 * 1024 * 1024, 48 * 1024 * 1024];
const minimums = [512,1];
const sha256 = bytes => createHash('sha256').update(bytes).digest();

async function release(directory = root) {
  const json = name => fs.readFile(path.join(directory,name),'utf8').then(JSON.parse);
  const [manifest,provenance] = await Promise.all([json('package-manifest.json'),json('provenance.json')]);
  if(provenance.launcher?.hotMenuAbi !== ABI)throw Error('Incompatible IZ2S menu ABI.');
  const prefix = Buffer.alloc(84);
  prefix.write('IZMENU01','ascii');
  prefix.writeUInt32BE(ABI,8);
  const rows = assets.map((name,i) => {
    const row = manifest.find(entry => entry.path===name);
    if(!row||!Number.isInteger(row.bytes)||row.bytes<minimums[i]||row.bytes>limits[i]||
       !/^[a-f0-9]{64}$/.test(row.sha256))throw Error('Invalid IZ2S release manifest.');
    prefix.writeUInt32BE(row.bytes,12+i*4);
    prefix.write(row.sha256,20+i*32,32,'hex');
    return row;
  });
  const id = sha256(prefix).subarray(0,16).toString('hex');
  return {
    id,
    async sign(key){
      const parts = await Promise.all(rows.map(async row => {
        const file = await fs.open(path.join(directory,'package',row.path),'r');
        try{
          const bytes=Buffer.alloc(row.bytes);
          if((await file.stat()).size!==row.bytes||
             (await file.read(bytes,{offset:0,length:bytes.length,position:0})).bytesRead!==bytes.length||
             sha256(bytes).toString('hex')!==row.sha256)throw Error('IZ2S release does not match its manifest.');
          return bytes;
        }finally{await file.close();}
      }));
      return Buffer.concat([prefix,createHmac('sha256',key).update(prefix).digest(),...parts]);
    },
  };
}
module.exports={ABI,release};
