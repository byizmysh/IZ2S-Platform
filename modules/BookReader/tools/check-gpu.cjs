'use strict';
// Executes the actual app shaders offscreen on the development PC, never on Switch.
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{execFileSync}=require('node:child_process');
(async()=>{
 const home=path.resolve(__dirname,'..'),platform=process.env.IZ2S_ROOT||path.resolve(home,'../..'),tools=require(path.join(platform,'sdk/tools/build-vulkan.cjs')),layout=require('../server/layout.cjs'),metrics=require('../switch/assets/font.json'),font=await fs.readFile(path.join(home,'switch/assets/font.bin')),text=(require('../server/store.cjs').WELCOME+'\n\n').repeat(15),folder=await fs.mkdtemp(path.join(os.tmpdir(),'book-reader-gpu-'));
 const input=path.join(folder,'input.bin');await fs.writeFile(input,Buffer.alloc(64));
 for(const [i,preferences]of [{...layout.DEFAULTS},{...layout.DEFAULTS,rotation:90,weight:'bold',fontSize:44},{...layout.DEFAULTS,rotation:270,family:'sans',theme:'night',fontSize:28}].entries()){
  const built=layout.initial(font,metrics,{text,title:'A little room to read',preferences,books:[{title:'A little room to read'}],selected:0}),state=Buffer.alloc(1024);state.writeUInt32LE(10,88);state.writeUInt32LE(1,788);if(i===1){state.writeUInt32LE(1,832);state.writeUInt32LE(6,836);}
  const memory=Buffer.alloc(8*1024*1024);state.copy(memory);built.bytes.copy(memory,1024);const initial=path.join(folder,i+'-initial.bin'),output=path.join(folder,i+'-output.bin');await fs.writeFile(initial,memory);
  execFileSync(tools.build('game-compute-check'),[initial,output,path.join(home,'switch/app.comp.spv'),input,'1'],{stdio:'inherit',env:{...process.env,HB_FULL_OUTPUT:'1'}});
  const decoded=await fs.readFile(output);assert.equal(decoded.readUInt32LE(796),1);
  for(let row=0;row<built.layout.pages[0].count;row++){
   const length=decoded.readUInt32LE(1024+(512+row)*4);let line='';for(let j=0;j<length;j++)line+=String.fromCharCode(decoded.readUInt32LE(1024+(1024+row*192+j)*4));assert.equal(line,built.layout.lines[row].text,'GPU text mismatch at row '+row);
  }
  const saved=path.join(folder,i+'-state.bin'),atlas=path.join(folder,i+'-atlas.bin'),image=path.join(folder,i+'-frame.rgba');await fs.writeFile(saved,decoded.subarray(0,1024));await fs.writeFile(atlas,decoded.subarray(1024));
  execFileSync(tools.build('game-render'),[saved,atlas,path.join(home,'switch'),image],{stdio:'inherit',env:{...process.env,HB_SHADER_PREFIX:'app'}});
 }
 console.log('All three GPU decode comparisons passed. Screenshots:',folder);
})().catch(error=>{console.error(error);process.exitCode=1;});
