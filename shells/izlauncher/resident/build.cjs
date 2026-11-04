'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const {compile}=require('../../../sdk/build.cjs');
async function build(output){
 output=path.resolve(output);await fs.mkdir(output,{recursive:true});const hashes={};
 for(const name of ['router','slot-reset','slot-input','slot-post','service-control']){
  const source=path.join(__dirname,'shaders',name+'.comp'),target=path.join(output,name+'.comp.spv');
  compile(source,target);hashes[name]=crypto.createHash('sha256').update(await fs.readFile(target)).digest('hex');
 }
 await fs.writeFile(path.join(output,'shaders.json'),JSON.stringify({abi:require('./layout.cjs').VERSION,hashes},null,2)+'\n');return hashes;
}
module.exports={build};if(require.main===module)build(process.argv[2]||path.join(__dirname,'../../../data/resident-build/shaders')).catch(e=>{console.error(e);process.exitCode=1;});
