'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto');
const {execFile}=require('node:child_process'),{promisify}=require('node:util');
const {encode,decode}=require('../core/archive.cjs');
const run=promisify(execFile),base='mtp:/DBI MTP Responder/1: SD Card/switch/';
const filename='iz2skeyboard.nro',archiveName='IZ2S-SB4-Keyboard-0.24.zip';
const firmwarePath=root=>path.join(root,'hardware/devices/switch1/firmware',filename);
const packagePath=root=>path.join(root,'data/state/switch-keyboard',archiveName);
const preparing=new Map();
function prepare(root){
 if(preparing.has(root))return preparing.get(root);
 const promise=(async()=>{
  const folder=path.join(root,'data/state/switch-keyboard'),configPath=path.join(folder,'connection.cfg');
  await fs.mkdir(folder,{recursive:true,mode:0o700});
  try{await fs.writeFile(configPath,crypto.randomBytes(16).toString('hex')+'\n',{flag:'wx',mode:0o600});}catch(e){if(e.code!=='EEXIST')throw e;}
  const config=await fs.readFile(configPath),token=config.toString('ascii').trim();
  if(!/^[a-f0-9]{32}$/.test(token))throw Error('The saved Switch 1 connection key is invalid. Restore data/state/switch-keyboard/connection.cfg from backup.');
  const files=decode(await fs.readFile(path.join(root,'hardware/devices/switch1/firmware',archiveName))).filter(f=>f.name!=='SHA256SUMS.txt');
  files.push({name:'switch/iz2skeyboard/connection.cfg',bytes:config});
  files.push({name:'SHA256SUMS.txt',bytes:Buffer.from(files.map(f=>crypto.createHash('sha256').update(f.bytes).digest('hex')+'  '+f.name+'\n').join(''))});
  const temporary=packagePath(root)+'.'+crypto.randomUUID()+'.tmp';
  try{await fs.writeFile(temporary,encode(files),{flag:'wx',mode:0o600});await fs.rename(temporary,packagePath(root));}finally{await fs.rm(temporary,{force:true});}
  return {token,configPath,path:packagePath(root)};
 })().finally(()=>preparing.delete(root));preparing.set(root,promise);return promise;
}
async function kio(args){try{return await run('kioclient',['--noninteractive',...args],{timeout:60000,maxBuffer:1024*1024});}catch(e){throw Error(e.code==='ENOENT'?'KDE MTP support is unavailable. Download the SD package and copy it to Switch 1.':'DBI copy failed. Connect Switch 1 to this PC and start DBI MTP Responder. '+String(e.stderr||e.message).slice(0,400));}}
let copying=null;
function install(root,bundle){if(copying)return copying;copying=(async()=>{
 bundle??=await prepare(root);
 const target=base+'iz2skeyboard/';
 const listing=await kio(['ls',base]);
 if(!listing.stdout.split(/\r?\n/).includes('iz2skeyboard'))await kio(['mkdir',target.slice(0,-1)]);
 const temp=await fs.mkdtemp(path.join(os.tmpdir(),'iz-switch-copy-'));
 try {
  const entries=(await kio(['ls',target])).stdout.split(/\r?\n/);
  const backup=path.join(root,'data/state/switch-keyboard-backups',String(Date.now()));
  await fs.mkdir(backup,{recursive:true,mode:0o700});
  for(const [name,source] of [['connection.cfg',bundle.configPath],[filename,firmwarePath(root)]]){
   const bytes=await fs.readFile(source),saved=path.join(backup,name);
   let replaced=false;
   if(entries.includes(name)){
    await kio(['copy',target+name,saved]);
    if((await fs.readFile(saved)).equals(bytes))continue;
    // DBI reports read-only file attributes. KIO cannot overwrite in place;
    // retain a local backup, remove that exact file, then copy the replacement.
    await kio(['remove',target+name]);replaced=true;
   }
   try{
    await kio(['copy',source,target+name]);
    const check=path.join(temp,name);await kio(['copy',target+name,check]);
    if(!(await fs.readFile(check)).equals(bytes))throw Error('Switch 1 copy verification failed. Copy the SD package again.');
   }catch(error){
    if(replaced){
     try{if((await kio(['ls',target])).stdout.split(/\r?\n/).includes(name))await kio(['remove',target+name]);await kio(['copy',saved,target+name]);}
     catch{throw Error(error.message+' The previous file is backed up at '+saved+'.');}
    }
    throw error;
   }
  }
  return {ok:true,verified:true,paired:true,path:'switch/iz2skeyboard/'+filename,sha256:crypto.createHash('sha256').update(await fs.readFile(firmwarePath(root))).digest('hex')};
 }finally{await fs.rm(temp,{recursive:true,force:true});}
 })().finally(()=>{copying=null;});return copying;}
module.exports={firmwarePath,packagePath,prepare,install};
