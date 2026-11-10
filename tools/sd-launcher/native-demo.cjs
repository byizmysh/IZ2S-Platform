'use strict';
const fs=require('node:fs/promises'),path=require('node:path');
async function main(){
 const root=path.resolve(__dirname,'../..'),home=path.join(root,'data/build-cache/sd-demo/sd-proof-native');await fs.rm(home,{recursive:true,force:true});await require('../../sdk/create-gpu-module.cjs').make('sd-proof-native',home);const app=path.join(home,'switch');
 const native=await require('./build-native.cjs').build(path.join(__dirname,'native-proof.c'),path.join(app,'native'));const data=Buffer.alloc(4096);data.writeUInt32LE(0x5a17c0de);await fs.writeFile(path.join(app,'native/data.bin'),data);
 const m=JSON.parse(await fs.readFile(path.join(app,'app.json')));m.name='Native CPU from SD';m.description='ARM64 code and data read from SD; returns after 120 frames';m.native={format:'iz2s.native-app',abi:1,code:'native/native.bin',initial:'native/data.bin',dataBytes:4096};await fs.writeFile(path.join(app,'app.json'),JSON.stringify(m,null,2)+'\n');
 await fs.writeFile(path.join(app,'shaders/app.frag'),'#version 450\n#extension GL_GOOGLE_include_directive : require\n#include "abi.glsl"\nlayout(location=0) out vec4 color;void main(){vec2 p=(gl_FragCoord.xy-vec2(960,540))/540.0;float progress=float(c.game.x)/120.0;vec3 v=vec3(.15,.9,.5);float bar=step(abs(p.y),.08)*step(abs(p.x),.8)*step(p.x,-.8+1.6*progress);color=vec4(mix(v*.1,v,bar),1);}\n');await require('../../sdk/build.cjs').buildApp(app);
 const result=await require('../../core/console/sd-launcher/package.cjs').build(app,path.join(home,'sd'));const report={id:result.id,file:result.output,bytes:result.bytes.length,sha256:result.sha256,native};await fs.writeFile(path.join(home,'manifest.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));return report;
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
