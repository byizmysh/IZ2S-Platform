const fs=require('node:fs/promises'),path=require('node:path');
const ALIGNMENT=65536,MAXIMUM=1024*1024;
function plan(shaders){
 let largest=0;
 for(const stage of ['vert','frag','comp']){
  const bytes=shaders[stage];
  if(!Buffer.isBuffer(bytes)||bytes.length<20||bytes.length%4||bytes.length>MAXIMUM||bytes.readUInt32LE()!==0x07230203)throw Error('Invalid launcher '+stage+' SPIR-V');
  largest=Math.max(largest,bytes.length);
 }
 return {shaders,bytes:Math.max(0x20000,Math.ceil(largest/ALIGNMENT)*ALIGNMENT)};
}
async function load(directory){
 const stages=['vert','frag','comp'],bytes=await Promise.all(stages.map(stage=>fs.readFile(path.join(directory,'launcher.'+stage+'.spv'))));
 return plan(Object.fromEntries(stages.map((stage,i)=>[stage,bytes[i]])));
}
module.exports={plan,load,ALIGNMENT,MAXIMUM};
