'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
function verify(directory=__dirname){const manifest=JSON.parse(fs.readFileSync(path.join(directory,'native/build/manifest.json')));const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const fail=()=>{throw Object.assign(Error('SD launcher build is out of date. Rebuild it before launch; no console commands were sent.'),{code:'IZ2S_SD_BUILD_STALE'});};if(sha(fs.readFileSync(path.join(directory,'native/build/launcher.bin')))!==manifest.sha256)fail();for(const [name,hash]of Object.entries(manifest.sources))if(sha(fs.readFileSync(path.join(directory,'native',name)))!==hash)fail();return manifest;}
module.exports={verify};
