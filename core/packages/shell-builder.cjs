// Composition remains owned by the selected, validated shell extension.
const fs=require('node:fs/promises'),path=require('node:path'),D=require('../descriptors.cjs');
const original=require('../../shells/izlauncher/server/composition.cjs');
async function build(apps,cacheRoot,{root=require('../root.cjs'),shell='izlauncher'}={}){
 D.assert(D.ID.test(shell),'Invalid shell id');
 const selected=D.scan(path.resolve(root),'shell').items.find(item=>item.id===shell);
 D.assert(selected?.supported&&selected.consoleAbi===1,'Shell unavailable for module composition: '+shell);
 const folder=await fs.realpath(selected.folder),file=await fs.realpath(path.join(folder,'server/composition.cjs'));
 D.assert(file.startsWith(folder+path.sep)&&(await fs.stat(file)).isFile(),'Shell composition must stay inside its extension');
 const implementation=require(file);
 D.assert(typeof implementation.build==='function','Missing shell composition builder');
 return implementation.build(apps,cacheRoot);
}
module.exports={...original,build};
