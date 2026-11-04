// Byte offsets relative to the 8 MiB binding-zero buffer.
const OFFSET=0x7f0000, SIZE=8192, MAGIC=0x48425344, VERSION=1;
const FIELD={magic:0,version:4,sequence:8,flags:12,error:16,padId:20,style:24,motionCount:28,
 pad:64,motion:128,keyboard:320,mouse:384,network:448,ir:480,padSources:640,
 request:2048,response:4096,padSweepHeader:12272,padSweep:12288};
const FLAGS={pad:1,motion:2,keyboard:4,mouse:8,network:16,joyMouse:32,padSources:64};
const OP={vibrate:1,nfc:2,udp:3,haptics:4};
const STATUS={ok:0,unsupported:1,invalid:2,native:3,timeout:4};
function word(n){const b=Buffer.alloc(4);b.writeUInt32LE(n);return b;}
function motion(b){if(b.length!==96)throw Error('Motion state must be 96 bytes');const floats=(offset,n)=>Array.from({length:n},(_,i)=>b.readFloatLE(offset+i*4));return {deltaNanoseconds:String(b.readBigUInt64LE()),sequence:String(b.readBigUInt64LE(8)),acceleration:floats(16,3),angularVelocity:floats(28,3),angle:floats(40,3),direction:floats(52,9),attributes:b.readUInt32LE(88)};}
module.exports={OFFSET,SIZE,MAGIC,VERSION,FIELD,FLAGS,OP,STATUS,word,motion};
