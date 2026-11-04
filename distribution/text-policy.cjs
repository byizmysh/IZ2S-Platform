'use strict';
const path=require('node:path');
const textTypes=new Set(['.md','.txt','.rst','.cjs','.js','.json','.py','.sh','.ps1','.cmd','.command','.desktop','.yml','.yaml','.ini','.glsl','.cpp','.c','.h','.comp','.frag','.vert','.bas','.svg','.html','.css','.csv']);
function check(relative,text){
 const name=relative.replace(/\\/g,'/');
 // The four interface catalogs and their translation runtime intentionally
 // contain native language labels. Other project prose uses English.
 if(/^language\/(?:en|ru|es|zh)\//.test(name)||name==='language/runtime.js')return;
 if(!textTypes.has(path.posix.extname(name).toLowerCase())&&!/^(?:README|LICENSE|COPYRIGHT|NOTICE)$/.test(path.posix.basename(name)))return;
 const lines=text.split('\n'),index=lines.findIndex(line=>/[\p{Script=Cyrillic}\p{Script=Han}]/u.test(line));
 if(index>=0)throw Error('English text required outside UI translations: '+name+':'+(index+1));
}
module.exports={check};
