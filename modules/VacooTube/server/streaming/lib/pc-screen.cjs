// RGB565 double buffer in launcher-owned mapped storage. No native allocation,
// app switch, decoder, or host desktop permission is needed for the color bars.
const fs=require('node:fs/promises'),path=require('node:path'),http=require('node:http'),crypto=require('node:crypto');
const SLOT=0x610000,STRIDE=0x78000,HEADER=0x700c80,MAX=1920*1080/2;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function dimensions(w,h,format=0){
 const valid=format===0?w>=2&&w<=640&&h>=2&&h<=360&&w%2===0:format===2?w>=4&&w<=1280&&h>=4&&h<=720&&w%4===0&&h%4===0&&1024+w*h<=STRIDE:format===1&&w>=4&&w<=1920&&h>=4&&h<=1080&&w%4===0&&h%4===0;
 if(!Number.isInteger(w)||!Number.isInteger(h)||!valid)throw Error('Invalid frame dimensions');
}
function frameBytes(w,h,format=1){dimensions(w,h,format);return format===2?1024+w*h:format===1?w*h/2:w*h*2;}
function formatName(format){return ['RGB565-LE','BC1-opaque','Indexed8-lossless'][format];}
function changed(before,after){
 if(!before||before.length!==after.length)return [0,after.length];
 let first=0;while(first<after.length&&before.readUInt32LE(first)===after.readUInt32LE(first))first+=4;
 if(first===after.length)return null;
 let end=after.length;while(end>first&&before.readUInt32LE(end-4)===after.readUInt32LE(end-4))end-=4;
 return [Math.floor(first/4096)*4096,Math.min(after.length,Math.ceil(end/4096)*4096)];
}
function bars(w=480,h=270){
 dimensions(w,h);const b=Buffer.alloc(w*h*2),colors=[0xffff,0xffe0,0x07ff,0x07e0,0xf81f,0xf800,0x001f,0];
 for(let y=0;y<h;y++)for(let x=0;x<w;x++)b.writeUInt16LE(colors[Math.min(7,Math.floor(x*8/w))],(y*w+x)*2);return b;
}
module.exports={SLOT,STRIDE,HEADER,MAX,dimensions,frameBytes,formatName,changed,bars};
