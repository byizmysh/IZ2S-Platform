// Opaque BC1: two RGB565 endpoints and sixteen 2-bit indices per 4x4 block.
// Decoding needs two uint loads per source pixel; no video decoder or textures.
(function(root){
 const pack=(r,g,b)=>(r>>3)<<11|(g>>2)<<5|b>>3;
 const color=v=>[(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31];
 let native;
 function installWasm(bytes){
  const e=new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;
  if(!e.memory||!e.encode_bc1||!e.__heap_base)throw Error('Invalid frame codec');
  native={...e,base:Number(e.__heap_base.value)};
 }
 // A complete 24-bit palette plus one byte per pixel. No color quantization.
 function indexed(rgba,w,h){
  if(rgba.length!==w*h*4)throw Error('Invalid image size');
  const out=new Uint8Array(1024+w*h),palette=new Map();let count=0;
  for(let i=0,p=1024;i<rgba.length;i+=4,p++){
   const rgb=rgba[i]|rgba[i+1]<<8|rgba[i+2]<<16;let index=palette.get(rgb);
   if(index===undefined){if(count===256)return null;index=count++;palette.set(rgb,index);const q=index*4;out[q]=rgba[i];out[q+1]=rgba[i+1];out[q+2]=rgba[i+2];out[q+3]=255;}
   out[p]=index;
  }
  return out;
 }
 function encode(rgba,w,h,format=1,{bgra=false}={}){
  if(rgba.length!==w*h*4)throw Error('Invalid image size');
  if(bgra&&(format!==1||!native?.encode_bc1_bgra))throw Error('BGRA requires the native BC1 encoder');
  if(format===2){const out=indexed(rgba,w,h);if(!out)throw Error('Frame exceeds 256 colors');return out;}
  if(format===0){const out=new Uint8Array(w*h*2);for(let i=0,j=0;i<rgba.length;i+=4,j+=2){const v=pack(rgba[i],rgba[i+1],rgba[i+2]);out[j]=v&255;out[j+1]=v>>8;}return out;}
  if(format!==1||w%4||h%4)throw Error('BC1 needs complete 4x4 blocks');
  if(native){
   if(w<4||h<4||w>1920||h>1080)throw Error('Invalid image size');
   const n=w*h*4,start=native.base,end=start+n,out=end;
   if(out+w*h/2>native.memory.buffer.byteLength)throw Error('Codec memory limit');
   new Uint8Array(native.memory.buffer,start,n).set(rgba);
   if(!(bgra?native.encode_bc1_bgra:native.encode_bc1)(start,out,w,h))throw Error('Invalid image size');
   return new Uint8Array(native.memory.buffer,out,w*h/2).slice();
  }
  const out=new Uint8Array(w*h/2),view=new DataView(out.buffer);let pos=0;
  for(let by=0;by<h;by+=4)for(let bx=0;bx<w;bx+=4){
   let r0=255,g0=255,b0=255,r1=0,g1=0,b1=0;
   for(let y=0;y<4;y++)for(let x=0;x<4;x++){
    const i=((by+y)*w+bx+x)*4,r=rgba[i],g=rgba[i+1],b=rgba[i+2];
    r0=Math.min(r0,r);r1=Math.max(r1,r);g0=Math.min(g0,g);g1=Math.max(g1,g);b0=Math.min(b0,b);b1=Math.max(b1,b);
   }
   const axis=r1-r0>=g1-g0&&r1-r0>=b1-b0?0:g1-g0>=b1-b0?1:2;
   let lo=256,hi=-1,li=0,hiIndex=0;
   for(let y=0;y<4;y++)for(let x=0;x<4;x++){const i=((by+y)*w+bx+x)*4,v=rgba[i+axis];if(v<lo){lo=v;li=i;}if(v>hi){hi=v;hiIndex=i;}}
   let a=pack(rgba[li],rgba[li+1],rgba[li+2]),b=pack(rgba[hiIndex],rgba[hiIndex+1],rgba[hiIndex+2]);
   if(a<b){const t=a;a=b;b=t;}const c=color(a),d=color(b),dr=d[0]-c[0],dg=d[1]-c[1],db=d[2]-c[2],norm=dr*dr+dg*dg+db*db;let bits=0;
   for(let y=0;y<4;y++)for(let x=0;x<4;x++){
    const i=((by+y)*w+bx+x)*4,q=norm?Math.max(0,Math.min(3,Math.round(((rgba[i]-c[0])*dr+(rgba[i+1]-c[1])*dg+(rgba[i+2]-c[2])*db)*3/norm))):0;
    const index=q===0?0:q===3?1:q+1;bits|=index<<((y*4+x)*2);
   }
   view.setUint16(pos,a,true);view.setUint16(pos+2,b,true);view.setUint32(pos+4,bits>>>0,true);pos+=8;
  }
  return out;
 }
 const api={encode,color,indexed,installWasm};if(typeof module==='object')module.exports=api;else root.IZFrame=api;
})(typeof window==='object'?window:typeof self==='object'?self:{});
