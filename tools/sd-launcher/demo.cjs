'use strict';
// Two small, independently packaged GPU apps for the real console acceptance
// test. They are built under data/build-cache and never enter the public store.
const fs=require('node:fs/promises'),path=require('node:path');
async function main(){const root=path.resolve(__dirname,'../..'),base=path.join(root,'data/build-cache/sd-demo');await fs.mkdir(base,{recursive:true});const results=[];
 for(const [id,name,color]of [['sd-proof-one','SD application one','0.08,0.55,0.95'],['sd-proof-two','SD application two','0.9,0.22,0.12']]){
  const home=path.join(base,id);await fs.rm(home,{recursive:true,force:true});await require('../../sdk/create-gpu-module.cjs').make(id,home);const app=path.join(home,'switch'),m=JSON.parse(await fs.readFile(path.join(app,'app.json')));m.name=name;m.description='Stored on SD; A changes color; B or double R3 returns';await fs.writeFile(path.join(app,'app.json'),JSON.stringify(m,null,2)+'\n');
  await fs.writeFile(path.join(app,'shaders/app.comp'),'#version 450\n#extension GL_GOOGLE_include_directive : require\nlayout(local_size_x=1) in;\n#include "abi.glsl"\n#include "hb.glsl"\nvoid main(){c.state.z++;uint pressed=hbPressed();if((pressed&HB_A)!=0u)c.game.x^=1u;if((pressed&HB_B)!=0u)hbExit();hbEndFrame();}\n');
  await fs.writeFile(path.join(app,'shaders/app.frag'),'#version 450\n#extension GL_GOOGLE_include_directive : require\n#include "abi.glsl"\nlayout(location=0) out vec4 color;void main(){vec2 p=(gl_FragCoord.xy-vec2(960,540))/540.0;float t=float(c.state.z)*.035;vec3 bg=vec3('+color+');if(c.game.x!=0u)bg=bg.bgr;float ring=1.0-smoothstep(.02,.04,abs(length(p)-.4));vec2 dot=vec2(cos(t),sin(t))*.4;float moving=1.0-smoothstep(.04,.07,length(p-dot));color=vec4(mix(mix(bg*.15,bg,ring),vec3(1),moving),1);}\n');
  await require('../../sdk/build.cjs').buildApp(app);const output=await require('../../core/console/sd-launcher/package.cjs').build(app,path.join(home,'sd'));results.push({id,name,file:output.output,bytes:output.bytes.length,sha256:output.sha256});
 }await fs.writeFile(path.join(base,'manifest.json'),JSON.stringify(results,null,2)+'\n');console.log(JSON.stringify(results,null,2));return results;}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
