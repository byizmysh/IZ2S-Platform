// Data-only post-menu protocol: independent raw DEFLATE blocks with a dictionary
// from the same inactive slot. The console uses its existing zlib routines.
const zlib=require('node:zlib');
// Stay below zlib's usable dictionary distance (32768 - 262). At 32768,
// same-position temporal matches fell outside the compressor's search window.
const BLOCK=32256,STAGING=0x520000,ALLOCATION=0x900000,HEAP_DICTIONARY=0x700000,HEAP_STRIDE=0x100000,MAX_FRAME=1920*1080/2,MAGIC=0x315a4449;
// Native pointer updates retain the high word. A power-of-two alignment that
// covers the entire arena prevents its offsets from crossing a 4 GiB boundary.
const ALLOCATION_ALIGNMENT=2**Math.ceil(Math.log2(ALLOCATION));
const table=Uint32Array.from({length:256},(_,n)=>{for(let k=0;k<8;k++)n=(n&1)?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
function crc32(bytes){if(typeof zlib.crc32==='function')return zlib.crc32(bytes)>>>0;let n=0xffffffff;for(const b of bytes)n=table[(n^b)&255]^(n>>>8);return (n^0xffffffff)>>>0;}
function layout(bytes){
 if(!Number.isInteger(bytes)||bytes<8||bytes>MAX_FRAME)throw Error('Invalid delta frame size');
 const count=Math.ceil(bytes/BLOCK),header=64+count*16,maxPacket=header+bytes+count*64;
 if(STAGING+maxPacket+0x10000>HEAP_DICTIONARY||HEAP_DICTIONARY+2*HEAP_STRIDE>ALLOCATION||MAX_FRAME+64>HEAP_STRIDE)throw Error('Delta staging exceeds allocation');
 return {bytes,count,header,maxPacket,blocks:Array.from({length:count},(_,i)=>({offset:i*BLOCK,length:Math.min(BLOCK,bytes-i*BLOCK)}))};
}
function validateBase(base,bytes){const p=layout(bytes);if(!Number.isSafeInteger(base)||base<4096||base%4096||base+ALLOCATION>=2**39||Math.floor(base/2**32)!==Math.floor((base+ALLOCATION-1)/2**32))throw Error('Delta allocation crosses pointer-word boundary');return p;}
function seed(base,bytes){const p=validateBase(base,bytes),out=Buffer.alloc(p.maxPacket);out.writeUInt32LE(MAGIC);out.writeUInt32LE(p.count,4);for(let i=0;i<p.count;i++)out.writeBigUInt64LE(BigInt(base+STAGING+p.header),64+i*16);return out;}
function incompressible(raw,dictionary){
 const counts=new Uint16Array(256);let matches=0,total=0;
 for(let i=0;i<raw.length;i+=31){counts[raw[i]]++;matches+=raw[i]===dictionary[i];total++;}
 if(matches/total>.1)return false;let entropy=0;
 for(const n of counts)if(n){const p=n/total;entropy-=p*Math.log2(p);}
 return entropy>7.4;
}
function literal(raw){const b=Buffer.allocUnsafe(raw.length+5);b[0]=1;b.writeUInt16LE(raw.length,1);b.writeUInt16LE((~raw.length)&65535,3);raw.copy(b,5);return b;}
function temporalMatch(raw,dictionary,ratio=.25,stride=128){let same=0,count=0;for(let i=0;i+4<=raw.length;i+=stride){same+=raw.readUInt32LE(i)===dictionary.readUInt32LE(i);count++;}return count>0&&same/count>=ratio;}
// Ethernet can trade a few extra lossless bytes for less host work. On Wi-Fi
// preserve temporal savings; otherwise a smaller CPU time can mean slower wire.
function latencyPolicy(networkType){return {budgetMs:networkType==='ethernet'?4:8,temporalRatio:networkType==='ethernet'?.75:.25,temporalStride:124};}
function encode(frame,before,{base,metrics=Buffer.alloc(32),sequence=0,keyframe=false,level=1,codec=null,budgetMs=Infinity,temporalRatio=.25,temporalStride=128,now=()=>performance.now()}={}){
 if(![0,1,3,6].includes(level))throw Error('Invalid compression level');
 if(!(budgetMs>=0)||!Number.isFinite(temporalRatio)||temporalRatio<0||temporalRatio>1||!Number.isInteger(temporalStride)||temporalStride<4||temporalStride>1024)throw Error('Invalid compression budget');
 if(!Buffer.isBuffer(frame)||!Buffer.isBuffer(before)||before.length!==frame.length||!Buffer.isBuffer(metrics)||metrics.length!==32)throw Error('Invalid delta input');
 const began=now(),p=validateBase(base,frame.length),parts=[];let length=p.header,changedBlocks=0,literalBlocks=0,dictionaryBlocks=0,fastBlocks=0,dictionarySkippedBlocks=0;
 const checksum=codec?codec.crc32(frame):crc32(frame);
 const header=Buffer.alloc(p.header);header.writeUInt32LE(MAGIC);header.writeUInt32LE(p.count,4);header.writeUInt32LE(checksum,8);header.writeUInt32LE(sequence>>>0,12);header.writeUInt32LE(frame.length,16);metrics.copy(header,32);
 for(let i=0;i<p.count;i++){
  const {offset,length:n}=p.blocks[i],raw=frame.subarray(offset,offset+n),dictionary=before.subarray(offset,offset+n);const changed=keyframe||!raw.equals(dictionary);if(changed)changedBlocks++;
  const exhausted=now()-began>=budgetMs;
  let compressed;
  if(!changed)compressed=Buffer.from([3,0]);
  else if(level===0||incompressible(raw,dictionary)){compressed=literal(raw);literalBlocks++;}
  else{
   const temporal=!keyframe&&temporalMatch(raw,dictionary,temporalRatio,temporalStride);
   if(codec&&(keyframe||exhausted||!temporal)){
    compressed=codec.deflate(raw,level);fastBlocks++;if(exhausted&&temporal)dictionarySkippedBlocks++;
   }else if(exhausted){compressed=literal(raw);literalBlocks++;dictionarySkippedBlocks++;}
   else{compressed=zlib.deflateRawSync(raw,{level,...(!keyframe?{dictionary}:{})});dictionaryBlocks++;}
  }
  // A stored block is smaller and cheaper to decode than unsuccessful DEFLATE.
  if(changed&&compressed.length>n+5){compressed=literal(raw);literalBlocks++;}
  if(compressed.length<2||compressed.length>n+64||length+compressed.length>p.maxPacket)throw Error('Compressed block exceeds bound');
  header.writeBigUInt64LE(BigInt(base+STAGING+length),64+i*16);header.writeUInt32LE(compressed.length,72+i*16);header.writeUInt32LE(changed?n:0,76+i*16);parts.push(compressed);length+=compressed.length;
 }
 header.writeUInt32LE(length-p.header,20);
 return {packet:Buffer.concat([header,...parts]),rawBytes:frame.length,changedBlocks,literalBlocks,dictionaryBlocks,fastBlocks,dictionarySkippedBlocks,dictionaryBudgetExceeded:dictionarySkippedBlocks>0,compressionBudgetMs:Number.isFinite(budgetMs)?budgetMs:null,blocks:p.count,keyframe,compressionLevel:level,compressionMode:level===0?'stored changed blocks':codec?'hybrid temporal DEFLATE':'dictionary DEFLATE',compressionBackend:codec?'libdeflate + zlib':'zlib',crc:header.readUInt32LE(8)};
}
function decode(packet,before,{base}={}){
 if(!Buffer.isBuffer(packet)||!Buffer.isBuffer(before))throw Error('Invalid delta packet');const p=validateBase(base,before.length);
 if(packet.length<p.header||packet.length>p.maxPacket||packet.readUInt32LE()!==MAGIC||packet.readUInt32LE(4)!==p.count||packet.readUInt32LE(16)!==before.length||packet.readUInt32LE(20)!==packet.length-p.header)throw Error('Invalid delta header');
 const out=Buffer.alloc(before.length);let end=p.header;
 for(let i=0;i<p.count;i++){
  const b=p.blocks[i],at=Number(packet.readBigUInt64LE(64+i*16))-base-STAGING,n=packet.readUInt32LE(72+i*16),copyLength=packet.readUInt32LE(76+i*16);
  if(at!==end||n<2||n>b.length+64||at+n>packet.length||![0,b.length].includes(copyLength))throw Error('Invalid delta block span');
  const raw=zlib.inflateRawSync(packet.subarray(at,at+n),{dictionary:before.subarray(b.offset,b.offset+b.length),maxOutputLength:b.length});
  if(raw.length!==copyLength)throw Error('Delta output length differs');(copyLength?raw:before.subarray(b.offset,b.offset+b.length)).copy(out,b.offset);end=at+n;
 }
 if(end!==packet.length||crc32(out)!==packet.readUInt32LE(8))throw Error('Delta frame checksum differs');return out;
}
module.exports={BLOCK,STAGING,ALLOCATION,ALLOCATION_ALIGNMENT,HEAP_DICTIONARY,HEAP_STRIDE,MAX_FRAME,MAGIC,layout,validateBase,seed,encode,decode,crc32,latencyPolicy};
