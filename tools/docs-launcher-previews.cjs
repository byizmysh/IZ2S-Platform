'use strict';
// Offscreen documentation images using the shipped console shaders and app artwork.
// No RPC client, console commands or saved personal state are used.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
async function main(){
 process.env.IZ2S_ROOT=root;process.env.IZ15_ROOT=root;process.env.HB_FONT=path.join(root,'language/fonts/DejaVuSans.ttf');process.env.PYTHONDONTWRITEBYTECODE='1';
 const catalog=require('../core/services/app-catalog.cjs'),apps=[];
 for(const folder of ['iz-exchange','iz-exchange-fs','BookReader','izpsx'])apps.push(await catalog.readApp(path.join(root,'modules',folder,'switch')));
 const renderer=require('../sdk/tools/build-vulkan.cjs').build('game-render');
 const records=[];
 for(const shell of ['izlauncher','izpslauncher']){
  const out=path.join(root,'data/build-cache/docs-0.22',shell);await fs.mkdir(out,{recursive:true});
  const server=path.join(root,'shells',shell,'server');
  const font=await require(path.join(server,'font-data.cjs')).load(),raster=await require(path.join(server,'raster.cjs')).raster({apps},out,'catalog'),type=await require(path.join(server,'assets.cjs')).loadTypeAtlas();
  const atlas=Buffer.alloc(8*1024*1024-1024);font.copy(atlas);type.bytes.copy(atlas,type.metadata.offset);raster.copy(atlas,catalog.ATLAS_BYTES);
  raster.subarray(0,apps.length*131072).copy(atlas,catalog.ATLAS_BYTES+16*131072);
  if(shell==='izpslauncher')require(path.join(server,'clock.cjs')).header(new Date('2026-11-10T10:22:00Z')).copy(atlas,0x50000);
  const activity=require(path.join(server,'activity.cjs'));
  const events=apps.map((a,i)=>({id:'documentation-'+i,packageId:a.id,name:a.name,version:a.version,type:'installed',at:new Date(Date.UTC(2026,10,10,10,20-i*3)).toISOString()}));
  const feed=await activity.raster(events,apps,out,'example-activity');feed.header.copy(atlas,activity.HEADER);feed.text.copy(atlas,activity.DATA);
  const state=Buffer.alloc(1024);state.writeFloatLE(4.2,64);state.writeUInt32LE(3,80);state.writeUInt32LE(apps.length,112);state.writeUInt32LE(apps.length,832);state.writeUInt32LE(apps.length,1020);
  const sf=path.join(out,'state.bin'),af=path.join(out,'atlas.bin'),rgba=path.join(out,'frame.rgba');await fs.writeFile(sf,state);await fs.writeFile(af,atlas);
  execFileSync(renderer,[sf,af,path.join(root,'shells',shell,'app'),rgba],{env:{...process.env,HB_SHADER_PREFIX:'app'},stdio:'inherit'});
  const image=path.join(root,'docs/images',shell+'.png');execFileSync('python3',['-c',"from PIL import Image;import sys;from pathlib import Path;Image.frombytes('RGBA',(1920,1080),Path(sys.argv[1]).read_bytes()).convert('RGB').save(sys.argv[2],optimize=True)",rgba,image],{stdio:'inherit'});
  records.push({shell,apps:apps.map(a=>({id:a.id,version:a.version})),fragmentSha256:crypto.createHash('sha256').update(await fs.readFile(path.join(root,'shells',shell,'app/app.frag.spv'))).digest('hex'),imageSha256:crypto.createHash('sha256').update(await fs.readFile(image)).digest('hex')});
 }
 await fs.writeFile(path.join(root,'data/build-cache/docs-0.22/launcher-images.json'),JSON.stringify({version:'0.22.0',consoleCapture:false,consoleCommands:0,exampleActivity:true,records},null,2)+'\n');
 console.log('Fresh 0.22 launcher images generated from the shipped shaders.');
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});
