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
function uploader({rpc,mapped,video={base:mapped+SLOT,stride:STRIDE,capacity:STRIDE},save=async()=>{},onFreshness=async()=>{}}){
 let visibilityEpoch=0;const slots=[null,null];let pub=0,active=0,busy=false,failure=null,initialized=false,last=null,currentFormat=0,lastSave=0;
 const stats={format:'RGB565-LE',frames:0,unchanged:0,uploadedBytes:0,acknowledged:0,started:new Date().toISOString()};
 async function push(bytes,w,h,format=0,metrics){
  dimensions(w,h,format);if(frameBytes(w,h,format)>video.capacity)throw Error('Frame exceeds video storage');if(!Buffer.isBuffer(bytes)||bytes.length!==frameBytes(w,h,format))throw Error('Invalid frame length');
  if(busy)throw Error('Frame upload already in progress');if(failure)throw Error(failure);busy=true;
  try{
   const ownEpoch=visibilityEpoch;
   if(!initialized){const header=await rpc.read(mapped+HEADER,32);pub=header.readUInt32LE(0);active=header.readUInt32LE(12)&1;currentFormat=header.readUInt32LE(24);initialized=true;}
   stats.requests=(stats.requests||0)+1;stats.lastInputAt=new Date().toISOString();
   if(last&&last.w===w&&last.h===h&&last.format===format&&last.bytes.equals(bytes)){
    stats.unchanged++;if(Date.now()-lastSave>1000){await save(stats);lastSave=Date.now();}return {skipped:true,...stats};
   }
   if(metrics)await rpc.write(mapped+HEADER+104,metrics);
   const slot=active^1,range=changed(slots[slot],bytes),start=performance.now();
   if(range)await rpc.write(video.base+slot*video.stride+range[0],bytes.subarray(...range));
   if(!last||last.w!==w||last.h!==h){
    const dims=Buffer.alloc(8);dims.writeUInt32LE(w);dims.writeUInt32LE(h,4);await rpc.write(mapped+HEADER+4,dims);
   }
   if(currentFormat!==format){const f=Buffer.alloc(4);f.writeUInt32LE(format);await rpc.write(mapped+HEADER+24,f);}
   pub=((((pub>>>1)+1)&0x7fffffff)<<1|slot)>>>0;if(pub===0)pub=2;
   const commit=Buffer.alloc(4);commit.writeUInt32LE(pub);await rpc.write(mapped+HEADER,commit);
   const deadline=Date.now()+2500;let ack;
   do{ack=(await rpc.read(mapped+HEADER+12,4)).readUInt32LE();if(ack===pub)break;await sleep(8);}while(Date.now()<deadline);
   if(ack!==pub)throw Error('Frame acknowledgement timed out; uploads stopped to protect the active buffer');
   slots[slot]=Buffer.from(bytes);last={w,h,format,bytes:slots[slot]};active=slot;currentFormat=format;
   if(ownEpoch===visibilityEpoch)await onFreshness(true,'fresh-frame-acknowledged');
   stats.frames++;stats.acknowledged=pub;stats.uploadedBytes+=range?range[1]-range[0]:0;
   stats.width=w;stats.height=h;stats.format=formatName(format);stats.lastMs=+(performance.now()-start).toFixed(2);stats.lastAt=new Date().toISOString();
   stats.lastBytes=range?range[1]-range[0]:0;await save(stats);lastSave=Date.now();return {...stats};
  }catch(error){failure=error.message;stats.error=failure;await save(stats);throw error;}
  finally{busy=false;}
 }
 return {push,stats,invalidate:async reason=>{visibilityEpoch++;last=null;await onFreshness(false,reason);},close:async()=>{visibilityEpoch++;await onFreshness(false,'stream-stopped');}};
}

module.exports={uploader,changed,bars,dimensions,frameBytes,formatName,SLOT,STRIDE,HEADER,MAX};
