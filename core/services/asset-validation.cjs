'use strict';
// Content validation is pure: reuse successes for identical bytes in this
// process, while callers still read, bound and fingerprint every app asset.
const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto');
const execute=require('node:util').promisify(require('node:child_process').execFile);
function create({run=execute,maximumEntries=256}={}){
 if(!Number.isInteger(maximumEntries)||maximumEntries<1)throw Error('Invalid validation cache limit');
 const complete=new Map(),pending=new Map();
 async function validate(kind,command,bytes){
  if(!Buffer.isBuffer(bytes)||!bytes.length)throw Error('Invalid validation bytes');
  const key=JSON.stringify([kind,command,process.env.PATH,process.env.PYTHONPATH,crypto.createHash('sha256').update(bytes).digest('hex')]);
  if(complete.has(key)){complete.delete(key);complete.set(key,true);return;}
  if(pending.has(key))return pending.get(key);
  const snapshot=Buffer.from(bytes);
  const work=(async()=>{
   const folder=await fs.mkdtemp(path.join(require('node:os').tmpdir(),'hb-validate-'));
   try{
    const file=path.join(folder,kind==='shader'?'module.spv':'icon.image');await fs.writeFile(file,snapshot);
    const args=kind==='shader'?['--target-env','vulkan1.0',file]:['-c','from PIL import Image; import sys; im=Image.open(sys.argv[1]); assert 0<im.width<=4096 and 0<im.height<=4096; im.verify()',file];
    await run(command,args,{timeout:10000,maxBuffer:65536});
   }finally{await fs.rm(folder,{recursive:true,force:true});}
   complete.set(key,true);if(complete.size>maximumEntries)complete.delete(complete.keys().next().value);
  })();
  pending.set(key,work);try{await work;}finally{if(pending.get(key)===work)pending.delete(key);}
 }
 return {
  shader(bytes){return validate('shader',require('../../sdk/tools/toolchain.cjs').tool('SPIRV_VAL','spirv-val'),bytes);},
  icon(bytes){return validate('icon',require('../platform/python.cjs').command(),bytes);},
 };
}
module.exports={create,...create()};
