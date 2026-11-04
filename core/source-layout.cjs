// Explicit, project-owned compatibility links for mechanical source relocation.
// Unlisted links and links escaping the project retain the default rejection.
const fs=require('node:fs'),path=require('node:path');
function compatibilityTarget(root,file){
 const relative=path.relative(root,file).split(path.sep).join('/');
 let layout;
 try{layout=JSON.parse(fs.readFileSync(path.join(root,'PROJECT-LAYOUT.json'),'utf8'));}catch{throw Error('Extension links are not allowed');}
 if(layout.format!=='izlauncher.source-layout'||layout.version!==1||!Array.isArray(layout.relocations))throw Error('Invalid source layout');
 const row=layout.relocations.find(item=>item.source===relative);
 if(!row||row.target!=='bootstrap-chain/sources/'+relative||!['file','directory'].includes(row.kind))throw Error('Extension links are not allowed');
 const expected=path.resolve(root,row.target),actual=fs.realpathSync(file);
 if(!expected.startsWith(path.resolve(root,'bootstrap-chain/sources')+path.sep)||actual!==expected)throw Error('Compatibility link target differs from source layout');
 const stat=fs.statSync(actual);
 if(row.kind==='file'?!stat.isFile():!stat.isDirectory())throw Error('Compatibility link type differs from source layout');
 return actual;
}
module.exports={compatibilityTarget};
