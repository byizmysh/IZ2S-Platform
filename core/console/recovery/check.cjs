'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
function verify(){const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'native/build/manifest.json')));if(sha(fs.readFileSync(path.join(__dirname,'native/build/recovery.bin')))!==manifest.sha256)throw Error('Rebuild the recovery worker image');for(const [name,hash]of Object.entries(manifest.sources))if(sha(fs.readFileSync(path.join(__dirname,name)))!==hash)throw Error('Recovery worker sources changed; rebuild before installing');return manifest;}
module.exports={verify};
