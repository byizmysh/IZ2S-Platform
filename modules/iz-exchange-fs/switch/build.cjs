const fs=require('node:fs/promises'),path=require('node:path'),{execFileSync}=require('node:child_process'),{deflateSync}=require('node:zlib');
async function main(){
 execFileSync((process.env.IZ2S_PYTHON || (process.platform === 'win32' ? 'python.exe' : 'python3')),[path.join(__dirname,'assets.py')],{stdio:'inherit'});
 await require('../../../sdk/build.cjs').buildApp(__dirname);
 for(const stage of ['vert','frag','comp']){const name=path.join(__dirname,'app.'+stage+'.spv');await fs.writeFile(name+'.deflate',deflateSync(await fs.readFile(name),{level:9}));}
 console.log('File Exchange FS shaders built. Repack with node sdk/pack.cjs modules/iz-exchange-fs');
}
if(require.main===module)main().catch(e=>{console.error(e);process.exitCode=1;});
module.exports={main};
