'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),{prepare}=require('../server/ui/import-plan.js');
const file=(name,text,relative)=>{const f=new File([text],name);if(relative)Object.defineProperty(f,'webkitRelativePath',{value:relative});return f;};
test('CUE imports validate every track before writes and repair only the SD copy',async()=>{
 const cue=file('game.cue','FILE "C:\\old\\disc.img" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n'),disc=file('disc.img','1234');
 const r=await prepare('games','Game',[cue,disc],false,[]);assert.equal(r.repaired,1);assert.equal(r.items[0].file.name,'disc.img');assert.match(await r.items[1].file.text(),/FILE "disc.img" BINARY/);assert.match(await cue.text(),/C:\\old\\disc.img/);
 await assert.rejects(prepare('games','Game',[cue],false,[]),/Missing track/);
 const existing=await prepare('games','Game',[cue],false,[{name:'disc.img',type:'file'}]);assert.equal(existing.repaired,1);
 await assert.rejects(prepare('games','',[file('empty.cue','REM nothing here')],false,[]),/No FILE/);
 const folder=await prepare('games','',[file('a.bin','123','Title/a.bin'),file('cover.jpg','x','Title/cover.jpg')],true,[]);assert.equal(folder.skipped,1);assert.equal(folder.items[0].name,'Title/a.bin');
 await assert.rejects(prepare('games','',[file('bad.bin','1','../bad.bin')],true,[]),/Invalid/);
 await assert.rejects(prepare('games','',[file('A.bin','1'),file('a.bin','2')],false,[]),/same SD name/);
});
