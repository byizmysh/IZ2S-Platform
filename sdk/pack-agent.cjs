const fs=require('node:fs/promises'),path=require('node:path'),crypto=require('node:crypto'),{execFile}=require('node:child_process'),{promisify}=require('node:util');
async function pack(root=require('../core/root.cjs')){
 const folder=path.join(root,'hardware/agents/arm-input'),manifest=JSON.parse(await fs.readFile(path.join(folder,'package.json'))),files={};
 for(const name of manifest.files){if(!/^[\w.-]+$/.test(name)||name==='..')throw Error('Invalid agent source path');files[name]=crypto.createHash('sha256').update(await fs.readFile(path.join(folder,name))).digest('hex');}
 await fs.writeFile(path.join(folder,'checksums.json'),JSON.stringify({algorithm:'sha256',files},null,2)+'\n');
 await fs.mkdir(path.join(root,'packages'),{recursive:true});
 const output=path.join(root,'packages/iz2s-arm-input.tar.gz');
 await promisify(execFile)(require('../core/platform/python.cjs').command(),['-c',"import sys,tarfile;from pathlib import Path;f=Path(sys.argv[1]);names=__import__('json').loads((f/'package.json').read_text())['files']+['checksums.json'];t=tarfile.open(sys.argv[2],'w:gz');[(t.add(f/n,arcname='arm-input/'+n,recursive=False)) for n in names];t.close()",folder,output]);
 return {file:output,bytes:(await fs.stat(output)).size};
}
if(require.main===module)pack().then(r=>console.log(JSON.stringify(r))).catch(e=>{console.error(e.message);process.exitCode=1;});
module.exports={pack};
