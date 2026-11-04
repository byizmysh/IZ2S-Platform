'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),{execFileSync}=require('node:child_process');
async function build(root=path.resolve(__dirname,'..')){
 const platform=process.env.IZ2S_ROOT||path.resolve(root,'../..');
 execFileSync((process.env.IZ2S_PYTHON || (process.platform === 'win32' ? 'python.exe' : 'python3')),[path.join(root,'tools/font.py')],{stdio:'inherit'});
 const font=await fs.readFile(path.join(root,'switch/assets/font.bin')),metrics=JSON.parse(await fs.readFile(path.join(root,'switch/assets/font.json'))),layout=require('../server/layout.cjs'),welcome=require('../server/store.cjs').WELCOME;
 const result=layout.initial(font,metrics,{text:welcome,title:'Welcome to Book Reader',books:[{title:'Welcome to Book Reader'}],selected:0});
 await fs.writeFile(path.join(root,'switch/assets/data.bin'),result.bytes);await fs.writeFile(path.join(root,'switch/assets/state.bin'),Buffer.alloc(1024));
 await require(path.join(platform,'core/packages/build-focus.cjs')).build(path.join(root,'switch'));
 const module=JSON.parse(await fs.readFile(path.join(root,'module.json')));const files=[];
 async function walk(dir){for(const e of (await fs.readdir(dir,{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name))){const file=path.join(dir,e.name);if(e.isDirectory())await walk(file);else if(e.name!=='font.bin')files.push(path.relative(root,file));}}
 await walk(path.join(root,'switch'));module.switch.files=files;await fs.writeFile(path.join(root,'module.json'),JSON.stringify(module,null,2)+'\n');console.log('Book Reader console assets built; '+result.bytes.length+' initial data bytes.');
}
if(require.main===module)build().catch(error=>{console.error(error);process.exitCode=1;});module.exports={build};
