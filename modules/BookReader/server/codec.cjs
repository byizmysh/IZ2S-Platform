'use strict';
const zlib=require('node:zlib'),crypto=require('node:crypto');
const MAX_TEXT=8*1024*1024;
function normalize(value){
 return String(value).replace(/\r\n?/g,'\n').replace(/[\u2018\u2019]/g,"'").replace(/[\u201c\u201d]/g,'"').replace(/[\u2013\u2014]/g,'--').replace(/\u2026/g,'...').normalize('NFKD').replace(/[\u0300-\u036f]/g,'').replace(/[^\x09\x0a\x20-\x7e]/g,'').replace(/\t/g,'    ').replace(/[ \t]+\n/g,'\n').replace(/\n{4,}/g,'\n\n\n').trim();
}
function pack(text){
 text=normalize(text);const source=Buffer.from(text,'ascii');if(!source.length||source.length>MAX_TEXT)throw Error('Book must contain English text and be no larger than 8 MiB after conversion');
 const payload=zlib.brotliCompressSync(source,{params:{[zlib.constants.BROTLI_PARAM_QUALITY]:9,[zlib.constants.BROTLI_PARAM_MODE]:zlib.constants.BROTLI_MODE_TEXT}}),header=Buffer.alloc(44);header.write('LFB1');header.writeUInt32LE(source.length,4);header.writeUInt32LE(payload.length,8);crypto.createHash('sha256').update(source).digest().copy(header,12);return Buffer.concat([header,payload]);
}
function unpack(bytes){
 if(bytes.length<44||bytes.toString('ascii',0,4)!=='LFB1')throw Error('Invalid book container');const size=bytes.readUInt32LE(4),length=bytes.readUInt32LE(8);if(size>MAX_TEXT||length!==bytes.length-44)throw Error('Invalid book size');
 const text=zlib.brotliDecompressSync(bytes.subarray(44),{maxOutputLength:MAX_TEXT});if(text.length!==size||!crypto.createHash('sha256').update(text).digest().equals(bytes.subarray(12,44)))throw Error('Book checksum mismatch');return text.toString('ascii');
}
function dictionary(text){
 const counts=new Map();for(const match of text.matchAll(/[A-Za-z']{2,24} ?/g)){const word=match[0];counts.set(word,(counts.get(word)||0)+1);}
 return [...counts].map(([word,n])=>({word,saving:n*(word.length-1)-word.length-8})).filter(x=>x.saving>0).sort((a,b)=>b.saving-a.saving||a.word.localeCompare(b.word)).slice(0,128).map(x=>x.word);
}
function encode(text,words){
 const lookup=new Map(words.map((w,i)=>[w,128+i])),out=[];let end=0;
 for(const m of text.matchAll(/[A-Za-z']{2,24} ?/g)){for(let i=end;i<m.index;i++)out.push(text.charCodeAt(i));const token=lookup.get(m[0]);if(token===undefined)for(const c of m[0])out.push(c.charCodeAt(0));else out.push(token);end=m.index+m[0].length;}
 for(let i=end;i<text.length;i++)out.push(text.charCodeAt(i));return Buffer.from(out);
}
function decode(bytes,words,limit=192){let out='';for(const b of bytes){if(b<128)out+=String.fromCharCode(b);else{if(!words[b-128])throw Error('Invalid dictionary token');out+=words[b-128];}if(out.length>limit)throw Error('Decoded line exceeds limit');}return out;}
module.exports={normalize,pack,unpack,dictionary,encode,decode,MAX_TEXT};
