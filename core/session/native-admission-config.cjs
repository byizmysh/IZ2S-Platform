'use strict';
// Original-host provisioning for a NEW per-run FS child. This conveys the
// existing dispatch authority; it does not derive process ownership from files.
const fs=require('node:fs/promises'),C=require('node:fs').constants;
const path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const SHA=x=>crypto.createHash('sha256').update(x).digest('hex');
const HEX=/^[0-9a-f]{64}$/;
async function provision({directory,bridge,addresses,clientModule,clientModuleSha256}){
 assert(path.isAbsolute(directory)&&path.isAbsolute(clientModule)&&HEX.test(clientModuleSha256));
 const a=bridge?.admission,channel=a?.channel;
 assert(a&&typeof a.inspect==='function'&&typeof a.retain==='function');
 assert(channel&&HEX.test(channel.token)&&typeof channel.authorityId==='string');
 assert(Number.isInteger(bridge.port)&&bridge.port>0&&bridge.port<65536&&HEX.test(bridge.token));
 assert(/^launcher-[A-Za-z0-9]+$/.test(addresses?.session));
 const url=new URL(channel.url);assert(url.protocol==='http:'&&url.hostname==='127.0.0.1'&&url.port===String(bridge.port)&&url.pathname==='/admission-control'&&!url.search&&!url.hash&&!url.username&&!url.password);
 assert.equal(a.inspect().authorityId,channel.authorityId);assert.equal(a.inspect().phase,'open');
 const dirStat=await fs.lstat(directory);assert(dirStat.isDirectory()&&!dirStat.isSymbolicLink());
 const module=await fs.open(clientModule,C.O_RDONLY|C.O_NOFOLLOW);
 try{const stat=await module.stat();assert(stat.isFile()&&stat.size>0&&stat.size<=128*1024);assert.equal(SHA(await module.readFile()),clientModuleSha256,'Pinned admission client differs')}
 finally{await module.close()}
 // Exclusive creation rejects an old directory, marker, symlink or restart.
 const privateDirectory=path.join(directory,'private');await fs.mkdir(privateDirectory,{mode:0o700});
 const privateStat=await fs.lstat(privateDirectory);assert(privateStat.isDirectory()&&!privateStat.isSymbolicLink()&&(privateStat.mode&0o777)===0o700);
 const configurationFile=path.join(privateDirectory,'native-admission.json');let file;
 try{
  file=await fs.open(configurationFile,C.O_WRONLY|C.O_CREAT|C.O_EXCL|C.O_NOFOLLOW,0o600);
  await file.writeFile(JSON.stringify({connection:{port:bridge.port,token:bridge.token,addresses:{...addresses}},channel:{...channel},participantIdPrefix:'fs-dispatch'})+'\n');
  await file.sync();
 }finally{await file?.close()}
 // Return paths/public CODE hash only. Never log/hash the private config.
 return Object.freeze({configurationFile,clientModule,clientModuleSha256});
}
module.exports={provision};
