'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'../..'),home=path.join(root,'core/console/recovery/sd-app'),id='iz-reconnect';
async function main(){
 const stage=path.join(root,'data/build-cache/sd-reconnect');await fs.mkdir(stage,{recursive:true});await fs.rm(path.join(stage,'module'),{recursive:true,force:true});
 await require('../../sdk/create-gpu-module.cjs').make(id,path.join(stage,'module'));
 await fs.rm(home,{recursive:true,force:true});await fs.cp(path.join(stage,'module/switch'),home,{recursive:true});
 await fs.copyFile(path.join(root,'core/console/recovery/design/reconnect.png'),path.join(home,'icon.png'));
 const m=JSON.parse(await fs.readFile(path.join(home,'app.json')));m.name='Restore connection';m.description='Request connection recovery and return to the menu';m.sdOnly=true;
 m.native={format:'iz2s.native-app',abi:1,code:'native/native.bin',dataBytes:4096};
 const native=await require('./build-native.cjs').build(path.join(root,'core/console/recovery/reconnect-app.c'),path.join(home,'native'));
 for(const file of ['native.o','native.elf','native.ld'])await fs.rm(path.join(home,'native',file));
 await fs.writeFile(path.join(home,'app.json'),JSON.stringify(m,null,2)+'\n');
 await fs.writeFile(path.join(home,'shaders/app.frag'),'#version 450\n#extension GL_GOOGLE_include_directive : require\n#include "abi.glsl"\nlayout(location=0) out vec4 color;void main(){vec2 p=(gl_FragCoord.xy-vec2(960,540))/540.0;float angle=atan(p.y,p.x);float ring=(1.-smoothstep(.025,.055,abs(length(p)-.32)))*step(-2.3,angle);color=vec4(mix(vec3(.025,.055,.08),vec3(.2,.75,1.),ring),1);}\n');
 await require('../../sdk/build.cjs').buildApp(home);
 const app=await require('../../core/console/sd-launcher/package.cjs').build(home,stage);await fs.copyFile(app.output,path.join(home,'launcher.izapp'));
 const sources={};for(const file of ['app.json','icon.png','shaders/app.frag','shaders/app.comp','shaders/app.vert'])sources[file]=crypto.createHash('sha256').update(await fs.readFile(path.join(home,file))).digest('hex');
 const hashFile=async file=>crypto.createHash('sha256').update(await fs.readFile(file)).digest('hex');
 const report={id,bytes:app.bytes.length,sha256:app.sha256,native,sources,sourceSha256:await hashFile(path.join(root,'core/console/recovery/reconnect-app.c')),nativeAbiSha256:await hashFile(path.join(root,'core/console/sd-launcher/native/native-app.h')),hardwareQualified:false};delete report.native.file;await fs.writeFile(path.join(home,'manifest.json'),JSON.stringify(report,null,2)+'\n');
 const module=path.join(stage,'module');await fs.rm(path.join(module,'switch'),{recursive:true,force:true});await fs.cp(home,path.join(module,'switch'),{recursive:true});
 const d=JSON.parse(await fs.readFile(path.join(module,'module.json'))),files=[];async function walk(dir,prefix){for(const e of await fs.readdir(dir,{withFileTypes:true})){const file=path.join(dir,e.name),name=prefix+'/'+e.name;if(e.isDirectory())await walk(file,name);else files.push(name);}}await walk(path.join(module,'switch'),'switch');d.name=m.name;d.description=m.description;d.version='1.0.0';d.moduleVersion='1.0.0';d.switch.files=files.sort();await fs.writeFile(path.join(module,'module.json'),JSON.stringify(d,null,2)+'\n');
 const packed=await require('../../core/archive.cjs').pack(module),output=path.join(stage,id+'.izm');await fs.writeFile(output,packed);console.log(JSON.stringify({...report,module:output}));return report;
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});module.exports={main};
