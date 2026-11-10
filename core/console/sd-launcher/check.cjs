'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
function verify(directory=__dirname){const manifest=JSON.parse(fs.readFileSync(path.join(directory,'native/build/manifest.json')));const sha=b=>crypto.createHash('sha256').update(b).digest('hex');const fail=()=>{throw Object.assign(Error('SD launcher build is out of date. Rebuild it before launch; no console commands were sent.'),{code:'IZ2S_SD_BUILD_STALE'});};if(sha(fs.readFileSync(path.join(directory,'native/build/launcher.bin')))!==manifest.sha256)fail();for(const [name,hash]of Object.entries(manifest.sources))if(sha(fs.readFileSync(path.join(directory,'native',name)))!==hash)fail();return manifest;}
// Run with the existing pre-launch check, before acquiring console resources.
// A current supervisor alone does not prove its companion apps were rebuilt.
function verifyRuntime(directory=__dirname){
 const supervisor=verify(directory),recovery=path.resolve(directory,'../recovery');
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const fail=(component,file)=>{throw Object.assign(Error('SD launcher '+component+' build is out of date ('+file+'). Rebuild the launcher resources before launch; no console commands were sent.'),{code:'IZ2S_SD_BUILD_STALE',component});};
 function read(folder,name){try{return fs.readFileSync(path.join(folder,name));}catch{fail('resources',name);}}
 function report(folder,name){try{return JSON.parse(read(folder,name));}catch{fail('manifest',name);}}
 function match(folder,name,expected,component){if(!/^[a-f0-9]{64}$/.test(expected||'')||sha(read(folder,name))!==expected)fail(component,name);}
 function image(folder,name,binary,component){const m=report(folder,name),bytes=read(folder,binary);if(m.bytes!==bytes.length||sha(bytes)!==m.sha256)fail(component,binary);for(const [file,hash]of Object.entries(m.sources||{}))match(folder,file,hash,component);return m;}
 image(recovery,'native/build/manifest.json','native/build/recovery.bin','recovery worker');
 const tile=path.join(recovery,'sd-app'),m=image(tile,'manifest.json','launcher.izapp','Restore connection');
 match(recovery,'reconnect-app.c',m.sourceSha256,'Restore connection');
 match(directory,'native/native-app.h',m.nativeAbiSha256,'Restore connection');
 match(tile,'native/native.bin',m.native?.sha256,'Restore connection');
 image(path.join(directory,'reader'),'native.json','runtime.bin','Book Reader');
 const instructions=path.join(directory,'instructions'),i=image(instructions,'native.json','runtime.bin','local instructions');
 match(instructions,'runtime.c',i.sourceSha256,'local instructions');
 match(instructions,'checkpoint.h',i.checkpointSha256,'local instructions');
 match(directory,'native/native-app.h',i.nativeAbiSha256,'local instructions');
 return supervisor;
}
module.exports={verify,verifyRuntime};
