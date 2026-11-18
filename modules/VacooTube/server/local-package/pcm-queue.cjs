// Bounded interleaved S16 stereo queue. Whole sample frames only.
class PcmQueue{
 constructor(maxBytes=38400){this.maxBytes=maxBytes;this.bytes=Buffer.alloc(0);this.tail=Buffer.alloc(0);this.received=0;this.dropped=0;this.nonzero=0;this.ended=false;}
 push(part){this.received+=part.length;const joined=Buffer.concat([this.tail,part]),n=joined.length&~3;this.tail=Buffer.from(joined.subarray(n));if(!n)return;const pcm=joined.subarray(0,n);if(pcm.some(v=>v!==0))this.nonzero++;this.bytes=Buffer.concat([this.bytes,pcm]);if(this.bytes.length>this.maxBytes){const excess=this.bytes.length-this.maxBytes;this.bytes=Buffer.from(this.bytes.subarray(excess));this.dropped+=excess;}}
 take(n){if(n%4||n>this.bytes.length)return null;const b=Buffer.from(this.bytes.subarray(0,n));this.bytes=this.bytes.subarray(n);return b;}
 clear(){this.dropped+=this.bytes.length;this.bytes=Buffer.alloc(0);}
 get length(){return this.bytes.length;}
}
module.exports={PcmQueue};
