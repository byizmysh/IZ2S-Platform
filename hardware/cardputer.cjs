'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const FIRMWARES={sb4:'IZ2S-ADV-SB4.bin',civ:'IZ2S-ADV-CIV.bin'};
function profileFile(profile='sb4'){if(!Object.hasOwn(FIRMWARES,profile))throw Error('Choose SB4 or CIV firmware');return FIRMWARES[profile];}
const README=`IZ2S Cardputer ADV: separate SmileBASIC 4 and CivSwitch applications

SmileBASIC 4: Utilities/IZ2S-ADV-SB4.bin — original 0.21.3, USB boot keyboard only. This preserved release binary is not rebuilt.
CivSwitch: Utilities/IZ2S-ADV-CIV.bin — 0.22.4, USB keyboard + controller. All 87 lines and controller actions are stored on SD before one autonomous batch starts. Network loss does not replay input.

The IZ2S-ADV.bin compatibility filename contains the profile selected in the PC panel. Use the explicit SB4/CIV filename to avoid confusion.

1. Connect ADV to the PC. Choose USB Storage in M5Launcher and mount its SD card. Copy the selected application using the panel, or extract this package to SD.
2. Eject the drive, exit USB Storage and install/run the appropriate application from SD > Utilities.
3. Keep ADV connected to local Wi-Fi; use its screen IP in Device settings if automatic discovery is unavailable. Connect its USB data cable to Switch. Both versions use the same pairing and Wi-Fi configuration. Direct USB copying preserves existing Wi-Fi settings.
4. SmileBASIC 4: English (US), fresh Direct screen, then First Setup or Launch. CivSwitch: follow the four-step PC wizard; controller A is automatic.

STOP in the panel requires Wi-Fi. G0 on ADV stops local input. USB disconnection or reboot never triggers input replay. CivSwitch batches tolerate network loss, with per-line and whole-job deadlines.

IZ2S/input.json contains the private pairing key. Preserve it. These application binaries do not replace the M5Launcher bootloader.
`;

function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
function zip(files){
 const entries=[],central=[];let offset=0;
 for(const [name,data]of files){const file=Buffer.from(name),bytes=Buffer.isBuffer(data)?data:Buffer.from(data),crc=crc32(bytes),header=Buffer.alloc(30);
  header.writeUInt32LE(0x04034b50);header.writeUInt16LE(20,4);header.writeUInt32LE(crc,14);header.writeUInt32LE(bytes.length,18);header.writeUInt32LE(bytes.length,22);header.writeUInt16LE(file.length,26);
  entries.push(header,file,bytes);const row=Buffer.alloc(46);row.writeUInt32LE(0x02014b50);row.writeUInt16LE(20,4);row.writeUInt16LE(20,6);row.writeUInt32LE(crc,16);row.writeUInt32LE(bytes.length,20);row.writeUInt32LE(bytes.length,24);row.writeUInt16LE(file.length,28);row.writeUInt32LE(offset,42);central.push(row,file);offset+=header.length+file.length+bytes.length;
 }
 const directory=Buffer.concat(central),end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(files.length,8);end.writeUInt16LE(files.length,10);end.writeUInt32LE(directory.length,12);end.writeUInt32LE(offset,16);return Buffer.concat([...entries,directory,end]);
}
function firmwarePath(root,profile='sb4'){return path.join(root,'hardware/devices/cardputer-adv/firmware',profileFile(profile));}
function packagePath(root){return path.join(root,'data/state/cardputer-adv-sd.zip');}
async function prepare(root,{profile='sb4',signal}={}){
 signal?.throwIfAborted();
 const images={};profileFile(profile);
 for(const id of Object.keys(FIRMWARES)){
  signal?.throwIfAborted();
  const image=await fs.readFile(firmwarePath(root,id)),manifest=JSON.parse(await fs.readFile(path.join(root,'hardware/devices/cardputer-adv/firmware','manifest-'+id+'.json')));
  if(image[0]!==0xe9||image.length!==manifest.bytes||crypto.createHash('sha256').update(image).digest('hex')!==manifest.sha256)throw Error('Cardputer firmware integrity check failed: '+id);
  images[id]=image;
 }
 signal?.throwIfAborted();
 const bytes=images[profile];
 const file=path.join(root,'data/state/cardputer-pairing.json');let saved=await fs.readFile(file,'utf8').then(JSON.parse,e=>{if(e.code==='ENOENT')return {};throw e;});
 const token=/^[a-f0-9]{64}$/.test(saved.token||'')?saved.token:crypto.randomBytes(32).toString('hex');
 await require('../core/services/json-write.cjs').write(file,{token},{directoryMode:0o700});
 const configuration={ssid:'',password:'',token};
 signal?.throwIfAborted();
 await fs.writeFile(packagePath(root),zip([['Utilities/IZ2S-ADV-SB4.bin',images.sb4],['Utilities/IZ2S-ADV-CIV.bin',images.civ],['Utilities/IZ2S-ADV.bin',bytes],['IZ2S/input.json',JSON.stringify(configuration,null,2)+'\n'],['Utilities/IZ2S-ADV-README.txt',README]]),{mode:0o600});
 signal?.throwIfAborted();
 return {token,configuration,firmware:firmwarePath(root,profile),profile,filename:'Utilities/'+profileFile(profile),archive:packagePath(root),readme:README};
}
const media = require('../core/platform/cardputer-media.cjs').discover;
async function install(root,{target,discover=media,profile='sb4',signal,onProgress=()=>{},bundle:prepared}={}) {
 signal?.throwIfAborted();
 const volumes=await discover();
 signal?.throwIfAborted();
 if(!volumes.length)throw Error('Connect Cardputer to this PC, choose USB storage in M5Launcher, and mount its SD card. Then try Copy over USB again.');
 const volume=target?volumes.find(item=>item.target===target):volumes.length===1?volumes[0]:null;
 if(!volume)throw Error('Several M5Launcher cards are connected. Keep only the intended Cardputer connected and retry.');
 const mounted=await fs.realpath(volume.target),bundle=prepared||await prepare(root,{profile,signal});
 signal?.throwIfAborted();
 let completedBytes=0;
 async function atomic(relative,bytes){
  signal?.throwIfAborted();bytes=Buffer.from(bytes);
  const file=path.join(mounted,relative),parent=path.dirname(file);
  await fs.mkdir(parent,{recursive:true});
  if(!(await fs.realpath(parent)).startsWith(mounted+path.sep))throw Error('SD destination escapes its mount');
  if((await fs.lstat(file).catch(error=>{if(error.code==='ENOENT')return null;throw error;}))?.isSymbolicLink())throw Error('SD destination must be an ordinary file');
  const temporary=file+'.iz2s-tmp-'+crypto.randomUUID();let handle;
  try{
   signal?.throwIfAborted();handle=await fs.open(temporary,'wx',0o600);
   for(let at=0;at<bytes.length;){
    signal?.throwIfAborted();const size=Math.min(65536,bytes.length-at);
    const result=await handle.write(bytes,at,size,at);
    if(!result.bytesWritten)throw Error('SD write made no progress');
    at+=result.bytesWritten;completedBytes+=result.bytesWritten;
    await onProgress({file:relative,completedBytes});
   }
   signal?.throwIfAborted();await handle.sync();signal?.throwIfAborted();
   await handle.close();handle=null;
   if(!(await fs.readFile(temporary)).equals(bytes))throw Error('SD verification failed: '+relative);
   signal?.throwIfAborted();await fs.rename(temporary,file);
  }finally{
   if(handle)await handle.close();
   await fs.rm(temporary,{force:true});
  }
 }
 const configFile=path.join(mounted,'IZ2S/input.json');
 let existing=await fs.readFile(configFile,'utf8').then(JSON.parse,error=>{if(error.code==='ENOENT')return {};throw error;});
 if(!existing||Array.isArray(existing)||typeof existing!=='object')throw Error('Existing Cardputer configuration is invalid; preserve it and correct it before copying.');
 for(const id of Object.keys(FIRMWARES))await atomic('Utilities/'+profileFile(id),await fs.readFile(firmwarePath(root,id)));
 await atomic('Utilities/IZ2S-ADV.bin',await fs.readFile(bundle.firmware));
 await atomic('IZ2S/input.json',JSON.stringify({...existing,ssid:existing.ssid||'',password:existing.password||'',token:bundle.token},null,2)+'\n');
 await atomic('Utilities/IZ2S-ADV-README.txt',README);
 return {ok:true,target:mounted,filename:bundle.filename,profile,version:profile==='sb4'?'0.21.3':'0.22.4',verified:true};
}
module.exports={prepare,firmwarePath,profileFile,packagePath,README,media,install};
