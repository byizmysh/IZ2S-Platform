/* PC-only WebAssembly SIMD BC1 encoder. The console receives the same BC1 blocks. */
#include <wasm_simd128.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
static u16 quant(const u8 *p){return ((p[0]*31+127)/255)<<11|((p[1]*63+127)/255)<<5|((p[2]*31+127)/255);}
static v128_t channel(v128_t p,int shift){
 return wasm_f32x4_convert_u32x4(wasm_v128_and(wasm_u32x4_shr(p,shift),wasm_i32x4_splat(255)));
}
__attribute__((visibility("default"))) int encode_bc1(const u8 *rgba,u8 *output,int w,int h){
 if(w<4||h<4||w>1920||h>1080||(w&3)||(h&3))return 0;
 for(int by=0;by<h;by+=4)for(int bx=0;bx<w;bx+=4){
  const u8 *src=rgba+(by*w+bx)*4;v128_t row[4];
  for(int y=0;y<4;y++)row[y]=wasm_v128_load(src+y*w*4);
  v128_t lo=wasm_u8x16_min(wasm_u8x16_min(row[0],row[1]),wasm_u8x16_min(row[2],row[3]));
  v128_t hi=wasm_u8x16_max(wasm_u8x16_max(row[0],row[1]),wasm_u8x16_max(row[2],row[3]));
  lo=wasm_u8x16_min(lo,wasm_i32x4_shuffle(lo,lo,2,3,0,1));hi=wasm_u8x16_max(hi,wasm_i32x4_shuffle(hi,hi,2,3,0,1));
  lo=wasm_u8x16_min(lo,wasm_i32x4_shuffle(lo,lo,1,0,3,2));hi=wasm_u8x16_max(hi,wasm_i32x4_shuffle(hi,hi,1,0,3,2));
  int range=wasm_i32x4_extract_lane(wasm_i8x16_sub(hi,lo),0);
  int r=range&255,g=(range>>8)&255,b=(range>>16)&255,axis=r>=g&&r>=b?0:g>=b?1:2;
  const u8 *lp=src,*hp=src;int lv=src[axis],hv=lv;
  for(int y=0;y<4;y++)for(int x=0;x<4;x++){const u8 *p=src+(y*w+x)*4;int v=p[axis];if(v<lv){lv=v;lp=p;}if(v>hv){hv=v;hp=p;}}
  u16 a=quant(lp),bq=quant(hp);if(a<bq){u16 t=a;a=bq;bq=t;}
  float cr=(a>>11)*255.f/31,cg=((a>>5)&63)*255.f/63,cb=(a&31)*255.f/31;
  float dr=(bq>>11)*255.f/31-cr,dg=((bq>>5)&63)*255.f/63-cg,db=(bq&31)*255.f/31-cb;
  float norm=dr*dr+dg*dg+db*db,scale=norm>0?3/norm:0;u32 bits=0;
  for(int y=0;y<4;y++){
   v128_t dot=wasm_f32x4_add(wasm_f32x4_add(wasm_f32x4_mul(wasm_f32x4_sub(channel(row[y],0),wasm_f32x4_splat(cr)),wasm_f32x4_splat(dr)),wasm_f32x4_mul(wasm_f32x4_sub(channel(row[y],8),wasm_f32x4_splat(cg)),wasm_f32x4_splat(dg))),wasm_f32x4_mul(wasm_f32x4_sub(channel(row[y],16),wasm_f32x4_splat(cb)),wasm_f32x4_splat(db)));
   v128_t q=wasm_i32x4_trunc_sat_f32x4(wasm_f32x4_min(wasm_f32x4_splat(3),wasm_f32x4_max(wasm_f32x4_splat(0),wasm_f32x4_add(wasm_f32x4_mul(dot,wasm_f32x4_splat(scale)),wasm_f32x4_splat(.5f)))));
   v128_t idx=wasm_v128_and(wasm_i32x4_add(q,wasm_i32x4_splat(1)),wasm_i32x4_ne(q,wasm_i32x4_splat(0)));
   idx=wasm_v128_bitselect(wasm_i32x4_splat(1),idx,wasm_i32x4_eq(q,wasm_i32x4_splat(3)));
   u32 packed=wasm_i32x4_extract_lane(idx,0)|(wasm_i32x4_extract_lane(idx,1)<<2)|(wasm_i32x4_extract_lane(idx,2)<<4)|(wasm_i32x4_extract_lane(idx,3)<<6);bits|=packed<<(y*8);
  }
  output[0]=a;output[1]=a>>8;output[2]=bq;output[3]=bq>>8;output[4]=bits;output[5]=bits>>8;output[6]=bits>>16;output[7]=bits>>24;output+=8;
 }
 return 1;
}

/* X11's native BGRA input stays in that order until the owned WASM copy.
 * SIMD conversion avoids a scalar JavaScript loop and ffmpeg RGB conversion. */
__attribute__((visibility("default"))) int encode_bc1_bgra(u8 *bgra,u8 *output,int w,int h){
 if(w<4||h<4||w>1920||h>1080||(w&3)||(h&3))return 0;
 for(int i=0;i<w*h*4;i+=16){
  v128_t p=wasm_v128_load(bgra+i);
  wasm_v128_store(bgra+i,wasm_i8x16_shuffle(p,p,2,1,0,3,6,5,4,7,10,9,8,11,14,13,12,15));
 }
 return encode_bc1(bgra,output,w,h);
}
