'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const FIRMWARE='hardware/devices/cardputer-adv/firmware/IZ2S-ADV-0.21.bin';
const README=`IZ2S Cardputer ADV 0.21\n\n1. Extract this package into the root of the Cardputer SD card.\n2. Exit USB storage in M5Launcher. Select SD > Utilities > IZ2S-ADV-0.21.bin and install/run the application.\n3. On first run, choose a local Wi-Fi network that can reach this PC (the PC may use Ethernet). Press 1-5 to select, N/P for pages, M for a manual name. Type the password on the ADV keyboard and press Enter. Tab shows or hides the password; key hints are in the footer. An unavailable saved network opens this menu again after 20 seconds.\n4. Connect the Cardputer USB data cable to Switch. USB readiness is reported automatically over Wi-Fi. If the host has not configured the keyboard after a console restart, firmware 0.21.3 reannounces USB while idle and the panel is checking status; input jobs are never replayed. Set SmileBASIC to English (US) and match the Caps Lock setting selected in the panel.\n5. In IZ2S choose Cardputer ADV as the input device. In Device settings use the IP shown on its screen if iz2s-adv.local is unavailable.\n6. In the panel follow the ADV First Setup stages: Firmware, Wi-Fi, USB to Switch 2, Enter and save, Restart, Launch. The panel uploads all source and workflow commands to SD first. Later requests only name the action to execute. Cardputer types it locally over USB.\n\nStop: red STOP in the panel, or G0 on the Cardputer. Hold G0 for two seconds while idle to change Wi-Fi. Loss of server contact stops input within 15 seconds. Commands are never replayed after a reboot.\n\nIZ2S/input.json contains your private pairing key. Keep it with this SD card. Wi-Fi credentials are entered on the device and saved there, never embedded in the firmware.\nThis is an application-only binary for M5Launcher. It does not replace the M5Launcher bootloader.\n`;
function crc32(bytes){let crc=0xffffffff;for(const byte of bytes){crc^=byte;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0);}return (crc^0xffffffff)>>>0;}
function zip(files){
 const entries=[],central=[];let offset=0;
 for(const [name,data]of files){const file=Buffer.from(name),bytes=Buffer.isBuffer(data)?data:Buffer.from(data),crc=crc32(bytes),header=Buffer.alloc(30);
  header.writeUInt32LE(0x04034b50);header.writeUInt16LE(20,4);header.writeUInt32LE(crc,14);header.writeUInt32LE(bytes.length,18);header.writeUInt32LE(bytes.length,22);header.writeUInt16LE(file.length,26);
  entries.push(header,file,bytes);const row=Buffer.alloc(46);row.writeUInt32LE(0x02014b50);row.writeUInt16LE(20,4);row.writeUInt16LE(20,6);row.writeUInt32LE(crc,16);row.writeUInt32LE(bytes.length,20);row.writeUInt32LE(bytes.length,24);row.writeUInt16LE(file.length,28);row.writeUInt32LE(offset,42);central.push(row,file);offset+=header.length+file.length+bytes.length;
 }
 const directory=Buffer.concat(central),end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(files.length,8);end.writeUInt16LE(files.length,10);end.writeUInt32LE(directory.length,12);end.writeUInt32LE(offset,16);return Buffer.concat([...entries,directory,end]);
}
function firmwarePath(root){return path.join(root,FIRMWARE);}
function packagePath(root){return path.join(root,'data/state/cardputer-adv-sd.zip');}
async function prepare(root){
 const bytes=await fs.readFile(firmwarePath(root));if(bytes[0]!==0xe9||bytes.length>3*1024*1024)throw Error('Cardputer firmware image is unavailable or invalid');
 const file=path.join(root,'data/state/cardputer-pairing.json');let saved=await fs.readFile(file,'utf8').then(JSON.parse,e=>{if(e.code==='ENOENT')return {};throw e;});
 const token=/^[a-f0-9]{64}$/.test(saved.token||'')?saved.token:crypto.randomBytes(32).toString('hex');
 await require('../core/services/json-write.cjs').write(file,{token},{directoryMode:0o700});
 const configuration={ssid:'',password:'',token};
 await fs.writeFile(packagePath(root),zip([['Utilities/IZ2S-ADV-0.21.bin',bytes],['IZ2S/input.json',JSON.stringify(configuration,null,2)+'\n'],['Utilities/IZ2S-ADV-README.txt',README]]),{mode:0o600});
 return {token,configuration,firmware:firmwarePath(root),archive:packagePath(root),readme:README};
}
const media = require('../core/platform/cardputer-media.cjs').discover;
async function install(root,{target,discover=media}={}) {
 const volumes=await discover();
 if(!volumes.length)throw Error('Connect Cardputer to this PC, choose USB storage in M5Launcher, and mount its SD card. Then try Copy over USB again.');
 const volume=target?volumes.find(item=>item.target===target):volumes.length===1?volumes[0]:null;
 if(!volume)throw Error('Several M5Launcher cards are connected. Keep only the intended Cardputer connected and retry.');
 const mounted=await fs.realpath(volume.target),bundle=await prepare(root);
 async function atomic(relative,bytes){
  const file=path.join(mounted,relative),parent=path.dirname(file);
  await fs.mkdir(parent,{recursive:true});
  if(!(await fs.realpath(parent)).startsWith(mounted+path.sep))throw Error('SD destination escapes its mount');
  if((await fs.lstat(file).catch(error=>{if(error.code==='ENOENT')return null;throw error;}))?.isSymbolicLink())throw Error('SD destination must be an ordinary file');
  const temporary=file+'.iz2s-tmp';const handle=await fs.open(temporary,'w',0o600);
  try{await handle.writeFile(bytes);await handle.sync();}finally{await handle.close();}
  await fs.rename(temporary,file);
  if(!(await fs.readFile(file)).equals(Buffer.from(bytes)))throw Error('SD verification failed: '+relative);
 }
 const configFile=path.join(mounted,'IZ2S/input.json');
 let existing=await fs.readFile(configFile,'utf8').then(JSON.parse,error=>{if(error.code==='ENOENT')return {};throw error;});
 if(!existing||Array.isArray(existing)||typeof existing!=='object')throw Error('Existing Cardputer configuration is invalid; preserve it and correct it before copying.');
 await atomic('Utilities/IZ2S-ADV-0.21.bin',await fs.readFile(bundle.firmware));
 await atomic('IZ2S/input.json',JSON.stringify({...existing,ssid:existing.ssid||'',password:existing.password||'',token:bundle.token},null,2)+'\n');
 await atomic('Utilities/IZ2S-ADV-README.txt',README);
 return {ok:true,target:mounted,filename:'Utilities/IZ2S-ADV-0.21.bin',verified:true};
}
module.exports={prepare,firmwarePath,packagePath,README,media,install};
