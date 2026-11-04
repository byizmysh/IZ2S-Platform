'use strict';
const codec=require('./codec.cjs');
const FONT_AT=128*1024,FONT_BYTES=1024*1920,DYNAMIC_AT=FONT_AT+FONT_BYTES,MAX_GPU=6*1024*1024;
const DEFAULTS={fontSize:36,weight:'regular',family:'serif',lineSpacing:1.45,rotation:0,theme:'paper',margin:80};
const choices={fontSize:[24,28,32,36,40,44,48,56,64],weight:['regular','bold'],family:['serif','sans'],lineSpacing:[1.25,1.45,1.65],rotation:[0,90,270],theme:['paper','sepia','night'],margin:[48,80,120]};
function settings(value={}){const out={};for(const [name,values]of Object.entries(choices)){const v=value[name]??DEFAULTS[name];if(!values.includes(v))throw Error('Unsupported reader setting: '+name);out[name]=v;}return out;}
function paginate(text,prefs,metrics){
 const s=settings(prefs),width=s.rotation?1080:1920,height=s.rotation?1920:1080,face=(s.family==='sans'?2:0)+(s.weight==='bold'?1:0),advance=ch=>metrics.advances[face*96+Math.max(0,Math.min(95,ch.charCodeAt(0)-32))]/64*s.fontSize/48;
 const limit=width-2*s.margin,rows=Math.min(64,Math.max(1,Math.floor((height-230)/(s.fontSize*s.lineSpacing)))),lines=[];
 let offset=0;
 for(const raw of text.split(/\n\s*\n/)){
  const start=text.indexOf(raw,offset);offset=start+raw.length;const normalized=raw.replace(/\s+/g,' ').trim();if(!normalized)continue;
  let line='',pixels=0,at=start,consumed=0;
  function flush(){if(line){lines.push({text:line,at});line='';pixels=0;}at=start+consumed;}
  for(const word of normalized.match(/\S+\s*/g)||[]){
   const measured=[...word].reduce((n,c)=>n+advance(c),0);
   if(line&&(pixels+measured>limit||line.length+word.length>190))flush();
   for(const ch of word){const px=advance(ch);if(line&&(pixels+px>limit||line.length>=190))flush();if(!line&&ch===' '){consumed++;continue;}line+=ch;pixels+=px;consumed++;}
  }
  flush();lines.push({text:'',at:start+raw.length});
 }
 if(lines.at(-1)?.text==='')lines.pop();if(!lines.length)lines.push({text:'No readable text.',at:0});
 const pages=[];for(let first=0;first<lines.length;first+=rows)pages.push({first,count:Math.min(rows,lines.length-first),at:lines[first].at});
 return {lines,pages,width,height,rows,face};
}
function view({text='',preferences=DEFAULTS,metrics,books=[],selected=-1,title='Your library',position=0}){
 const s=settings(preferences),layout=paginate(text||'Add an English TXT or EPUB book from Reader settings on your PC.',s,metrics),words=codec.dictionary(text),parts=[];let cursor=DYNAMIC_AT;
 const add=bytes=>{const offset=cursor;parts.push(bytes);cursor+=bytes.length;const pad=(4-cursor%4)%4;if(pad){parts.push(Buffer.alloc(pad));cursor+=pad;}return offset;};
 const ints=values=>{const b=Buffer.alloc(values.length*4);values.forEach((v,i)=>b.writeUInt32LE(v>>>0,i*4));return b;};
 const header=Buffer.alloc(256),put=(i,v)=>header.writeUInt32LE(v>>>0,i*4);
 const dictionaryMeta=Array(256).fill(0);words.forEach((w,i)=>{dictionaryMeta[i*2]=add(Buffer.from(w,'ascii'));dictionaryMeta[i*2+1]=w.length;});put(6,add(ints(dictionaryMeta))/4);
 const lineMeta=[];for(const line of layout.lines){const encoded=codec.encode(line.text,words);lineMeta.push(add(encoded),encoded.length,line.at);}put(8,add(ints(lineMeta))/4);
 put(9,add(ints(layout.pages.flatMap(p=>[p.first,p.count,p.at])))/4);
 const library=[];for(const book of books.slice(0,128)){const t=codec.normalize(book.title).slice(0,64);library.push(add(Buffer.from(t,'ascii')),t.length);}put(14,add(ints(library))/4);
 const name=codec.normalize(title).slice(0,64);put(29,add(Buffer.from(name,'ascii')));put(30,name.length);
 put(0,0x424f4f4b);put(1,1);put(2,FONT_AT);put(3,1024);put(4,1920);put(5,64);put(11,layout.pages.length);put(12,layout.lines.length);put(13,Math.min(books.length,128));
 put(16,s.fontSize);put(17,s.weight==='bold'?1:0);put(18,s.family==='sans'?1:0);put(19,s.rotation);put(20,choices.theme.indexOf(s.theme));put(21,choices.lineSpacing.indexOf(s.lineSpacing));put(22,s.margin);put(23,1024);put(24,13312);put(26,layout.width);put(27,layout.height);put(28,Date.now());put(31,selected<0?0xffffffff:selected);
 const page=Math.max(0,layout.pages.findLastIndex(p=>p.at<=position));
 if(cursor>MAX_GPU)throw Error('This book is too large for console memory at the selected text size. Split it into smaller books.');
 return {header,dynamic:Buffer.concat(parts),layout,page,dictionary:words,bytes:cursor,settings:s};
}
function initial(font,metrics,options={}){const v=view({metrics,...options}),bytes=Buffer.alloc(DYNAMIC_AT+v.dynamic.length);v.header.copy(bytes);metrics.advances.forEach((n,i)=>bytes.writeUInt32LE(n,256+i*4));font.copy(bytes,FONT_AT);v.dynamic.copy(bytes,DYNAMIC_AT);return {...v,bytes};}
module.exports={settings,choices,DEFAULTS,paginate,view,initial,FONT_AT,FONT_BYTES,DYNAMIC_AT,MAX_GPU};
