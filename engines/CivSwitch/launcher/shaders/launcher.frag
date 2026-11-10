#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#include "launcher-state.glsl"
#define HB_READONLY
#include "hb.glsl"
#include "launcher-text.glsl"
#include "iz-draw.glsl"
#include "launcher-layout.glsl"
#include "tiny-calc.glsl"
#include "tiny-calc-draw.glsl"
#include "pc-screen.glsl"
#include "pc-screen-draw.glsl"
float catalogText(vec2 p,vec2 origin,uint index,bool description){
 ivec2 q=ivec2(floor(p-origin));uint base=524288u+index*32768u,meta=base+25600u;
 uint w=c.atlas[meta+(description?2u:0u)],h=c.atlas[meta+(description?3u:1u)];
 if(any(lessThan(p-origin,vec2(0)))||q.x>=int(w)||q.y>=int(h))return 0.0;
 uint pixel=uint(q.y+(description?64:0))*512u+uint(q.x),word=c.atlas[base+(pixel>>2u)];
 return float((word>>((pixel&3u)*8u))&255u)/255.0;
}
vec4 iconPixel(ivec2 q,uint index){
 q=clamp(q,ivec2(0),ivec2(95));
 vec4 v=unpackUnorm4x8(c.atlas[524288u+index*32768u+16384u+uint(q.y*96+q.x)]);
 return vec4(v.rgb*v.a,v.a);
}
vec4 catalogIcon(vec2 p,vec2 center,uint index){
 vec2 uv=(p-center+vec2(104))*96.0/208.0-.5;
 if(any(lessThan(uv,vec2(-.5)))||any(greaterThanEqual(uv,vec2(95.5))))return vec4(0);
 ivec2 q=ivec2(floor(uv));vec2 f=fract(uv);
 vec4 v=mix(mix(iconPixel(q,index),iconPixel(q+ivec2(1,0),index),f.x),mix(iconPixel(q+ivec2(0,1),index),iconPixel(q+ivec2(1),index),f.x),f.y);
 if(v.a>0.0)v.rgb/=v.a;return v;
}
float polygon(vec2 p,vec2 v[24],int n){
 float d=dot(p-v[0],p-v[0]),s=1.0;
 for(int i=0,j=n-1;i<n;j=i,i++){
  vec2 e=v[j]-v[i],w=p-v[i];vec2 b=w-e*clamp(dot(w,e)/dot(e,e),0.0,1.0);d=min(d,dot(b,b));
  bvec3 test=bvec3(p.y>=v[i].y,p.y<v[j].y,e.x*w.y>e.y*w.x);
  if(all(test)||all(not(test)))s=-s;
 }
 return s*sqrt(d);
}
// Outline sampled from the site's iz-mouse SVG. No texture or sprite frames.
float mouse(vec2 q,float t){
 vec2 v[24]=vec2[24](vec2(69,103),vec2(72,91),vec2(78,80),vec2(86,71),vec2(99,63),vec2(113,60),
  vec2(124,56),vec2(135,56),vec2(146,59),vec2(155,63),vec2(162,68),vec2(181,84),
  vec2(173,92),vec2(163,98),vec2(152,104),vec2(139,108),vec2(124,110),vec2(99,110),
  vec2(85,110),vec2(73,110),vec2(70,109),vec2(69,107),vec2(68.8,105),vec2(69,103.2));
 float body=min(polygon(q,v,24),length(q-vec2(119,48))-25.0);
 float m=fill(body);
 m*=1.0-fill(length(q-vec2(156,80))-3.7);
 float ear=1e4;
 vec2 prev=vec2(108,49);
 for(int i=1;i<=6;i++){
  float u=float(i)/6.0;vec2 next=pow(1.0-u,3.0)*vec2(108,49)+3.0*pow(1.0-u,2.0)*u*vec2(108,39)+3.0*(1.0-u)*u*u*vec2(119,35)+u*u*u*vec2(128,41);
  ear=min(ear,stroke(q,prev,next));prev=next;
 }
 m*=1.0-fill(ear-2.25);
 float wiggle=2.8*sin(t*8.72664626);
 float tail=min(stroke(q,vec2(12,91+wiggle),vec2(36,91+wiggle)),stroke(q,vec2(36,91+wiggle),vec2(44,96)));
 tail=min(tail,stroke(q,vec2(44,96),vec2(44,105)));tail=min(tail,stroke(q,vec2(44,105),vec2(51,110)));
 tail=min(tail,stroke(q,vec2(51,110),vec2(76,110)));
 m=max(m,fill(tail-3.0));m=max(m,fill(length(q-vec2(12,91+wiggle))-5.0));
 // The tilted little crown and dark chevron from the reference author card.
 vec2 crown[24];crown[0]=vec2(130,56);crown[1]=vec2(150,63);crown[2]=vec2(171,61);crown[3]=vec2(162,56);crown[4]=vec2(162,35);crown[5]=vec2(154,39);crown[6]=vec2(145,34);crown[7]=vec2(140,51);
 float hat=polygon(q,crown,8);m=m*(1.0-fill(hat))+line(hat,1.1);
 float chevron=min(stroke(q,vec2(145,72),vec2(132,66)),stroke(q,vec2(132,66),vec2(139,88)));
 m*=1.0-fill(chevron-3.1);
 return clamp(m,0.0,1.0);
}
void main(){
 if(c.atlas[CB+7u]==CMAGIC&&c.atlas[CB+1u]==1u){outColor=vec4(calcDraw(gl_FragCoord.xy),1);return;}
 if(c.atlas[CB+7u]==CMAGIC&&c.atlas[CB+1u]==2u){outColor=vec4(streamDraw(gl_FragCoord.xy),1);return;}
 vec2 p=gl_FragCoord.xy;float t=c.motion.x;vec3 col=BG;
 // Quiet ruled sections and restrained panels follow the website palette.
 col=mix(col,vec3(.19),line(p.y-304.0,.5)*.65);
 col=mix(col,vec3(.19),line(p.y-952.0,.5)*.65);
 col=mix(col,INK,text(p,vec2(IZ_CONTENT_LEFT,144),TEXT_TITLE));
 float phase=mod(t,18.0)*.34906585,velocity=sin(phase);
 float dir=velocity>=0.0?1.0:-1.0;
 float facing=2.0*smoothstep(-.08,.08,velocity)-1.0;
 float progress=.5-.5*cos(phase),mx=mix(1072.0,1648.0,progress);
 col=mix(col,vec3(.23),line(p.y-248.0,.5)*step(960.0,p.x)*step(p.x,IZ_CONTENT_RIGHT));
 const float mouseScale=1.12;
 float bob=7.0*sin(t*8.37758041),nominalNose=mx+facing*85.0*mouseScale;
 float nearestCrumb=1136.0+80.0*clamp(round((nominalNose-1136.0)/80.0),0.0,6.0);
 float tilt=.30*(1.0-smoothstep(0.0,32.0,abs(nominalNose-nearestCrumb)))*abs(facing);
 float nose=mx+facing*(85.0*cos(tilt)-14.0*sin(tilt))*mouseScale;
 for(int i=0;i<7;i++){
  float x=1136.0+float(i)*80.0,y=224.0+mod(float(i)*7.0,11.0),distance=(x-nose)*dir;
  float visible=smoothstep(-8.0,5.0,distance)*smoothstep(0.0,.1,abs(velocity));
  vec2 crumb=vec2(x,y);
  col=mix(col,BLUE,fill(length(p-crumb)-(3.6+mod(float(i),3.0)*.85))*visible*.85);
 }
 if(abs(facing)>.02){
  vec2 q=(p-vec2(mx,180.0+bob))/mouseScale;q.x/=facing;
  q=mat2(cos(tilt),-sin(tilt),sin(tilt),cos(tilt))*q+vec2(96,70);
  if(q.x>=5.0&&q.x<=184.0&&q.y>=20.0&&q.y<=115.0)col=mix(col,BLUE,mouse(q,t));
 }
 uint count=min(c.library.x,32u),page=(c.state.x/IZ_PAGE_SIZE)*IZ_PAGE_SIZE;
 bool canOpen=count>0u&&c.atlas[524288u+c.state.x*32768u+25604u]!=1u;
 for(int i=0;i<int(IZ_PAGE_SIZE);i++){
  uint index=page+uint(i);if(index>=count)continue;
  vec2 center=izTileCenter(uint(i));float selected=c.state.x==index?1.0:0.0;
  float d=izTileDistance(p,center);
  col=mix(col,PANEL,fill(d));col=mix(col,BLUE,fill(d)*selected*.025);
  col=mix(col,mix(vec3(.22),BLUE,selected),line(d,.65+selected*.6));
  if(index<c.reserved.w){
   vec4 icon=catalogIcon(p,center,index);col=mix(col,icon.rgb,icon.a);
   float width=float(c.atlas[524288u+index*32768u+25600u]);
   float scale=min(1.0,232.0/max(width,1.0));
   vec2 origin=vec2(center.x-width*scale*.5,IZ_CAPTION_Y);
   col=mix(col,INK,catalogText((p-origin)/scale,vec2(0),index,false));
  }else{
   float plus=min(box(p,center,vec2(25,2),1.0),box(p,center,vec2(2,25),1.0));
   col=mix(col,MUTED,fill(plus));
  }
 }
 if(count==0u){
  col=mix(col,INK,text(p,vec2(960.0-float(TEXT_NOTITLES.z)*.5,616),TEXT_NOTITLES));
  col=mix(col,MUTED,text(p,vec2(960.0-float(TEXT_EMPTYHINT.z)*.5,672),TEXT_EMPTYHINT));
 }else if(canOpen){
  vec2 button=vec2(IZ_CONTENT_LEFT+14.0,IZ_FOOTER_Y);
  col=mix(col,BLUE,line(length(p-button)-14.0,1.0));
  col=mix(col,BLUE,text(p,button-vec2(TEXT_BUTTON_A.zw)*.5,TEXT_BUTTON_A));
  col=mix(col,INK,text(p,vec2(button.x+28.0,IZ_FOOTER_Y-float(TEXT_SELECT.w)*.5),TEXT_SELECT));
 }
 vec2 backButton=vec2(IZ_CONTENT_LEFT+14.0+(canOpen?128.0:0.0),IZ_FOOTER_Y);
 col=mix(col,BLUE,line(length(p-backButton)-14.0,1.0));
 col=mix(col,BLUE,text(p,backButton-vec2(TEXT_BUTTON_B.zw)*.5,TEXT_BUTTON_B));
 col=mix(col,INK,text(p,vec2(backButton.x+28.0,IZ_FOOTER_Y-float(TEXT_BACK.w)*.5),TEXT_BACK));
 if(c.motion.z>.001){
  float a=c.motion.z;col=mix(col,BG,a*.8);float d=box(p,vec2(960,540),vec2(390,154),7.0);
  col=mix(col,PANEL,fill(d)*a);col=mix(col,BLUE,line(d,.7)*a);
  col=mix(col,INK,text(p,vec2(795,461),TEXT_EMPTY)*a);
  col=mix(col,MUTED,text(p,vec2(769,527),TEXT_DETAIL)*a);col=mix(col,BLUE,text(p,vec2(878,611),TEXT_CLOSE)*a);
 }
 if(hbHas(HB_JOY_MOUSE)&&c.game.x!=0u&&c.stats.w!=0u){
  vec2 v=p-c.flow.xy;float d=min(stroke(v,vec2(0),vec2(0,28)),min(stroke(v,vec2(0),vec2(22,20)),stroke(v,vec2(0,28),vec2(22,20))));
  col=mix(col,BG,fill(d-4.0));col=mix(col,BLUE,fill(d-1.7));
 }
 col=mix(col,MUTED,text(p,vec2(IZ_CONTENT_RIGHT-float(TEXT_CREDIT.z),IZ_FOOTER_Y-float(TEXT_CREDIT.w)*.5),TEXT_CREDIT));
 outColor=vec4(col*(1.0-c.transition.x),1.0);
}
