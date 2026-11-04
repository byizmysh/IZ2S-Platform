const crypto=require('node:crypto');
const {MAGIC,BYTES}=require('./passport.cjs');
const CHALLENGE=1,PROOF=2,ACK=3,MAX=BYTES+32;
const sizes={1:32,2:BYTES+32,3:40};
function frame(type,payload){
 if(!Buffer.isBuffer(payload)||payload.length!==sizes[type])throw Error('Invalid recovery frame');
 const header=Buffer.alloc(8);header.writeUInt32LE(MAGIC);header.writeUInt16LE(type,4);header.writeUInt16LE(payload.length,6);
 return Buffer.concat([header,payload]);
}
function parser(onFrame){
 let buffer=Buffer.alloc(0);
 return bytes=>{
  // Bound buffering even when an attacker sends many records in one chunk.
  while(bytes.length){
   const count=Math.min(bytes.length,MAX+8-buffer.length);
   if(count<=0)throw Error('Recovery frame exceeds limit');
   buffer=Buffer.concat([buffer,bytes.subarray(0,count)]);bytes=bytes.subarray(count);
   while(buffer.length>=8){
    const type=buffer.readUInt16LE(4),size=buffer.readUInt16LE(6);
    if(buffer.readUInt32LE()!==MAGIC||size!==sizes[type])throw Error('Invalid recovery frame header');
    if(buffer.length<8+size)break;
    const payload=Buffer.from(buffer.subarray(8,8+size));buffer=buffer.subarray(8+size);onFrame(type,payload);
   }
  }
 };
}
function mac(secret,kind,nonce,body){return crypto.createHmac('sha256',secret).update('RUN18 status '+kind+' v1').update(nonce).update(body).digest();}
function equal(a,b){return a.length===b.length&&crypto.timingSafeEqual(a,b);}
function proof(secret,nonce,book){return Buffer.concat([book,mac(secret,'proof',nonce,book)]);}
function verifyProof(secret,nonce,bytes){return bytes.length===MAX&&equal(bytes.subarray(BYTES),mac(secret,'proof',nonce,bytes.subarray(0,BYTES)));}
function ack(secret,nonce,sequence){const b=Buffer.alloc(8);b.writeBigUInt64LE(BigInt(sequence));return Buffer.concat([b,mac(secret,'ack',nonce,b)]);}
function verifyAck(secret,nonce,bytes){return bytes.length===40&&equal(bytes.subarray(8),mac(secret,'ack',nonce,bytes.subarray(0,8)));}
module.exports={CHALLENGE,PROOF,ACK,frame,parser,proof,verifyProof,ack,verifyAck};
