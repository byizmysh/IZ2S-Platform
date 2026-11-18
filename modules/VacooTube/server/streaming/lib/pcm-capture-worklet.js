// Selected capture track only. 25 ms stereo S16LE packets; no microphone.
class IzPcmCapture extends AudioWorkletProcessor{
 constructor(){super();this.bytes=new ArrayBuffer(4800);this.view=new DataView(this.bytes);this.position=0;}
 process(inputs){
  const channels=inputs[0];if(!channels?.length)return true;const left=channels[0],right=channels[1]||left;
  for(let i=0;i<left.length;i++){
   const l=Math.max(-1,Math.min(1,left[i]||0)),r=Math.max(-1,Math.min(1,right[i]||0));
   this.view.setInt16(this.position,Math.round(l*(l<0?32768:32767)),true);this.view.setInt16(this.position+2,Math.round(r*(r<0?32768:32767)),true);this.position+=4;
   if(this.position===4800){this.port.postMessage(this.bytes,[this.bytes]);this.bytes=new ArrayBuffer(4800);this.view=new DataView(this.bytes);this.position=0;}
  }
  return true;
 }
}
registerProcessor('iz-pcm-capture',IzPcmCapture);
