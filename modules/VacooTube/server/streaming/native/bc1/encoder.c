/* PC-only BC1 encoder. Same endpoints, quantization and projection as SIMD WASM.
 * Read native BGRA in place: no full-frame copy or channel conversion pass. */
#include <node_api.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
typedef float f4 __attribute__((vector_size(16)));
typedef uint32_t u4 __attribute__((vector_size(16)));
typedef int32_t i4 __attribute__((vector_size(16)));
typedef uint8_t b16 __attribute__((vector_size(16)));
static b16 min8(b16 a,b16 b){return a^((a^b)&(b<a));}
static b16 max8(b16 a,b16 b){return a^((a^b)&(b>a));}
static uint16_t quant(const uint8_t *p,int red,int blue){return ((p[red]*31+127)/255)<<11|((p[1]*63+127)/255)<<5|((p[blue]*31+127)/255);}
static napi_value fail(napi_env env,const char *text){napi_throw_error(env,NULL,text);return NULL;}
typedef struct {const uint8_t *rgba;uint8_t *output;int w,start,end,red,blue;} Region;
static void *region(void *opaque){
 Region *r=opaque;const uint8_t *rgba=r->rgba;uint8_t *output=r->output+(size_t)r->start*r->w/2;int w=r->w,red=r->red,blue=r->blue;
 for(int by=r->start;by<r->end;by+=4)for(int bx=0;bx<w;bx+=4){
  const uint8_t *src=rgba+(by*w+bx)*4,*lp=src,*hp=src;b16 rows[4];for(int y=0;y<4;y++)memcpy(&rows[y],src+y*w*4,16);
  b16 lo=min8(min8(rows[0],rows[1]),min8(rows[2],rows[3])),hi=max8(max8(rows[0],rows[1]),max8(rows[2],rows[3]));
  lo=min8(lo,(b16)__builtin_shuffle((u4)lo,(u4){2,3,0,1}));hi=max8(hi,(b16)__builtin_shuffle((u4)hi,(u4){2,3,0,1}));
  lo=min8(lo,(b16)__builtin_shuffle((u4)lo,(u4){1,0,3,2}));hi=max8(hi,(b16)__builtin_shuffle((u4)hi,(u4){1,0,3,2}));
  int r=hi[red]-lo[red],g=hi[1]-lo[1],b=hi[blue]-lo[blue],axis=r>=g&&r>=b?red:g>=b?1:blue,lv=src[axis],hv=lv;
  for(int y=0;y<4;y++)for(int x=0;x<4;x++){const uint8_t *p=src+(y*w+x)*4;int v=p[axis];if(v<lv){lv=v;lp=p;}if(v>hv){hv=v;hp=p;}}
  uint16_t a=quant(lp,red,blue),bq=quant(hp,red,blue);if(a<bq){uint16_t t=a;a=bq;bq=t;}
  float cr=(a>>11)*255.f/31,cg=((a>>5)&63)*255.f/63,cb=(a&31)*255.f/31;
  float dr=(bq>>11)*255.f/31-cr,dg=((bq>>5)&63)*255.f/63-cg,db=(bq&31)*255.f/31-cb;
  float norm=dr*dr+dg*dg+db*db,scale=norm>0?3/norm:0;uint32_t bits=0;
  for(int y=0;y<4;y++){
   u4 pixels=(u4)rows[y];f4 vr=__builtin_convertvector((pixels>>(red*8))&255,f4),vg=__builtin_convertvector((pixels>>8)&255,f4),vb=__builtin_convertvector((pixels>>(blue*8))&255,f4);
   f4 q=(((vr-cr)*dr+(vg-cg)*dg)+(vb-cb)*db)*scale+.5f;
   i4 k=__builtin_convertvector(q,i4);k&=k>0;k=(k&~(k>3))|((i4){3,3,3,3}&(k>3));
   i4 idx=(k+1)&(k!=0);idx=(idx&~(k==3))|((i4){1,1,1,1}&(k==3));
   uint32_t packed=idx[0]|(idx[1]<<2)|(idx[2]<<4)|(idx[3]<<6);bits|=packed<<(y*8);
  }
  output[0]=a;output[1]=a>>8;output[2]=bq;output[3]=bq>>8;output[4]=bits;output[5]=bits>>8;output[6]=bits>>16;output[7]=bits>>24;output+=8;
 }
 return NULL;
}
static napi_value encode(napi_env env,napi_callback_info info){
 size_t count=5,n;void *input;bool is_buffer,bgra;int32_t w,h,threads=2;napi_value args[5],out;
 if(napi_get_cb_info(env,info,&count,args,NULL,NULL)!=napi_ok||count<4||napi_is_buffer(env,args[0],&is_buffer)!=napi_ok||!is_buffer||napi_get_buffer_info(env,args[0],&input,&n)!=napi_ok||napi_get_value_int32(env,args[1],&w)!=napi_ok||napi_get_value_int32(env,args[2],&h)!=napi_ok||napi_get_value_bool(env,args[3],&bgra)!=napi_ok||w<4||h<4||w>1920||h>1080||(w&3)||(h&3)||n!=(size_t)w*h*4)return fail(env,"Invalid BC1 frame");
 if(count==5&&(napi_get_value_int32(env,args[4],&threads)!=napi_ok||(threads!=1&&threads!=2)))return fail(env,"Invalid BC1 thread limit");
 uint8_t *output;if(napi_create_buffer(env,(size_t)w*h/2,(void **)&output,&out)!=napi_ok)return fail(env,"BC1 allocation failed");

 int split=threads==2&&w*h>=1280*720?(h/8)*4:0;
 Region first={input,output,w,0,split?split:h,bgra?2:0,bgra?0:2},second={input,output,w,split,h,bgra?2:0,bgra?0:2};pthread_t thread;
 if(split&&pthread_create(&thread,NULL,region,&second)==0){region(&first);pthread_join(thread,NULL);}else{first.end=h;region(&first);}
 return out;
}
static napi_value init(napi_env env,napi_value exports){napi_property_descriptor methods[]={{"encode",NULL,encode,NULL,NULL,NULL,napi_default,NULL}};if(napi_define_properties(env,exports,1,methods)!=napi_ok)return fail(env,"BC1 exports failed");return exports;}
NAPI_MODULE(NODE_GYP_MODULE_NAME,init)
