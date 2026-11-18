'use strict';
// A bounded UI thumbnail. Never resize, retain, or transfer the encoder's frame.
const WIDTH=320,HEIGHT=180;
function thumbnail(frame){
 if(!frame||!Number.isInteger(frame.w)||!Number.isInteger(frame.h)||frame.w<1||frame.h<1||frame.w>8192||frame.h>8192||!Buffer.isBuffer(frame.rgba)||frame.rgba.length!==frame.w*frame.h*4)return null;
 const output=Buffer.alloc(WIDTH*HEIGHT*4),scale=Math.min(WIDTH/frame.w,HEIGHT/frame.h),w=Math.max(1,Math.floor(frame.w*scale)),h=Math.max(1,Math.floor(frame.h*scale)),left=Math.floor((WIDTH-w)/2),top=Math.floor((HEIGHT-h)/2);
 for(let i=3;i<output.length;i+=4)output[i]=255;
 const source=frame.rgba;
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  const i=(Math.min(frame.h-1,Math.floor(y/scale))*frame.w+Math.min(frame.w-1,Math.floor(x/scale)))*4,j=((top+y)*WIDTH+left+x)*4;
  output[j]=source[i+(frame.bgra?2:0)];output[j+1]=source[i+1];output[j+2]=source[i+(frame.bgra?0:2)];
 }
 return output;
}
module.exports={thumbnail,WIDTH,HEIGHT};
