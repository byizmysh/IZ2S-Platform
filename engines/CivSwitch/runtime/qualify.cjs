'use strict';
const crypto=require('node:crypto'),fs=require('node:fs/promises'),path=require('node:path');
const {create}=require('./client.cjs'),{inspectModule}=require('../../../core/services/inspect.cjs');
const check=(value,message)=>{if(!value)throw Error(message);};
async function qualify({host,directory,client=create({host}),graphics=true}){
  await fs.mkdir(directory,{recursive:true,mode:0o700});
  const info=await client.connect(),report={engine:'civswitch',filesystem:false,started:new Date().toISOString(),info};
  let allocation;
  try{
    await client.notice('IZ2S: checking private memory and the Civ7 native caller.');
    allocation=await client.allocate(64);
    const marker=crypto.randomBytes(64);await client.write(allocation,marker);
    check((await client.read(allocation,64)).equals(marker),'CivSwitch owned-memory challenge failed');
    const memset=await client.lookup('memset');
    check(await client.call('memset',[allocation,0x5a,64])===allocation,'CivSwitch memset return differs');
    check((await client.read(allocation,64)).equals(Buffer.alloc(64,0x5a)),'CivSwitch native memory challenge failed');
    report.memory={verified:true,bytes:64,memset};
    const sdk=info.modules.find(m=>m.name==='sdk');
    const sdkRegion=await client.query(sdk.base);
    check(sdkRegion.address===BigInt(sdk.base)&&sdkRegion.permissions===5,'CivSwitch SDK mapping differs');
    report.memoryMap={verified:true,sdk:{...sdkRegion,address:String(sdkRegion.address),size:String(sdkRegion.size)}};
    if(graphics){
      await client.notice('IZ2S: memory verified; locating the graphics driver.');
      // On Switch 2, the game's import may be interposed by the compatibility
      // driver. Looking up the same name in Civ7's SDK finds its original
      // Switch 1 implementation instead of the driver used by the game.
      const driver=await require('./driver.cjs').locate(client,directory);
      await fs.writeFile(path.join(directory,'graphics-symbols.json'),JSON.stringify(driver.graphics,null,2),{mode:0o600});
      report.graphics={base:driver.module.base,bootstrap:driver.bootstrap,exports:driver.graphics.symbols.length,vulkanExports:driver.vulkanExports,cached:driver.cached};
      await client.notice('IZ2S: Civ7 graphics driver found. Native qualification complete; loader stays active.');
    }
    report.verified=true;
  }catch(error){report.error=error.message;throw error;}
  finally{
    if(!client.uncertain){if(allocation)await client.free(allocation);await client.releaseNative();await client.quiet(false);}
    report.completed=new Date().toISOString();
    await fs.writeFile(path.join(directory,'qualification.json'),JSON.stringify(report,null,2),{mode:0o600});
  }
  return report;
}
if(require.main===module)qualify({host:process.argv[2],directory:path.resolve(process.argv[3]||'data/diagnostics/civswitch-runtime')})
 .then(report=>console.log(JSON.stringify({verified:report.verified,memory:report.memory?.verified,memoryMap:report.memoryMap?.verified,graphics:report.graphics,filesystem:false})))
 .catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={qualify};
