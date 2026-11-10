'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {inspectModule}=require('../../../core/services/inspect.cjs');
async function locate(client,directory){
  const bootstrap=await client.resolveImport('nvnBootstrapLoader'),region=await client.query(bootstrap.address);
  if(region.permissions!==5||region.type!==3)throw Error('Graphics bootstrap is not executable');
  let next=region.address+region.size;
  for(let i=0;i<3;i++){
    const following=await client.query(next);
    if(!(following.permissions&1)||![3,4].includes(following.type)||following.permissions===5)break;
    next=following.address+following.size;
  }
  const module=await client.registerModule('graphics',region.address),base=Number(region.address);
  const header=await client.read(base,32),offset=header.readUInt32LE(4);
  if(offset<8||offset+28>module.size)throw Error('Invalid graphics module header');
  const mod=await client.read(base+offset,28);
  if(mod.toString('ascii',0,4)!=='MOD0')throw Error('Missing graphics module metadata');
  const fingerprint=crypto.createHash('sha256').update(header).update(mod).digest('hex');
  const file=path.join(directory,'graphics-cache.json');
  const previous=await fs.readFile(file,'utf8').then(JSON.parse,e=>{if(e.code==='ENOENT')return null;throw e;});
  let graphics,cached=false;
  if(previous?.fingerprint===fingerprint&&previous.size===module.size&&Array.isArray(previous.symbols)){
    graphics={base,symbols:previous.symbols};cached=true;
  }else{
    graphics=await inspectModule(client,base,module.size);
    await fs.mkdir(directory,{recursive:true,mode:0o700});
    await fs.writeFile(file,JSON.stringify({fingerprint,size:module.size,symbols:graphics.symbols}),{mode:0o600});
  }
  const count=graphics.symbols.filter(s=>s.section&&s.type===2&&s.name.startsWith('vk')).length;
  if(!count)throw Error('Current Civ7 graphics driver has no Vulkan exports');
  return {graphics,bootstrap,module,cached,vulkanExports:count};
}
module.exports={locate};
