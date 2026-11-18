'use strict';
// User-level device credentials, shared by installed platform copies. Never
// included in distribution/files.json. A candidate must be verified by its
// device transport before it can be remembered or applied to a new instance.
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const types={cardputer:{profile:'pc-cardputer-adv',key:'cardputerToken',address:'cardputerEndpoint',digits:64},switch1:{profile:'pc-switch1',key:'switchToken',address:'switchAddress',digits:32}};
function locations(){const home=os.homedir(),config=process.env.XDG_CONFIG_HOME||(process.platform==='win32'?process.env.APPDATA:process.platform==='darwin'?path.join(home,'Library/Application Support'):path.join(home,'.config'));return {file:path.join(config||home,'iz2s-platform','device-pairings.json'),applications:path.join(process.env.XDG_DATA_HOME||path.join(home,'.local/share'),'applications')};}
async function json(file){const stat=await fs.stat(file);if(!stat.isFile()||stat.size>1024*1024)throw Error('Invalid device pairing file');return JSON.parse(await fs.readFile(file,'utf8'));}
function valid(kind,row){const type=types[kind];return !!type&&row?.kind===kind&&new RegExp('^[a-f0-9]{'+type.digits+'}$').test(row.token||'')&&typeof row.endpoint==='string'&&row.endpoint.length<256;}
async function candidates(kind,{file=locations().file,applications=locations().applications,root}={}){
 const found=[];let registry;
 try{registry=await json(file);}catch{}
 for(const row of registry?.devices||[])if(valid(kind,row))found.push({...row,source:'registry'});
 // One-time adoption from applications registered by this user. Read only
 // their credential records, never execute or require another copy's code.
 const type=types[kind];if(!type)return [];
 for(const name of (await fs.readdir(applications).catch(()=>[])).filter(n=>/^izlauncher(?:-run\d+)?\.desktop$/.test(n)).slice(0,32)){
  try{
   const entry=await fs.readFile(path.join(applications,name),'utf8');
   const match=/^Exec="([^"\n]+\/start(?:-run\d+)?\.sh)"\s*$/m.exec(entry);if(!match)continue;
   const directory=await fs.realpath(path.dirname(match[1]));if(directory===root)continue;
   if((await json(path.join(directory,'package.json'))).name!=='iz2s-platform')continue;
   const profile=(await json(path.join(directory,'data/state/launch-configurations.json'))).profiles?.[type.profile];
   const token=String(profile?.[type.key]||'').replace(/[\s-]/g,'').toLowerCase();
   const row={kind,token,endpoint:profile?.[type.address]||'',port:profile?.switchPort||24872,source:'registered-application'};
   if(valid(kind,row)&&row.endpoint)found.push(row);
  }catch{}
 }
 return found.filter((row,index)=>found.findIndex(other=>other.token===row.token&&other.endpoint===row.endpoint&&other.port===row.port)===index).slice(0,16);
}
async function remember(row,{file=locations().file}={}){
 if(!valid(row.kind,row))throw Error('Invalid verified device pairing');
 let previous={version:1,devices:[]};try{previous=await json(file);}catch(e){if(e.code!=='ENOENT')throw e;}
 const clean={kind:row.kind,endpoint:row.endpoint,token:row.token,port:row.port,identity:row.identity||null,address:row.address||null,verifiedAt:new Date().toISOString()};
 const devices=(Array.isArray(previous.devices)?previous.devices:[]).filter(p=>!(p.kind===row.kind&&(p.token===row.token||(row.identity&&p.identity===row.identity))));
 devices.push(clean);await require('./json-write.cjs').write(file,{version:1,devices:devices.slice(-32)},{directoryMode:0o700});
}
module.exports={locations,candidates,remember};
