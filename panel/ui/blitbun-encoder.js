'use strict';

importScripts('/blitbun-codec.js');

const ready = (async () => {
  const response = await fetch('/blitbun-codec.wasm');
  if (!response.ok) throw Error('Could not load the BlitBun frame encoder.');
  self.IZFrame.installWasm(await response.arrayBuffer());
})();
ready.catch(() => {});

let surface,context;
self.onmessage = async ({data:{bitmap,rgba,width,height}}) => {
  try {
    await ready;
    if(bitmap) {
      if(!surface) {
        surface=new OffscreenCanvas(width,height);
        context=surface.getContext('2d',{alpha:false,willReadFrequently:true});
      } else if(surface.width!==width||surface.height!==height) {
        surface.width=width;
        surface.height=height;
      }
      const scale=Math.min(width/bitmap.displayWidth,height/bitmap.displayHeight);
      const w=Math.round(bitmap.displayWidth*scale),h=Math.round(bitmap.displayHeight*scale);
      context.imageSmoothingQuality='high';
      context.fillStyle='#000';
      context.fillRect(0,0,width,height);
      context.drawImage(bitmap,(width-w)/2,(height-h)/2,w,h);
      rgba=context.getImageData(0,0,width,height).data;
    }
    const encoded = self.IZFrame.encode(rgba,width,height,1);
    self.postMessage({encoded},[encoded.buffer]);
  } catch (error) {
    self.postMessage({error:error.message});
  } finally {bitmap?.close();}
};
