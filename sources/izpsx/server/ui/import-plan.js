'use strict';
// Plan a PS1 upload before any SD write. Original PC files are never changed.
(function(scope){
 const supported=/\.(cue|bin|img|pbp|sbi|sub|ccd)$/i;
 async function prepare(area,relative,files,folder,cached){
  let skipped=0;const items=[],byName=new Map();
  for(const file of files){
   const local=folder?file.webkitRelativePath:file.name,name=[relative,local].filter(Boolean).join('/');
   if(!local||name.split('/').some(p=>!p||p==='.'||p==='..'||/[\x00-\x1f\x7f\\:]/.test(p)))throw Error('Invalid upload path: '+file.name);
   if(area==='games'&&!supported.test(name)){if(folder){skipped++;continue;}throw Error('Unsupported game file: '+file.name);}
   if(area==='bios'&&(name!=='scph101.bin'||file.size!==524288))throw Error('Choose a 512 KB BIOS named scph101.bin');
   if(!file.size)throw Error('Empty file: '+file.name);const key=name.normalize('NFC').toLocaleLowerCase();if(byName.has(key))throw Error('Two files have the same SD name: '+name);
   const item={file,name};items.push(item);byName.set(key,item);
  }
  if(!items.length)throw Error('No supported PS1 game files were found in that folder');
  for(const item of items)if(/\.cue$/i.test(item.name)){
   if(item.file.size>1024*1024)throw Error('CUE file is unusually large: '+item.file.name);
   const text=await item.file.text(),parent=item.name.split('/').slice(0,-1).join('/');let tracks=0,changed=false;
   const repaired=text.replace(/^(\s*FILE\s+)(?:"([^"]+)"|([^\s]+))(\s+\w+.*)$/gmi,(line,prefix,quoted,bare,tail)=>{
    const original=quoted||bare,base=original.replace(/\\/g,'/').split('/').pop();if(!base||/[\x00-\x1f\x7f:]/.test(base)||base==='.'||base==='..')throw Error('Invalid track name in '+item.file.name);
    const wanted=[parent,base].filter(Boolean).join('/').normalize('NFC').toLocaleLowerCase();
    const selected=byName.get(wanted),existing=parent===relative&&cached?.find(f=>f.type==='file'&&f.name.toLocaleLowerCase()===base.toLocaleLowerCase());
    if(!selected&&!existing)throw Error('Missing track “'+base+'”. Select the CUE and all its BIN/IMG files together.');
    tracks++;const actual=selected?selected.name.split('/').pop():existing.name,output=prefix+'"'+actual+'"'+tail;changed||=output!==line;return output;
   });
   if(!tracks)throw Error('No FILE tracks found in '+item.file.name);
   if(changed){item.file=new File([repaired],item.file.name,{type:'text/plain',lastModified:item.file.lastModified});item.repairedCue=true;}
  }
  items.sort((a,b)=>(/\.cue$/i.test(a.name)?1:0)-(/\.cue$/i.test(b.name)?1:0));return {items,skipped,repaired:items.filter(x=>x.repairedCue).length};
 }
 const api={prepare};if(typeof module!=='undefined'&&module.exports)module.exports=api;else scope.IzpsxImport=api;
})(typeof window!=='undefined'?window:globalThis);
