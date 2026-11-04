// Optional desktop diagnostics: compile for the current host, never for Switch.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {execFileSync,spawnSync}=require('node:child_process');
const ROOT=path.resolve(__dirname,'../..'),NAMES=['game-render','game-compute-check'];
function build(name,{root=ROOT,force=process.env.REBUILD_CHECKS==='1'}={}){
 if(!NAMES.includes(name))throw Error('Unknown Vulkan diagnostic tool');
 if(process.platform!=='linux')throw Error('Desktop Vulkan diagnostics currently support Linux hosts');
 const source=path.join(root,'sdk/fixtures/vulkan',name+'.c'),compiler=process.env.CC||'cc',args=['-O2',source,'-lvulkan'];
 const digest=crypto.createHash('sha256').update(fs.readFileSync(source)).update(JSON.stringify({compiler,args:['-O2','-lvulkan'],platform:process.platform,arch:process.arch})).digest('hex');
 const folder=path.join(root,'data/build-cache/vulkan',process.platform+'-'+process.arch,digest),output=path.join(folder,name);
 if(!force){try{fs.accessSync(output,fs.constants.X_OK);return output;}catch{}}
 fs.mkdirSync(folder,{recursive:true,mode:0o700});
 const temporary=output+'.'+crypto.randomUUID()+'.tmp';
 try{
  execFileSync(compiler,[...args,'-o',temporary],{stdio:['ignore','pipe','pipe'],timeout:60000});
  fs.chmodSync(temporary,0o700);fs.renameSync(temporary,output);
 }catch(error){
  throw Error('Could not build '+name+'. Install a C compiler, Vulkan headers and libvulkan for this host. '+(error.stderr?.toString().trim()||error.message));
 }finally{fs.rmSync(temporary,{force:true});}
 return output;
}
function run(name,args){
 if(args.includes('--help')){
  console.log(name==='game-render'?'Usage: game-render STATE ATLAS SHADER_DIRECTORY OUTPUT [FRAMES FPS]':'Usage: game-compute-check STATE OUTPUT COMPUTE_SHADER INPUT FRAMES');
  console.log('Desktop Vulkan diagnostic only. Builds automatically for this host; cache: data/build-cache/vulkan/<platform>-<arch>/.');return;
 }
 try{
  const result=spawnSync(build(name),args,{stdio:'inherit'});
  if(result.error)throw result.error;
  if(result.signal){process.kill(process.pid,result.signal);return;}
  process.exitCode=result.status??1;
 }catch(error){console.error(error.message);process.exitCode=1;}
}
if(require.main===module){
 try{
  if(process.argv[2]==='--all')for(const name of NAMES)console.log(build(name));
  else if(NAMES.includes(process.argv[2]))console.log(build(process.argv[2]));
  else console.log('Usage: node sdk/tools/build-vulkan.cjs --all | game-render | game-compute-check');
 }catch(error){console.error(error.message);process.exitCode=1;}
}
module.exports={build,run};
