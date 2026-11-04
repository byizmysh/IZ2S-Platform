#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#include "abi.glsl"
#define HB_READONLY
#include "hb.glsl"
#include "exchange.glsl"
uint atlasByte(uint offset){return (c.atlas[offset/4u]>>((offset%4u)*8u))&255u;}
float glyph(vec2 q,uint ch){if(ch<32u||ch>126u)return 0.0;uint x=(ch-32u)%16u*8u+uint(q.x)%8u,y=(ch-32u)/16u*14u+uint(q.y),pixel=y*128u+x;return float((c.atlas[c.atlas[3]+pixel/16u]>>((pixel%16u)*2u))&3u)/3.0;}
float label(vec2 p,vec2 origin,uint id,float scale){uint meta=c.atlas[1]+id*2u,len=c.atlas[meta+1u];vec2 q=(p-origin)/scale;if(q.x<0.0||q.y<0.0||q.x>=float(len*8u)||q.y>=14.0)return 0.0;return glyph(q,atlasByte(c.atlas[2]+c.atlas[meta]+uint(q.x)/8u));}
float dynamicText(vec2 p,vec2 origin,uint offset,uint limit,float scale){vec2 q=(p-origin)/scale;if(q.x<0.0||q.y<0.0||q.x>=float(limit*8u)||q.y>=14.0)return 0.0;uint at=offset+uint(q.x)/8u;return glyph(q,(fx(at&~3u)>>((at%4u)*8u))&255u);}
float number(vec2 p,vec2 origin,uint n,float scale){vec2 q=(p-origin)/scale;if(q.x<0.0||q.y<0.0||q.y>=14.0)return 0.0;uint length=1u,v=n;while(v>=10u&&length<10u){v/=10u;length++;}uint cell=uint(q.x)/8u;if(cell>=length)return 0.0;uint div=1u;for(uint i=cell+1u;i<length;i++)div*=10u;return glyph(q,48u+(n/div)%10u);}
bool rect(vec2 p,vec2 a,vec2 b){return all(greaterThanEqual(p,a))&&all(lessThan(p,b));}
void main(){
 vec2 p=gl_FragCoord.xy;vec3 ink=vec3(.92,.94,.88),muted=vec3(.57,.63,.54),lime=vec3(.78,1,.15),panel=vec3(.105,.13,.10),color=vec3(.055,.075,.055);
 if(rect(p,vec2(56,190),vec2(1328,942))||rect(p,vec2(1360,190),vec2(1864,942)))color=panel;
 color=mix(color,ink,label(p,vec2(64,52),0u,4.1));color=mix(color,lime,label(p,vec2(1440,80),1u,1.9));
 color=mix(color,muted,label(p,vec2(80,216),2u,1.6));
 if(fxReady())color=mix(color,lime,dynamicText(p,vec2(80,250),96u,75u,1.8));
 else color=mix(color,lime,label(p,vec2(80,250),9u,1.8));
 uint count=fxCount();
 for(uint row=0u;row<count;row++){
  float y=286.0+float(row)*50.0;uint at=256u+row*256u;bool folder=fx(at)==0u;
  if(row==c.game.y&&rect(p,vec2(72,y),vec2(1312,y+44.0)))color=mix(panel,lime,.14);
  if(rect(p,vec2(88,y+12),vec2(105,y+28)))color=folder?lime:muted;
  color=mix(color,ink,dynamicText(p,vec2(124,y+9),at+16u,61u,1.65));
  if(fx(at)==1u)color=mix(color,muted,number(p,vec2(1120,y+12),((fx(at+12u)<<22u)|(fx(at+8u)>>10u)),1.3));
 }
 if(fxReady()){
  color=mix(color,muted,number(p,vec2(1120,216),fx(84u)+1u,1.5));
  color=mix(color,muted,number(p,vec2(1200,216),fx(88u),1.5));
 }
 color=mix(color,lime,label(p,vec2(1392,224),3u,2.1));uint status=fx(3584u);
 color=mix(color,status>0u?lime:muted,label(p,vec2(1392,276),!fxReady()||fx(72u)==3u?9u:status==0u?11u:status==2u?12u:10u,1.6));
 uint ip=fx(3596u);for(uint i=0u;i<4u;i++)color=mix(color,ink,number(p,vec2(1392.0+float(i)*92.0,334),((ip>>(i*8u))&255u),2.3));
 color=mix(color,lime,number(p,vec2(1392,386),fx(3588u),3.2));
 color=mix(color,muted,label(p,vec2(1392,456),13u,1.2));color=mix(color,muted,label(p,vec2(1392,490),14u,1.2));
 color=mix(color,muted,label(p,vec2(1392,564),16u,1.4));color=mix(color,ink,number(p,vec2(1392,602),((fx(3604u)<<22u)|(fx(3600u)>>10u)),2.3));
 color=mix(color,muted,label(p,vec2(1392,668),17u,1.4));color=mix(color,ink,number(p,vec2(1392,706),((fx(3612u)<<22u)|(fx(3608u)>>10u)),2.3));
 color=mix(color,muted,dynamicText(p,vec2(1392,800),3616u,32u,1.2));
 if(rect(p,vec2(1392,850),vec2(1832,918)))color=mix(panel,lime,.1);
 color=mix(color,lime,label(p,vec2(1430,876),18u,1.7));
 color=mix(color,muted,label(p,vec2(80,896),fx(3808u)>0u?19u:4u,1.5));color=mix(color,muted,label(p,vec2(690,896),fx(3808u)>0u?20u:5u,1.5));
 color=mix(color,vec3(1,.58,.40),dynamicText(p,vec2(80,946),3328u,105u,1.25));
 color=mix(color,ink,label(p,vec2(64,988),7u,1.7));color=mix(color,ink,label(p,vec2(1000,988),8u,1.4));
 color=mix(color,muted,label(p,vec2(64,1040),6u,1.2));outColor=vec4(color,1);
}
