#include "focus-type.glsl"
float focusText(vec2 p,vec2 origin,ivec4 rect,float scale){
 vec2 d=(p-origin)/scale;ivec2 q=ivec2(floor(d));if(any(lessThan(d,vec2(0)))||any(greaterThanEqual(q,rect.zw)))return 0.0;
 uint pixel=uint(q.y+rect.y)*FOCUS_WIDTH+uint(q.x+rect.x);return float((c.atlas[FOCUS_TYPE+(pixel>>4u)]>>((pixel&15u)*2u))&3u)/3.0;
}
ivec4 focusDigit(uint n){if(n==0u)return F_NUM_0;if(n==1u)return F_NUM_1;if(n==2u)return F_NUM_2;if(n==3u)return F_NUM_3;if(n==4u)return F_NUM_4;if(n==5u)return F_NUM_5;if(n==6u)return F_NUM_6;if(n==7u)return F_NUM_7;if(n==8u)return F_NUM_8;return F_NUM_9;}
float focusNumber(vec2 p,vec2 origin,uint n){float m=0.0;uint div=n>=1000u?1000u:n>=100u?100u:n>=10u?10u:1u;for(int i=0;i<4;i++){m=max(m,focusText(p,origin,focusDigit((n/div)%10u),1.0));origin.x+=14.0;if(div==1u)break;div/=10u;}return m;}
float focusCounter(vec2 p,vec2 origin,uint current,uint count){float m=focusNumber(p,origin,current);float dx=current>=100u?48.0:current>=10u?34.0:20.0;m=max(m,focusText(p,origin+vec2(dx+4.0,0),F_NUM_10,1.0));return max(m,focusNumber(p,origin+vec2(dx+28.0,0),count));}
uint slotBase(uint index){return 524288u+index*32768u;}
float labelPixel(ivec2 q,uint base){uint width=c.atlas[base+25600u],height=c.atlas[base+25601u];if(any(lessThan(q,ivec2(0)))||q.x>=int(width)||q.y>=int(height))return 0.0;uint pixel=uint(q.y)*512u+uint(q.x);return float((c.atlas[base+(pixel>>2u)]>>((pixel&3u)*8u))&255u)/255.0;}
float focusLabel(vec2 p,vec2 origin,uint base,float scale){vec2 d=(p-origin)/scale;ivec2 q=ivec2(floor(d));vec2 f=fract(d);return mix(mix(labelPixel(q,base),labelPixel(q+ivec2(1,0),base),f.x),mix(labelPixel(q+ivec2(0,1),base),labelPixel(q+ivec2(1),base),f.x),f.y);}
vec4 focusIconPixel(ivec2 q,uint base){q=clamp(q,ivec2(0),ivec2(95));vec4 v=unpackUnorm4x8(c.atlas[base+16384u+uint(q.y*96+q.x)]);return vec4(v.rgb*v.a,v.a);}
vec4 focusIcon(vec2 p,vec2 center,uint base,float size){vec2 uv=(p-center)*96.0/size+47.5;if(any(lessThan(uv,vec2(-.5)))||any(greaterThanEqual(uv,vec2(95.5))))return vec4(0);ivec2 q=ivec2(floor(uv));vec2 f=fract(uv);vec4 v=mix(mix(focusIconPixel(q,base),focusIconPixel(q+ivec2(1,0),base),f.x),mix(focusIconPixel(q+ivec2(0,1),base),focusIconPixel(q+ivec2(1),base),f.x),f.y);if(v.a>0.0)v.rgb/=v.a;return v;}
float focusPlus(vec2 p,vec2 center,float size){return fill(min(box(p,center,vec2(size,2.1),1.8),box(p,center,vec2(2.1,size),1.8)));}

float heroLabel(vec2 p,vec2 origin,uint base){vec2 d=p-origin;ivec2 q=ivec2(floor(d));uint width=c.atlas[base+25605u],height=c.atlas[base+25606u];if(any(lessThan(d,vec2(0)))||q.x>=int(width)||q.y>=int(height))return 0.0;uint n=uint(q.y)*512u+uint(q.x);return float((c.atlas[base+26624u+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;}

float focusAscii(vec2 p,vec2 origin,uint ch){uint i=clamp(ch,32u,127u)-32u;return focusText(p,origin,ivec4(int(i%32u)*24,F_ASCII.y+int(i/32u)*32,24,32),1.0);}
float focusQueryText(vec2 p,vec2 origin){if(any(lessThan(p,origin))||any(greaterThanEqual(p-origin,vec2(400,32))))return 0.0;float m=0.0;for(uint i=0u;i<min(c.rows[2],16u);i++){uint ch=focusQuery(i);if(ch>=97u&&ch<=122u)ch-=32u;m=max(m,focusAscii(p,origin+vec2(float(i)*25.0,0),ch));}return m;}
