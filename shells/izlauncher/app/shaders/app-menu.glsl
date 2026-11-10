// Common translucent application navigation, using the main launcher type atlas.
const uint AM=(0x701800u-1024u)/4u;
#ifndef HB_READONLY
int uiMenuInput(uint pressed,bool held,bool tap,vec2 p,inout uint menu,inout uint selected,uint count){
 bool badge=tap&&all(lessThanEqual(abs(p-vec2(120,90)),vec2(92,36)));
 bool close=tap&&menu!=0u&&all(lessThanEqual(abs(p-vec2(492,90)),vec2(30)));
 if((pressed&HB_MINUS)!=0u||badge||close){menu=menu==0u?1u:0u;selected=0u;return -1;}
 if(menu==0u)return -1;
 if((pressed&HB_B)!=0u){menu=0u;return -1;}
 int move=(pressed&HB_UP)!=0u?-1:(pressed&HB_DOWN)!=0u?1:0;
 if(c.atlas[AM+3u]>0u)c.atlas[AM+3u]--;vec2 stick=hbStick(false);
 if(abs(stick.y)>.55&&c.atlas[AM+3u]==0u){move=stick.y>0.0?-1:1;c.atlas[AM+3u]=12u;}
 if(move!=0)selected=uint((int(selected)+move+int(count))%int(count));
 if(tap&&p.x>=40.0&&p.x<=510.0&&p.y>=310.0&&p.y<310.0+float(count)*88.0){selected=uint((p.y-310.0)/88.0);return int(selected);}
 if((pressed&HB_A)!=0u)return int(selected);
 return -1;
}
#else
float uiBox(vec2 p,vec2 center,vec2 size,float radius){vec2 q=abs(p-center)-size+radius;return min(max(q.x,q.y),0.0)+length(max(q,0.0))-radius;}
float uiFill(float d){return 1.0-smoothstep(-.8,.8,d);}
float uiPixel(ivec2 q){if(any(lessThan(q,ivec2(0)))||q.x>=768||q.y>=F_ASCII.y+96)return 0.0;uint i=uint(q.y*768+q.x);return float((c.atlas[16384u+(i>>4u)]>>((i&15u)*2u))&3u)/3.0;}
float uiGlyph(vec2 p,uint ch,float scale){
 if(ch<32u||ch>127u)return 0.0;if(ch==46u||ch==44u)p.y-=19.0*scale;if(ch==45u)p.y-=10.0*scale;
 vec2 v=p/scale;if(any(lessThan(v,vec2(0)))||v.x>=24.0||v.y>=25.0)return 0.0;
 uint i=ch-32u;vec2 at=v+vec2(float(i%32u)*24.0,float(F_ASCII.y)+float(i/32u)*32.0);ivec2 q=ivec2(floor(at));vec2 f=fract(at);return mix(mix(uiPixel(q),uiPixel(q+ivec2(1,0)),f.x),mix(uiPixel(q+ivec2(0,1)),uiPixel(q+ivec2(1)),f.x),f.y);
}
const uint UI_CHARS[191]=uint[191](77u,69u,78u,85u,73u,90u,76u,65u,85u,78u,67u,72u,69u,82u,67u,79u,78u,84u,73u,78u,85u,69u,82u,69u,84u,85u,82u,78u,32u,84u,79u,32u,76u,65u,85u,78u,67u,72u,69u,82u,65u,32u,83u,69u,76u,69u,67u,84u,32u,32u,32u,66u,32u,66u,65u,67u,75u,80u,67u,32u,83u,84u,82u,69u,65u,77u,68u,79u,79u,77u,72u,65u,76u,70u,45u,76u,73u,70u,69u,32u,50u,89u,79u,85u,84u,85u,66u,69u,83u,72u,65u,82u,69u,32u,80u,67u,32u,87u,73u,78u,68u,79u,87u,83u,84u,65u,82u,84u,32u,68u,79u,79u,77u,67u,65u,76u,67u,85u,76u,65u,84u,79u,82u,82u,69u,65u,68u,89u,32u,84u,79u,32u,67u,65u,76u,67u,85u,76u,65u,84u,69u,70u,73u,76u,69u,83u,85u,80u,32u,47u,32u,68u,79u,87u,78u,32u,84u,79u,32u,67u,72u,79u,79u,83u,69u,67u,76u,69u,65u,82u,77u,69u,77u,79u,82u,89u,45u,32u,77u,69u,78u,85u,50u,88u,32u,45u,32u,77u,69u,78u,85u);
const uvec2 UI_LABELS[19]=uvec2[19](uvec2(0u,4u),uvec2(4u,10u),uvec2(14u,8u),uvec2(22u,18u),uvec2(40u,17u),uvec2(57u,9u),uvec2(66u,4u),uvec2(70u,11u),uvec2(81u,7u),uvec2(88u,15u),uvec2(103u,10u),uvec2(113u,10u),uvec2(123u,18u),uvec2(141u,5u),uvec2(146u,19u),uvec2(165u,5u),uvec2(170u,6u),uvec2(176u,6u),uvec2(182u,9u));
float uiLabel(vec2 p,vec2 origin,uint label,float scale){uvec2 item=UI_LABELS[label];vec2 q=p-origin;int i=int(floor(q.x/(22.0*scale)));if(i<0||i>=int(item.y))return 0.0;return uiGlyph(q-vec2(float(i)*22.0*scale,0),UI_CHARS[item.x+uint(i)],scale);}
vec3 uiMenuDrawWithHint(vec2 p,vec3 color,uint menu,uint selected,uint title,bool start,bool doubleMinus){
 vec3 lime=vec3(.835,1,0),ink=vec3(.949,.945,.91),muted=vec3(.62,.66,.62);
 if(menu==0u){float badge=uiFill(uiBox(p,vec2(120,90),vec2(92,36),27.0));color=mix(color,vec3(.04,.055,.045),badge*.68);color=mix(color,ink,uiLabel(p,doubleMinus?vec2(80,81):vec2(91,79),doubleMinus?18u:0u,doubleMinus?.54:.8));for(int i=0;i<3;i++)color=mix(color,lime,uiFill(uiBox(p,vec2(60,79+float(i)*10.0),vec2(12,1),1.0)));return color;}
 color*=.88;float panel=uiFill(uiBox(p,vec2(268,540),vec2(268,540),24.0));color=mix(color,vec3(.045,.058,.047),panel*.9);
 color=mix(color,lime,uiLabel(p,vec2(48,70),1u,.8));color=mix(color,ink,uiLabel(p,vec2(48,186),title,1.05));color=mix(color,muted,uiLabel(p,vec2(48,245),14u,.64));
 for(int j=0;j<2;j++){vec2 a=vec2(492,90),v=p-a;float l=abs(v.y-(j==0?v.x:-v.x))*.707;float mark=(1.0-smoothstep(1.0,2.6,l))*step(abs(v.x),12.0);color=mix(color,ink,mark);}
 uint count=start?3u:2u;for(uint i=0u;i<count;i++){float y=354.0+float(i)*88.0;bool focus=selected==i;float d=uiBox(p,vec2(274,y),vec2(230,36),13.0);if(focus){color=mix(color,lime,uiFill(d)*.13);color=mix(color,lime,(1.0-smoothstep(.5,1.5,abs(d)))*.65);color=mix(color,lime,uiFill(uiBox(p,vec2(48,y),vec2(2,14),2.0)));}uint label=start?(i==0u?9u:i==1u?10u:3u):(i==0u?2u:3u);color=mix(color,focus?lime:ink,uiLabel(p,vec2(70,y-9),label,.84));}
 color=mix(color,muted,uiLabel(p,vec2(48,956),4u,.78));return color;
}
vec3 uiMenuDraw(vec2 p,vec3 color,uint menu,uint selected,uint title,bool start){return uiMenuDrawWithHint(p,color,menu,selected,title,start,false);}
#endif
