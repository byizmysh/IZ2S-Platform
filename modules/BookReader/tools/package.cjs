'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),os=require('node:os');
const root=path.resolve(__dirname,'..'),argv=process.argv.slice(2),command=argv.includes('install')?'install':'pack';
function option(name){const i=argv.indexOf(name);return i<0?null:argv[i+1];}
async function main(){
 const input=option('--platform')||process.env.IZ2S_ROOT;if(!input)throw Error('Specify --platform /path/to/iz2s-platform');const platform=path.resolve(input);if(!platform||!require(path.join(platform,'sdk/platform.cjs')).moduleSettings)throw Error('Update IZ2S Platform: prelaunch module settings are required');
 const descriptor=JSON.parse(await fs.readFile(path.join(root,'module.json'))),archive=require(path.join(platform,'core/archive.cjs')),temp=await fs.mkdtemp(path.join(os.tmpdir(),'book-reader-package-'));
 try{
  await fs.cp(root,temp,{recursive:true,filter:file=>{const parts=path.relative(root,file).split(path.sep);return !parts.some(p=>p.startsWith('.')||['data','runtime','node_modules','dist','__pycache__'].includes(p))&&!file.endsWith('.izm')&&!file.endsWith('.pyc');}});
  const bytes=await archive.pack(temp),pkg=archive.inspect(bytes);if(pkg.files.some(f=>f.name.startsWith('data/')))throw Error('Private reader data must not be packaged');
  if(command==='pack'){const target=path.join(root,'book-reader.izm');await fs.writeFile(target,bytes);console.log(JSON.stringify({file:target,bytes:bytes.length,switchBytes:pkg.files.filter(f=>descriptor.switch.files.includes(f.name)).reduce((n,f)=>n+f.bytes.length,0)}));}
  else{
   const target=path.join(platform,'modules','BookReader'),exists=await fs.access(target).then(()=>true,()=>false);if(exists&&(await fs.lstat(target)).isSymbolicLink())throw Error('Module destination must not be a symlink');if(exists&&!argv.includes('--replace'))throw Error('Already installed. Stop its session and use --replace to keep a backup.');
   const stage=await fs.mkdtemp(path.join(platform,'modules','.book-reader-'));await fs.cp(temp,stage,{recursive:true});await fs.writeFile(path.join(stage,'book-reader.izm'),bytes);
   if(exists){const data=path.join(target,'data');if(await fs.access(data).then(()=>true,()=>false))await fs.cp(data,path.join(stage,'data'),{recursive:true});const backup=path.join(platform,'data/module-backups/book-reader',Date.now().toString());await fs.mkdir(path.dirname(backup),{recursive:true});await fs.rename(target,backup);try{await fs.rename(stage,target);}catch(error){await fs.rename(backup,target);throw error;}}
   else await fs.rename(stage,target);
   console.log('Installed Book Reader in '+target+'; no console commands sent.');
  }
 }finally{await fs.rm(temp,{recursive:true,force:true});}
}
if(require.main===module)main().catch(error=>{console.error(error.message);process.exitCode=1;});module.exports={main};
