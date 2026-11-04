'use strict';
// Presentation telemetry for package operations; bytes advance only after acknowledgement.
const jobs=require('./jobs.cjs');
const HEADER=1024+0x120000,BYTES=1024,MAGIC=0x495a5031;
const PHASE={verify:1,prepare:2,graphics:3,install:4,transfer:5,ready:6,failed:7};
const LABEL={verify:'Checking files',prepare:'Preparing on PC',graphics:'Preparing graphics',install:'Installing on PC',transfer:'Sending menu to console',ready:'Ready in menu',failed:'Loading failed'};
const mib=n=>(n/1048576).toFixed(1)+' MiB';
function encode(value){
 const b=Buffer.alloc(BYTES),known=Number.isSafeInteger(value.totalBytes)&&value.totalBytes>0;
 b.writeUInt32LE(MAGIC);b.writeUInt32LE(1,4);b.writeUInt32LE(PHASE[value.phase]||0,8);
 b.writeUInt32LE(value.phase==='ready'?100:known?Math.min(100,Math.floor(value.completedBytes*100/value.totalBytes)):0xffffffff,12);
 if(value.phase==='failed')b.writeUInt32LE(0xffffffff,12);
 b.writeUInt32LE(180,20);
 const percent=value.phase==='ready'?100:known?Math.min(100,Math.floor(value.completedBytes*100/value.totalBytes)):null;
 const lines=[value.name||'Module',LABEL[value.phase]||'Waiting',known?percent+'%   '+mib(value.completedBytes)+' / '+mib(value.totalBytes):(percent===null?'':percent+'% - ')+'Module '+value.index+' of '+value.count];
 for(let row=0;row<3;row++)for(const [i,ch]of [...lines[row].replace(/[^\x20-\x7e]/g,'?').slice(0,63)].entries())b.writeUInt32LE(ch.charCodeAt(0),64+row*256+i*4);
 return b;
}
function create({rpc,mapped,stateRoot,redraw=async()=>{},clock=Date.now}){
 let current=null,last=0;
 async function update(job,phase,bytes={},force=true){
  const changed=job.progress?.phase!==phase;
  const known=Number.isSafeInteger(bytes.totalBytes)&&bytes.totalBytes>0;
  const completed=known?Math.min(bytes.totalBytes,Math.max(0,bytes.completedBytes||0)):0;
  job.progress={phase,label:LABEL[phase],completedBytes:completed,totalBytes:known?bytes.totalBytes:null,remainingBytes:known?bytes.totalBytes-completed:null,
   percent:known?Math.min(100,Math.floor(completed*100/bytes.totalBytes)):phase==='ready'?100:null,updated:new Date(clock()).toISOString()};
  current={job,index:bytes.index||current?.index||1,count:bytes.count||current?.count||1};
  if(!force&&!changed&&completed!==bytes.totalBytes&&clock()-last<250)return;
  await jobs.save(stateRoot,job);await publish();last=clock();
 }
 async function publish(){if(!current)return;await rpc.write(mapped+HEADER,encode({...current.job.progress,name:current.job.name,index:current.index,count:current.count}));await redraw();}
 return {update,publish,async upload(bytes){if(current)await update(current.job,'transfer',bytes,false);},get current(){return current?.job||null;}};
}
async function upload(rpc,address,bytes,notify){
 if(!notify){await rpc.write(address,bytes);return;}
 await notify({completedBytes:0,totalBytes:bytes.length});
 for(let at=0;at<bytes.length;at+=262144){const part=bytes.subarray(at,at+262144);await rpc.write(address+at,part);await notify({completedBytes:at+part.length,totalBytes:bytes.length});}
}
module.exports={create,encode,upload,HEADER,BYTES,MAGIC,PHASE};
