#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#define HB_READONLY
#include "abi.glsl"
#include "reader.glsl"
#include "labels.glsl"
float inkPixel(ivec2 p){if(any(lessThan(p,ivec2(0)))||p.x>=1024||p.y>=1920)return 0.0;return float(readerByte(c.atlas[2]+uint(p.y*1024+p.x)))/255.0;}
float glyph(vec2 p,vec2 origin,uint ch,uint face,float pixels){
 if(ch<32u||ch>127u)return 0.0;vec2 q=(p-origin)*48.0/pixels;if(any(lessThan(q,vec2(0)))||q.x>=64.0||q.y>=80.0)return 0.0;uint index=face*96u+ch-32u;vec2 at=q+vec2(index%16u*64u,index/16u*80u);ivec2 v=ivec2(floor(at));vec2 f=fract(at);return mix(mix(inkPixel(v),inkPixel(v+ivec2(1,0)),f.x),mix(inkPixel(v+ivec2(0,1)),inkPixel(v+ivec2(1,1)),f.x),f.y);
}
float label(vec2 p,vec2 origin,uint id,float pixels){
 uvec2 item=LABEL_INDEX[id];if(p.y<origin.y||p.y>origin.y+pixels*1.7||p.x<origin.x||p.x>origin.x+float(item.y)*pixels)return 0.0;float pen=0.0;
 for(uint i=0u;i<item.y;i++){uint ch=LABEL_TEXT[item.x+i];float advance=float(c.atlas[c.atlas[5]+192u+ch-32u])/64.0*pixels/48.0;if(p.x-origin.x<pen+advance)return glyph(p,origin+vec2(pen-4.0*pixels/48.0,0),ch,2u,pixels);pen+=advance;}return 0.0;
}
float title(vec2 p,vec2 origin,uint at,uint length,float pixels){
 if(p.y<origin.y||p.y>origin.y+pixels*1.7||p.x<origin.x||p.x>origin.x+float(length)*pixels)return 0.0;float pen=0.0;
 for(uint i=0u;i<min(length,64u);i++){uint ch=clamp(readerByte(at+i),32u,127u);float advance=float(c.atlas[c.atlas[5]+192u+ch-32u])/64.0*pixels/48.0;if(p.x-origin.x<pen+advance)return glyph(p,origin+vec2(pen-4.0*pixels/48.0,0),ch,2u,pixels);pen+=advance;}return 0.0;
}
float number(vec2 p,vec2 origin,uint value,float pixels){uint digits=value>=10000u?5u:value>=1000u?4u:value>=100u?3u:value>=10u?2u:1u;float result=0.0;for(uint i=0u;i<digits;i++){uint div=1u;for(uint j=i+1u;j<digits;j++)div*=10u;result=max(result,glyph(p,origin+vec2(float(i)*pixels*.65,0),48u+(value/div)%10u,2u,pixels));}return result;}
float rounded(vec2 p,vec2 center,vec2 halfSize,float radius){vec2 q=abs(p-center)-halfSize+radius;return min(max(q.x,q.y),0.0)+length(max(q,0.0))-radius;}
float box(vec2 p,vec2 center,vec2 halfSize,float radius){return 1.0-smoothstep(-.8,.8,rounded(p,center,halfSize,radius));}
float body(vec2 p){
 float top=140.0,size=float(c.atlas[16]),spacing=c.atlas[21]==0u?1.25:c.atlas[21]==2u?1.65:1.45,lineHeight=size*spacing,margin=float(c.atlas[22]);
 if(p.x<margin||p.x>=readerSize().x-margin||p.y<top||p.y>=readerSize().y-90.0)return 0.0;
 uint row=uint((p.y-top)/lineHeight),count=c.atlas[readerMeta()+1u];if(row>=count||row>=64u)return 0.0;
 uint length=c.atlas[512u+row],first=c.atlas[24]+row*193u,lo=0u,hi=length;float x=(p.x-margin)*64.0;
 for(int n=0;n<8;n++){if(lo>=hi)break;uint mid=(lo+hi)/2u;if(float(c.atlas[first+mid+1u])<=x)lo=mid+1u;else hi=mid;}
 if(lo>=length)return 0.0;uint ch=c.atlas[c.atlas[23]+row*192u+lo];float pen=float(c.atlas[first+lo])/64.0;
 return glyph(p,vec2(margin+pen-4.0*size/48.0,top+float(row)*lineHeight),ch,readerFace(),size);
}
void main(){
 vec2 p=readerPoint(gl_FragCoord.xy),sz=readerSize();vec3 lime=vec3(.82,.97,.36),dark=vec3(.065,.09,.072),muted=vec3(.59,.65,.57),ink=vec3(.94,.94,.89),color=dark;
 if(c.game.y==0u){
  color=mix(color,lime,label(p,vec2(64,48),0u,24.0));color=mix(color,ink,label(p,vec2(64,112),1u,50.0));color=mix(color,muted,label(p,vec2(64,181),2u,23.0));
  uint visible=min(12u,max(1u,uint((sz.y-300.0)/104.0))),start=c.game.z/visible*visible;
  for(uint row=0u;row<visible;row++){uint i=start+row;if(i>=c.atlas[13])break;float y=274.0+float(row)*104.0;bool selected=i==c.game.z;float panel=box(p,vec2(sz.x*.5,y),vec2(sz.x*.5-56.0,44),14.0);color=mix(color,selected?vec3(.20,.27,.15):vec3(.105,.14,.11),panel);color=mix(color,lime,box(p,vec2(94,y),vec2(15,23),5.0));for(int line=0;line<3;line++)color=mix(color,dark,box(p,vec2(96,y-9.0+float(line)*8),vec2(8,1),1.0));uint m=c.atlas[14]+i*2u;color=mix(color,selected?lime:ink,title(p,vec2(136,y-22),c.atlas[m],c.atlas[m+1u],30.0));}
  if(c.atlas[13]==0u)color=mix(color,muted,label(p,vec2(64,280),30u,26.0));color=mix(color,muted,label(p,vec2(64,sz.y-64),3u,22.0));
 }else{
  uint theme=c.atlas[20];vec3 paper=theme==2u?vec3(.075,.11,.09):theme==1u?vec3(.91,.84,.70):vec3(.965,.955,.92),text=theme==2u?vec3(.82,.86,.78):theme==1u?vec3(.25,.18,.11):vec3(.13,.16,.12);color=paper;
  color=mix(color,text*.75,title(p,vec2(max(float(c.atlas[22]),205.0),31),c.atlas[29],c.atlas[30],24));if(abs(p.y-112.0)<.6&&p.x>c.atlas[22]&&p.x<sz.x-c.atlas[22])color=mix(paper,text,.15);
  if(c.game.w==readerPage()+1u)color=mix(color,text,body(p));else color=mix(color,text,label(p,vec2(float(c.atlas[22]),160),31u,26));
  float progress=float(readerPage()+1u)/float(readerPageCount());if(p.y>sz.y-5.0&&p.x<sz.x*progress)color=theme==2u?lime:vec3(.42,.54,.24);
  color=mix(color,text*.8,number(p,vec2(sz.x*.5-62.0,sz.y-58),readerPage()+1u,24));color=mix(color,text*.8,glyph(p,vec2(sz.x*.5-4.0,sz.y-58),47u,2u,24));color=mix(color,text*.8,number(p,vec2(sz.x*.5+20.0,sz.y-58),readerPageCount(),24));
 }
 // A compact visible menu button, usable in either screen orientation.
 float badge=box(p,vec2(108,49),vec2(62,25),18);if(c.game.y==1u){color=mix(color,dark,badge*.9);color=mix(color,lime,label(p,vec2(75,31),4u,21));}
 if(c.bagMeta.x!=0u){
  color*=.6;float panel=box(p,vec2(285,sz.y*.5),vec2(285,sz.y*.5),24);color=mix(color,vec3(.07,.105,.077),panel*.98);color=mix(color,lime,label(p,vec2(42,48),0u,24));color=mix(color,ink,label(p,vec2(42,114),4u,46));color=mix(color,muted,label(p,vec2(42,184),29u,20));
  const uint ids[10]=uint[10](5u,6u,7u,8u,9u,10u,11u,12u,13u,14u);
  for(uint row=0u;row<10u;row++){float y=282.0+float(row)*65.0;bool focus=c.bagMeta.y==row;if(focus)color=mix(color,lime,box(p,vec2(282,y),vec2(250,28),10)*.16);color=mix(color,focus?lime:ink,label(p,vec2(50,y-18),ids[row],25));uint value=0u;
   if(row==2u)color=mix(color,muted,number(p,vec2(432,y-16),c.atlas[16],23));
   if(row==3u)value=15u+c.atlas[17];if(row==4u)value=17u+c.atlas[18];if(row==6u)value=c.atlas[19]==0u?19u:c.atlas[19]==90u?20u:21u;if(row==7u)value=22u+c.atlas[20];if(row==8u)value=c.atlas[22]==48u?25u:c.atlas[22]==80u?26u:27u;
   if(value!=0u)color=mix(color,muted,label(p,vec2(326,y-15),value,17));
   if(row==5u){uint fraction=c.atlas[21]==0u?25u:c.atlas[21]==2u?65u:45u;float n=number(p,vec2(423,y-16),1u,20);n=max(n,glyph(p,vec2(435,y-16),46u,2u,20));n=max(n,number(p,vec2(445,y-16),fraction,20));color=mix(color,muted,n);}
  }
  color=mix(color,muted,label(p,vec2(42,sz.y-65),28u,22));
 }
 if(c.rows[3]!=0u){float toast=box(p,vec2(sz.x*.5,sz.y-112.0),vec2(min(sz.x*.5-32.0,460.0),35.0),14.0);color=mix(color,vec3(.28,.09,.085),toast*.98);color=mix(color,vec3(1.0,.82,.75),label(p,vec2(sz.x*.5-270.0,sz.y-127.0),34u,23.0));}
 outColor=vec4(color,1);
}
