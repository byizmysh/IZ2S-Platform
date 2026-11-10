'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
const {create}=require('./client.cjs');
const root=path.resolve(__dirname,'../../..');
async function prepare({host,directory,diagnostics=path.join(root,'data/diagnostics/civswitch-runtime')}){
  const proof=JSON.parse(await fs.readFile(path.join(diagnostics,'qualification.json')));
  if(!proof.verified||!proof.graphics?.vulkanExports)throw Error('Qualify the current CivSwitch graphics driver first');
  const graphics=JSON.parse(await fs.readFile(path.join(diagnostics,'graphics-symbols.json')));
  const client=create({host}),info=await client.connect();
  for(const name of ['main','sdk'])if(info.modules.find(m=>m.name===name)?.base!==proof.info.modules.find(m=>m.name===name)?.base)throw Error('CivSwitch process changed since qualification');
  let region=await client.query(graphics.base);
  for(let i=0;i<4;i++){
    const next=await client.query(region.address+region.size);
    if(!(next.permissions&1)||![3,4].includes(next.type)||next.permissions===5)break;
    region=next;
  }
  await client.registerModule('graphics',graphics.base);
  await fs.mkdir(path.join(directory,'shared'),{recursive:true,mode:0o700});
  const scratch=await client.allocate(0x20000);
  const addresses={session:path.basename(directory),engine:'civswitch',platform:'switch2',target:'switch2',
    main:Number(info.modules.find(m=>m.name==='main').base),sdk:Number(info.modules.find(m=>m.name==='sdk').base),scratch:Number(scratch),filesystem:false};
  const options={host,addresses,graphics};
  await fs.writeFile(path.join(directory,'civswitch-connection.json'),JSON.stringify(options,null,2),{mode:0o600});
  await fs.writeFile(path.join(directory,'compatibility-module.json'),JSON.stringify(graphics,null,2),{mode:0o600});
  await fs.writeFile(path.join(directory,'shared/rpc.cjs'),
    'module.exports=require('+JSON.stringify(path.join(__dirname,'rpc.cjs'))+').connect(require("../civswitch-connection.json"));\n',{mode:0o600});
  await client.notice('IZ2S: native graphics transport ready. No filesystem service has been started.');
  return {client,addresses};
}
async function main(host,mode='copy',existing,shell='izlauncher'){
  if(!['izlauncher','izpslauncher'].includes(shell))throw Error('Choose IZlauncher or IZPSlauncher');
  if(!['copy','display','menu'].includes(mode))throw Error('Choose copy, display or menu');
  let directory=existing?path.resolve(existing):await fs.mkdtemp(path.join(root,'data/diagnostics/civ-launcher-'));
  if(!existing)await prepare({host,directory});
  console.log('CivSwitch graphics session: '+directory);
  if(mode!=='copy'){
    const client=create({host});await client.connect();await client.quiet(true);
  }
  if(mode==='menu'){
    // Civ7 owns its renderer and shaders; package manifests retain the common ABI.
    process.env.HB_APPS_DIR||=path.join(directory,'apps');
    await fs.mkdir(process.env.HB_APPS_DIR,{recursive:true,mode:0o700});
    await require('../'+(shell==='izpslauncher'?'pslauncher':'launcher')+'/prepare.cjs').main(directory,'auto');
  }else await require('./display.cjs').main(directory,mode);
}
if(require.main===module)main(process.argv[2],process.argv[3],process.argv[4],process.argv[5]).catch(error=>{console.error(error.stack);process.exitCode=1;});
module.exports={prepare,main};
