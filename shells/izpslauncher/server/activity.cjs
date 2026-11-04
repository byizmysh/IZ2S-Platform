// Only completed package jobs become activity; file mtimes are not install dates.
const fs=require('node:fs/promises'),path=require('node:path'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
const HEADER=0xf0000,DATA=0x100000,TEXT_BYTES=131072,MAGIC=0x50534143,MAX_EVENTS=6;
function select(jobs){
 return jobs.filter(j=>j.status==='ready'&&Number.isFinite(Date.parse(j.completed)))
  .sort((a,b)=>Date.parse(b.completed)-Date.parse(a.completed)||String(a.id).localeCompare(String(b.id)))
  .slice(0,MAX_EVENTS).map(j=>({id:j.id,packageId:j.packageId,name:j.name,type:j.activityType||'added',version:j.version||'',previousVersion:j.previousVersion||'',at:j.completed}));
}
async function raster(events,apps,directory,label='activity'){
 await fs.mkdir(directory,{recursive:true});
 const input=path.join(directory,label+'.json'),output=path.join(directory,label+'.bin');
 await fs.writeFile(input,JSON.stringify({events:events.slice(0,MAX_EVENTS),apps:apps.map(a=>({id:a.id,name:a.name}))}));
 await promisify(execFile)(require('../../../core/platform/python.cjs').command(),[path.join(__dirname,'ps-activity.py'),input,output],{timeout:15000,maxBuffer:65536});
 const bytes=await fs.readFile(output);if(bytes.length!==128+TEXT_BYTES||bytes.readUInt32LE()!==MAGIC)throw Error('Invalid activity atlas');
 return {header:bytes.subarray(0,128),text:bytes.subarray(128)};
}
function create({rpc,mapped,directory,stateRoot}){
 let stamp='',bytes;
 return {async publish(apps){
  const events=select(await require('../../../core/packages/jobs.cjs').all(stateRoot));
  const next=JSON.stringify({events,apps:apps.map(a=>({id:a.id,name:a.name}))});
  if(next!==stamp){bytes=await raster(events,apps,directory,'izps-activity');stamp=next;}
  // The common storage may have belonged to a module since the previous boundary.
  await rpc.write(mapped+1024+DATA,bytes.text);await rpc.write(mapped+1024+HEADER,bytes.header);
 }};
}
module.exports={select,raster,create,HEADER,DATA,TEXT_BYTES,MAGIC,MAX_EVENTS};
