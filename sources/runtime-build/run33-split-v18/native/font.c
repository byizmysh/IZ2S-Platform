/* The same trusted DejaVu face used by Platform catalog-assets.py.
 * Only the embedded face is loaded. App names are bounded UTF-8 text. */
#include "font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../deps/plutovg/plutovg-stb-truetype.h"
static unsigned char *font_bytes;
static stbtt_fontinfo face;
uint32_t iz_ounce_font_open(const char*path){
 if(font_bytes)return 0xe890;
 FILE*f=fopen(path,"rb");if(!f)return 0xe891;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0xe891;}long size=ftell(f);
 if(size<1024||size>2*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 0xe891;}
 unsigned char*data=malloc((size_t)size);if(!data){fclose(f);return 0xe891;}
 size_t got=fread(data,1,size,f);int closed=fclose(f);
 if(got!=(size_t)size||closed||!stbtt_InitFont(&face,data,0)){free(data);return 0xe891;}
 font_bytes=data;return 0;
}
void iz_ounce_font_close(void){free(font_bytes);font_bytes=NULL;memset(&face,0,sizeof face);}
static unsigned next(const unsigned char*s,unsigned *at){
 unsigned i=*at,a=s[i++];if(a<128){*at=i;return a;}
 unsigned n=a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:0;
 unsigned v=a&((1u<<(6-n))-1u);
 if(!n){*at=i;return 0xfffd;}
 for(unsigned k=0;k<n;k++){if(i>=64||!s[i]||(s[i]&0xc0)!=0x80){*at=*at+1;return 0xfffd;}v=(v<<6)|(s[i++]&63);}
 *at=i;if((n==1&&v<128)||(n==2&&v<2048)||(n==3&&v<65536)||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0xfffd;return v;
}
static void render(uint8_t*tile,unsigned stride,unsigned rows,const char*name,float size,uint32_t dims[2]){
 unsigned text[64],count=0,at=0;
 while(at<64&&name[at]&&count<63)text[count++]=next((const unsigned char*)name,&at);
 float scale=stbtt_ScaleForMappingEmToPixels(&face,size),pen=0,limit=stride-4.0f;
 int min_y=0,max_y=0,width=0;unsigned used=0;
 for(unsigned i=0;i<count;i++){
  int advance,lsb,x0,y0,x1,y1;stbtt_GetCodepointHMetrics(&face,text[i],&advance,&lsb);
  stbtt_GetCodepointBitmapBox(&face,text[i],scale,scale,&x0,&y0,&x1,&y1);
  if(pen+x1>limit){text[i]=0x2026;count=i+1;stbtt_GetCodepointHMetrics(&face,text[i],&advance,&lsb);stbtt_GetCodepointBitmapBox(&face,text[i],scale,scale,&x0,&y0,&x1,&y1);if(pen+x1>limit)break;}
  if(y0<min_y)min_y=y0;if(y1>max_y)max_y=y1;
  int right=(int)ceilf(pen)+x1;if(right>width)width=right;
  pen+=advance*scale;used++;
 }
 memset(tile,0,(size_t)stride*rows);pen=0;unsigned char glyph[4096];
 for(unsigned i=0;i<used;i++){
  int advance,lsb,x0,y0,x1,y1;stbtt_GetCodepointHMetrics(&face,text[i],&advance,&lsb);stbtt_GetCodepointBitmapBox(&face,text[i],scale,scale,&x0,&y0,&x1,&y1);
  int w=x1-x0,h=y1-y0;if(w>0&&h>0&&w<=64&&h<=64){
   stbtt_MakeCodepointBitmap(&face,glyph,w,h,w,scale,scale,text[i]);
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){int dx=(int)floorf(pen)+x0+x,dy=y0-min_y+y;if(dx>=0&&(unsigned)dx<stride&&dy>=0&&(unsigned)dy<rows){unsigned char*p=tile+dy*stride+dx;if(glyph[y*w+x]>*p)*p=glyph[y*w+x];}}
  }pen+=advance*scale;
 }
 dims[0]=(uint32_t)(width>0?width:1);dims[1]=(uint32_t)(max_y-min_y>0?max_y-min_y:1);
 if(dims[0]>stride)dims[0]=stride;if(dims[1]>rows)dims[1]=rows;
}
static float title_width(const unsigned *text,unsigned count,float scale){
 float width=0;
 for(unsigned i=0;i<count;i++){int advance,lsb;stbtt_GetCodepointHMetrics(&face,text[i],&advance,&lsb);width+=advance*scale;if(i+1<count)width+=stbtt_GetCodepointKernAdvance(&face,text[i],text[i+1])*scale;}
 return width;
}
static void title_line(uint8_t *tile,const unsigned *text,unsigned count,float scale,float baseline){
 float pen=(216.0f-title_width(text,count,scale))*.5f;
 unsigned char glyph[4096];
 for(unsigned i=0;i<count;i++){
  int advance,lsb,x0,y0,x1,y1;float shift=pen-floorf(pen);
  stbtt_GetCodepointHMetrics(&face,text[i],&advance,&lsb);
  stbtt_GetCodepointBitmapBoxSubpixel(&face,text[i],scale,scale,shift,0,&x0,&y0,&x1,&y1);
  int w=x1-x0,h=y1-y0;
  if(w>0&&h>0&&w<=64&&h<=64){
   stbtt_MakeCodepointBitmapSubpixel(&face,glyph,w,h,w,scale,scale,shift,0,text[i]);
   for(int y=0;y<h;y++)for(int x=0;x<w;x++){
    int dx=(int)floorf(pen)+x0+x,dy=(int)baseline+y0+y;
    if(dx>=0&&dx<216&&dy>=0&&dy<60){uint8_t*p=tile+dy*512+dx;if(glyph[y*w+x]>*p)*p=glyph[y*w+x];}
   }
  }
  pen+=advance*scale;if(i+1<count)pen+=stbtt_GetCodepointKernAdvance(&face,text[i],text[i+1])*scale;
 }
}
/* Separate, native-resolution title raster; card captions retain their own layout. */
static void hero_title(uint8_t *tile,const char *name,const unsigned *text,unsigned count){
 static uint8_t pixels[512*72];
 float size=60.0f;
 float width=title_width(text,count,stbtt_ScaleForMappingEmToPixels(&face,size));
 if(width>500.0f)size*=500.0f/width;
 uint32_t dims[2];render(pixels,512,72,name,size,dims);
 uint32_t *packed=(uint32_t*)(tile+26624*4);
 memset(packed,0,512*72/4);
 for(unsigned y=0;y<dims[1];y++)for(unsigned x=0;x<dims[0];x++){
  unsigned n=y*512+x;packed[n/16]|=((pixels[n]*3u+127u)/255u)<<((n%16)*2);
 }
 memcpy(tile+25605*4,dims,sizeof dims);
}

void iz_ounce_title(void*output,const char*name){
 if(!font_bytes||!output||!name)return;
 uint8_t*tile=output;unsigned text[64],count=0,at=0;
 while(at<64&&name[at]&&count<63)text[count++]=next((const unsigned char*)name,&at);
 memset(tile,0,65536);
 const float scale=stbtt_ScaleForMappingEmToPixels(&face,24.0f);
 unsigned first=0,space=0;
 while(first<count&&title_width(text,first+1,scale)<=212.0f){if(text[first]==' ')space=first;first++;}
 if(first<count&&space)first=space;
 if(!first&&count)first=1;
 unsigned second=first;while(second<count&&text[second]==' ')second++;
 unsigned tail[64],tail_count=0;
 if(second<count){
  while(second+tail_count<count&&title_width(text+second,tail_count+1,scale)<=212.0f)tail_count++;
  memcpy(tail,text+second,tail_count*sizeof *tail);
  if(second+tail_count<count){
   while(tail_count){tail[tail_count]=0x2026;if(title_width(tail,tail_count+1,scale)<=212.0f)break;tail_count--;}
   tail[tail_count++]=0x2026;
  }
 }
 title_line(tile,text,first,scale,24.0f);
 if(tail_count)title_line(tile,tail,tail_count,scale,54.0f);
 uint32_t dims[2]={216,tail_count?60:30},marker=0x32465454;
 memcpy(tile+102400,dims,sizeof dims);memcpy(tile+102528,&marker,sizeof marker);
 hero_title(tile,name,text,count);
}
void iz_ounce_label(uint8_t*bitmap,unsigned width,unsigned height,const char*text,float size){
 if(!font_bytes||!bitmap||!text||width<8||!height)return;
 uint32_t dims[2];
 render(bitmap,width,height,text,size,dims);
}
