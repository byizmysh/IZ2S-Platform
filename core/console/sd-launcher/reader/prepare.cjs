'use strict';
// Preparation exports existing TXT/EPUB imports once. The installed reader
// reflows their text and saves its own state on Switch, without this JS code.
const fs=require('node:fs/promises'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),zlib=require('node:zlib');
const root=path.resolve(__dirname,'../../../..'),P=require('../package.cjs'),assets=require('../assets.cjs');
const MAX_TEXT=8*1024*1024,MAX_ASSET=256*1024*1024,MAX_TOTAL=512*1024*1024;
const choices={fontSize:[24,28,32,36,40,44,48,56,64],weight:['regular','bold'],family:['serif','sans'],lineSpacing:[1.25,1.45,1.65],rotation:[0,90,270],theme:['paper','sepia','night'],margin:[48,80,120]},defaults={fontSize:36,weight:'regular',family:'serif',lineSpacing:1.45,rotation:0,theme:'paper',margin:80};
const WELCOME='Welcome to Book Reader\n\nYour books can now be read directly from the SD card. Page turns, typography, portrait rotation and reading progress run on the console.\n\nImport English TXT or EPUB books in Reader settings on your PC, then reinstall Book Reader on SD to copy the library. After installation the reader does not need the PC.\n\nUse Right, R or A to turn a page. Use Left or L to go back. Press Minus to open the reading menu. Return to the launcher with double R3.';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function canonical(text){return String(text).split(/\n\s*\n/).map(p=>p.replace(/\s+/g,' ').trim()).filter(Boolean).join('\n\n');}
function dictionary(text){const counts=new Map();for(const m of text.matchAll(/[A-Za-z']{2,24} ?/g))counts.set(m[0],(counts.get(m[0])||0)+1);return [...counts].map(([word,n])=>({word,saving:n*(word.length-1)-word.length-8})).filter(x=>x.saving>0).sort((a,b)=>b.saving-a.saving||a.word.localeCompare(b.word)).slice(0,128).map(x=>x.word);}
function preferenceIndices(preferences={}){return Object.entries(choices).map(([key,values])=>{const index=values.indexOf(preferences[key]??defaults[key]);assert(index>=0,'Invalid reader setting: '+key);return index;});}
function payload(text){
 text=canonical(text);const bytes=Buffer.from(text,'ascii');assert(text.length&&text.length===bytes.length&&/^[\x0a\x20-\x7e]+$/.test(text)&&bytes.length<=MAX_TEXT,'Invalid SD reader text');
 const words=dictionary(text),parts=[],h=Buffer.alloc(4);h.writeUInt32LE(words.length);parts.push(h);let at=4;
 for(const word of words){const w=Buffer.from(word),size=Buffer.alloc(4);size.writeUInt32LE(w.length);parts.push(size,w);at+=4+w.length;const padding=(4-at%4)%4;if(padding){parts.push(Buffer.alloc(padding));at+=padding;}}
 assert(at<=8192);return {bytes:Buffer.concat([...parts,bytes]),text,textAt:at,textBytes:bytes.length,words};
}
function encode(books,assetRows,{preferences={},selected=null}={}){
 assert(Array.isArray(books)&&books.length>0&&books.length<=128&&Array.isArray(assetRows)&&assetRows.length>0&&assetRows.length<=16);const size=64+books.length*128+assetRows.length*88,buffer=Buffer.alloc(size),selectedIndex=books.findIndex(b=>b.id===selected),prefs=preferenceIndices(preferences);
 [0x52425a49,1,size,Math.ceil(size/16)*16,books.length,assetRows.length,Math.max(0,selectedIndex),selectedIndex>=0?1:0,...prefs,0].forEach((n,i)=>buffer.writeUInt32LE(n,i*4));const ids=new Set();
 books.forEach((b,i)=>{assert(/^[a-f0-9]{24}$/.test(b.id)&&!ids.has(b.id),'Invalid reader book identity');ids.add(b.id);const row=64+i*128;buffer.write(b.id,row,24);const title=String(b.title).replace(/[^\x20-\x7e]/g,'').slice(0,63)||'Untitled book';buffer.write(title,row+24,64);[b.asset,b.offset,b.bytes,b.textAt,b.textBytes,b.crc,Math.min(b.position||0,b.textBytes),b.dictionary,0,0].forEach((n,k)=>buffer.writeUInt32LE(n,row+88+k*4));});
 assetRows.forEach((a,i)=>{assert(/^asset-[a-f0-9]{64}\.izdata$/.test(a.path)&&a.bytes>0&&a.bytes<=MAX_ASSET);const at=64+books.length*128+i*88;buffer.write(a.path,at,80);buffer.writeUInt32LE(a.bytes,at+80);});return buffer;
}
async function bounded(file,max,fallback,base){
 let handle;try{const relative=path.relative(base,file);assert(relative&&!relative.startsWith('..')&&!path.isAbsolute(relative));let current=base;for(const component of relative.split(path.sep).slice(0,-1)){current=path.join(current,component);const stat=await fs.lstat(current);assert(stat.isDirectory()&&!stat.isSymbolicLink(),'Reader data folders must not be symlinks');}handle=await fs.open(file,require('node:fs').constants.O_RDONLY|require('node:fs').constants.O_NOFOLLOW);const stat=await handle.stat();assert(stat.isFile()&&stat.size<=max,'Invalid reader library file');const bytes=await handle.readFile();assert(bytes.length<=max,'Reader library changed while reading');return bytes;}catch(e){if(e.code==='ENOENT'&&fallback!==undefined)return fallback;throw e;}finally{await handle?.close();}
}
async function library(home){
 const data=path.join(home,'data'),read=(file,max,fallback)=>bounded(path.join(data,file),max,fallback,home),index=JSON.parse((await read('books/library.json',1024*1024,Buffer.from('[]'))).toString()),preferences=JSON.parse((await read('settings.json',65536,Buffer.from('{}'))).toString()),progress=JSON.parse((await read('progress.json',1024*1024,Buffer.from('{}'))).toString());assert(Array.isArray(index)&&index.length<=128,'Invalid reader library index');preferenceIndices(preferences);
 const books=[];for(const b of index){assert(/^[a-f0-9]{24}$/.test(b.id),'Invalid reader library identity');const packed=await read('books/compiled/'+b.id+'.leaf',16*1024*1024);assert(packed.length>=44&&packed.toString('ascii',0,4)==='LFB1'&&packed.readUInt32LE(4)<=MAX_TEXT&&packed.readUInt32LE(8)===packed.length-44,'Invalid imported reader book');const bytes=zlib.brotliDecompressSync(packed.subarray(44),{maxOutputLength:MAX_TEXT});assert(bytes.length===packed.readUInt32LE(4)&&sha(bytes)===packed.subarray(12,44).toString('hex'),'Reader source checksum differs');books.push({id:b.id,title:b.title,text:bytes.toString('ascii'),position:progress[b.id]?.position||0});}
 if(!books.length)books.push({id:sha(Buffer.from(WELCOME)).slice(0,24),title:'Welcome to Book Reader',text:WELCOME,position:0});return {books,preferences,selected:preferences.bookId};
}
async function stage(books,directory,options={}){
 await fs.mkdir(directory,{recursive:true});let parts=[],size=0,total=0;const entries=[],assetRows=[];
 async function flush(){if(!size)return;const bytes=Buffer.concat(parts),digest=sha(bytes),name='asset-'+digest+'.izdata',file=path.join(directory,name);await fs.writeFile(file,bytes);assetRows.push({file,path:name,bytes:bytes.length,sha256:digest});parts=[];size=0;}
 for(const book of books){assert(Number.isSafeInteger(book.position||0)&&(book.position||0)>=0,'Invalid reader progress');const p=payload(book.text),position=canonical(String(book.text).slice(0,book.position||0)).length;if(size+p.bytes.length>MAX_ASSET)await flush();total+=p.bytes.length;assert(total<=MAX_TOTAL,'The SD reader library exceeds 512 MiB');entries.push({...book,position,asset:assetRows.length,offset:size,bytes:p.bytes.length,textAt:p.textAt,textBytes:p.textBytes,dictionary:p.words.length,crc:P.crc32(p.bytes)});parts.push(p.bytes);size+=p.bytes.length;const padding=(4-size%4)%4;if(padding){parts.push(Buffer.alloc(padding));size+=padding;}}
 await flush();return {config:encode(entries,assetRows,options),assets:assetRows,books:entries};
}
function eligibility(manifest,files){
 if(manifest.native||manifest.id!=='book-reader')return null;
 const pin=require('./profile.json');try{assert.equal(manifest.version,pin.version,'The local reader needs its qualified app version');for(const [name,digest]of Object.entries(pin.files)){const f=files.find(f=>f.name===name);assert(f&&sha(f.bytes)===digest,'The local reader requires its exact GPU build: '+name);}return {compatible:true,adapter:'launcher-reader-v1',experimental:true,instructions:{hostRequired:false,sdLibrary:true,localTypography:true}};}catch(error){return {compatible:false,reason:error.message};}
}
async function prepare(app,directory){
 if(app.manifest.native||app.id!=='book-reader')return null;const pin=require('./profile.json'),files=[];for(const name of Object.keys(pin.files))files.push({name,bytes:(await require('../../../services/app-catalog.cjs').asset(app.root,name,8*1024*1024)).bytes});const e=eligibility(app.manifest,files);assert(e.compatible,e.reason);
 const report=JSON.parse(await fs.readFile(path.join(__dirname,'native.json'))),code=await fs.readFile(path.join(__dirname,'runtime.bin'));assert.equal(report.format,'iz2s.local-reader-native-v1');assert.equal(report.abi,2);assert.equal(report.bytes,code.length);assert.equal(sha(code),report.sha256);for(const name of ['runtime.c','format.h','../native/native-app.h'])assert.equal(sha(await fs.readFile(path.join(__dirname,name))),report.sources[name],'Rebuild the changed local reader controller');
 const home=require('../../../module-paths.cjs').home(root,'book-reader'),source=await library(home),built=await stage(source.books,path.join(directory,'reader-library'),source);
 const staged=await assets.stage(built.assets,directory),manifest={...app.manifest,native:{format:'iz2s.native-app',abi:2,code:'generated/reader.bin',dataBytes:16*1024*1024,assets:staged.map(a=>({file:a.path,path:a.path,bytes:a.bytes,sha256:a.sha256}))}};
 return {...app,manifest,sdAssets:staged,files:{...app.files,nativeCode:{bytes:code},nativeData:{bytes:built.config}},localReader:{books:built.books.length,hostRequired:false}};
}
module.exports={choices,defaults,WELCOME,canonical,dictionary,preferenceIndices,payload,encode,library,stage,eligibility,prepare};
