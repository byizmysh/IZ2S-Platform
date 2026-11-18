"use strict";
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
async function stage(output){
 const source=path.join(__dirname,'precompiled'),manifest=JSON.parse(await fs.readFile(path.join(source,'shaders.json')));
 await fs.mkdir(output,{recursive:true});
 for(const [name,hash] of Object.entries(manifest.hashes)){
  const file=name+'.comp.spv',bytes=await fs.readFile(path.join(source,file));
  if(crypto.createHash('sha256').update(bytes).digest('hex')!==hash)throw Error('Resident shader integrity failure: '+name);
  await fs.writeFile(path.join(output,file),bytes);
 }
 await fs.copyFile(path.join(source,'shaders.json'),path.join(output,'shaders.json'));return manifest.hashes;
}
module.exports={stage};
