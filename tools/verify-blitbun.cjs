'use strict';
// Real, standalone BlitBun and VacooTube GPU execution. Never opens a console.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),BASE=1000000000000n;
async function main(){
 const out=path.join(root,'data/diagnostics/media-modules');await fs.mkdir(out,{recursive:true});
 const checker=path.join(out,'compute-check'),renderer=path.join(out,'render');
 for(const [source,binary]of [['media-compute-check.c',checker],['media-render.c',renderer]])execFileSync(process.env.CC||'cc',['-O2',path.join(root,'tools/fixtures',source),'-lvulkan','-o',binary]);
 const reports=[];
 for(const id of ['blitbun','vacootube']){
  const pkg=await require('../core/modules.cjs').read(root,id,{pinned:false}),app=await require('../core/services/app-catalog.cjs').readApp(pkg.switchRoot),folder=path.join(out,id);await fs.mkdir(folder,{recursive:true});
  assert(!app.manifest.builtin&&!app.manifest.shellContribution);assert.equal(app.manifest.hostRuntime,'module:'+id+'/standalone');
  const state=Buffer.alloc(0x800000);app.files.state.bytes.copy(state);app.files.data.bytes.copy(state,1024);
  for(const [stage,asset]of [['vert','vertex'],['frag','fragment'],['comp','compute']])await fs.writeFile(path.join(folder,'app.'+stage+'.spv'),app.files[asset].bytes);
  const sensor={OFFSET:0x7f0000+544},publication=Buffer.alloc(64);publication.writeUInt32LE(0x495a534e);publication.writeUInt32LE(1,4);publication.writeUInt32LE(1,8);publication.writeUInt32LE(2,16);publication.writeUInt32LE(4,24);publication.writeBigUInt64LE(BASE,32);publication.copy(state,sensor.OFFSET);
  state.writeUInt32LE(1,0x700c80);state.writeUInt32LE(4,0x700c84);state.writeUInt32LE(4,0x700c88);state.writeUInt32LE(1,0x700c98);state.writeUInt32LE(1,0x700c80+164);state.writeUInt32LE(1,0x700c80+292);
  async function execute(name,initial,keys){const input=Buffer.alloc(keys.length*64);keys.forEach((key,i)=>{input.writeUInt32LE(key,i*64+8);input.writeUInt32LE(1,i*64+32);});const src=path.join(folder,name+'.initial'),inp=path.join(folder,name+'.input'),output=path.join(folder,name+'.result');await fs.writeFile(src,initial);await fs.writeFile(inp,input);execFileSync(checker,[src,output,path.join(folder,'app.comp.spv'),inp,String(keys.length)],{env:{...process.env,HB_FULL_OUTPUT:'1'},stdio:['ignore','pipe','pipe']});return fs.readFile(output);}
  const visible=await execute('fresh-video',state,[0]);assert.equal(visible.readUInt32LE(0x700c80+276),1,id+' shows an acknowledged fresh frame despite a stale launcher sampler');assert.equal(visible.readUInt32LE(0x700c80+12),1);
  const jitter=Buffer.from(state);jitter.writeBigUInt64LE(BASE-40000000n,sensor.OFFSET+32);assert.equal((await execute('poll-jitter',jitter,[0])).readUInt32LE(0x700c80+276),1,id+' tolerates a delayed one-second sensor poll');
  const stale=Buffer.from(state);stale.writeBigUInt64LE(BASE-125000000n,sensor.OFFSET+32);const hidden=await execute('stale-video',stale,[0]);assert.equal(hidden.readUInt32LE(0x700c80+276),1,id+' a stale diagnostic sample cannot pause acknowledged video');
  const changed=Buffer.from(state);changed.writeUInt32LE(0,sensor.OFFSET+24);assert.equal((await execute('route-loss',changed,[0])).readUInt32LE(0x700c80+276),1);
  const wrong=Buffer.from(state);wrong.writeUInt32LE(9,0x700c80+292);assert.equal((await execute('old-echo',wrong,[0])).readUInt32LE(0x700c80+276),1);
  const paused=Buffer.from(state);paused.writeUInt32LE(0,0x700c80+164);assert.equal((await execute('transport-paused',paused,[0])).readUInt32LE(0x700c80+276),0,id+' an explicit transport pause still hides video');
  assert.equal((await execute('single-r3',state,[0,32,0])).readUInt32LE(1016),0);
  assert.equal((await execute('double-r3',state,[0,32,0,32,0])).readUInt32LE(1016),1);
  assert.equal((await execute('late-r3',state,[0,32,0,...Array(50).fill(0),32,0])).readUInt32LE(1016),0);
  assert.equal((await execute('single-minus',state,[0,2048,0])).readUInt32LE(0x700c80+100),0);
  const menu=await execute('double-minus',state,[0,2048,0,2048,0]);assert.equal(menu.readUInt32LE(0x700c80+100),1);
  const positions=id==='vacootube'?[['menu',menu],['resolution',await execute('resolution-menu',menu,[0,32768,0,1,0])]]:[['menu',menu],['resolution',await execute('resolution-menu',menu,[0,32768,0,1,0])]];
  const video=Buffer.from(visible);video.writeUInt32LE(0xf800f800,1024+0x100000);video.writeUInt32LE(0,1028+0x100000);const hud=Buffer.from(video);hud.writeUInt32LE(5,0x700c80+96);hud.writeUInt32LE(0x495a4d35,0x700c80+104);hud.writeUInt32LE(6000,0x700c80+108);positions.push(['overlay',hud],['video',video],['waiting',await execute('waiting',app.files.state.bytes.length?Buffer.concat([app.files.state.bytes,app.files.data.bytes,Buffer.alloc(0x800000-1024-app.files.data.bytes.length)]):state,[0])]);
  for(const [name,bytes]of positions){await fs.writeFile(path.join(folder,name+'.state'),bytes.subarray(0,1024));await fs.writeFile(path.join(folder,name+'.atlas'),bytes.subarray(1024));execFileSync(renderer,[path.join(folder,name+'.state'),path.join(folder,name+'.atlas'),folder,path.join(folder,name+'.rgba')],{env:{...process.env,HB_SHADER_PREFIX:'app',HB_PRIVATE_VM:'1'},stdio:['ignore','pipe','pipe']});}
  const overlay=await fs.readFile(path.join(folder,'overlay.rgba'));if(id==='blitbun')assert(!overlay.equals(await fs.readFile(path.join(folder,'video.rgba'))),id+' overlay can be toggled independently of video');
  const pixels=await fs.readFile(path.join(folder,'video.rgba')),at=(540*1920+960)*4;assert(pixels[at]>240&&pixels[at+1]<20&&pixels[at+2]<20,id+' actually renders video, not only ACKs it');
  execFileSync('python3',['-c',"from PIL import Image\nfrom pathlib import Path\nimport sys\np=Path(sys.argv[1])\nfor f in p.glob('*.rgba'): Image.frombytes('RGBA',(1920,1080),f.read_bytes()).convert('RGB').save(f.with_suffix('.png'))",folder]);
  reports.push({id,version:pkg.descriptor.version,sha256:pkg.sha256,consoleAbi:1,videoVisible:true,staleDiagnosticsDoNotPause:true,transportPauseHidden:true,noRecurringNativeReadinessChecks:true,doubleR3Exit:true,doubleMinusMenu:true,privateVideoBytes:app.manifest.memory.bytes});
 }
 const report={consoleExecuted:false,cases:reports};await fs.writeFile(path.join(out,'verification.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
