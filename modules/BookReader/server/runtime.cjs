'use strict';
const fs=require('node:fs/promises'),path=require('node:path'),layout=require('./layout.cjs');
async function create({rpc,mapped,app,store:providedStore}){
 const store=providedStore||await require('./store.cjs').open(require('./platform.cjs').home),metrics=JSON.parse(await fs.readFile(path.join(app.root,'assets/font.json'),'utf8'));
 let books=[],selected=null,current=null,last=0,closed=false,stamp='',pollAt=0,changed=false,write=Promise.resolve();
 async function publish({choose,position}={}){
  const prefs=await store.preferences();books=await store.library();
  const candidate=choose!==undefined?(books[choose]||null):prefs.bookId?(books.find(b=>b.id===prefs.bookId)||selected):selected;
  const book=candidate?await store.readBook(candidate.id):null;
  if(position===undefined)position=candidate?(await store.progress(candidate.id)).position:0;
  current=layout.view({text:book?.text,preferences:prefs,metrics,books,selected:candidate?books.findIndex(b=>b.id===candidate.id):-1,title:book?.title||'Your library',position});
  selected=candidate;if(choose!==undefined&&selected)await store.setPreferences({bookId:selected.id});
  await rpc.write(mapped+1024+layout.DYNAMIC_AT,current.dynamic);await rpc.write(mapped+1024,current.header);
  const state=Buffer.alloc(16);state.writeUInt32LE(current.page);state.writeUInt32LE(selected?1:0,4);state.writeUInt32LE(Math.max(0,books.findIndex(b=>b.id===selected?.id)),8);await rpc.write(mapped+784,state);
  stamp=JSON.stringify({prefs:await store.preferences(),books:books.map(b=>b.id)});
 }
 await publish();
 return {
  async pending(state){if(closed)return false;const seq=state.readUInt32LE(804);if(seq&&seq!==last)return true;if(Date.now()-pollAt>1200){pollAt=Date.now();const next=JSON.stringify({prefs:await store.preferences(),books:(await store.library()).map(b=>b.id)});changed=next!==stamp;}return changed;},
  async afterBatch(state){
   if(closed)return;const seq=state.readUInt32LE(804),op=state.readUInt32LE(800),arg=state.readUInt32LE(808),position=state.readUInt32LE(880);
   write=write.then(async()=>{
    try{
     await rpc.write(mapped+892,Buffer.alloc(4));
     if(changed){changed=false;await publish({position});}
     if(!seq||seq===last)return;last=seq;
     if(op===1)await publish({choose:arg});
     else if(op===2){const fields=['fontSize','weight','family','lineSpacing','rotation','theme','margin'],name=fields[arg];if(!name)throw Error('Unknown reader setting');const prefs=await store.preferences(),values=layout.choices[name],direction=state.readUInt32LE(888)===0?-1:1,value=values[(values.indexOf(prefs[name])+direction+values.length)%values.length];const candidate={...prefs,[name]:value};if(selected){const book=await store.readBook(selected.id);layout.view({text:book.text,preferences:candidate,metrics,books,position});}await store.setPreferences({[name]:value});await publish({position});}
     else if(op===3&&selected)await store.saveProgress(selected.id,arg);
    }catch(error){const flag=Buffer.alloc(4);flag.writeUInt32LE(1);await rpc.write(mapped+892,flag);await fs.writeFile(path.join(store.data,'last-error.json'),JSON.stringify({error:error.message,at:new Date().toISOString()})+'\n');}finally{await rpc.write(mapped+812,Buffer.alloc(4));}
   });await write;
  },async close(){closed=true;await write;if(selected){try{const position=(await rpc.read(mapped+880,4)).readUInt32LE();await store.saveProgress(selected.id,position);}catch{}}}
 };
}
module.exports={create};
