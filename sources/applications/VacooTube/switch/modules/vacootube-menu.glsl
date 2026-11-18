// VacooTube-only navigation. Shared BlitBun shader fragments stay unchanged.
bool vacooMenuActive(){return c.atlas[SB+40u]==4u;}
#ifndef HB_READONLY
void vacooMenuInput(uint pressed){
 pressed=streamMenuPressed(pressed);
 bool held=c.touch[2]==1u&&(c.touch[6]&2u)==0u,tap=held&&c.atlas[CB+2u]==0u;vec2 p=vec2(c.touch[8],c.touch[9])*1.5;
 uint menu=c.atlas[SB+25u],selected=c.atlas[SB+39u],previous=menu;
 if((pressed&HB_MINUS)!=0u){menu=menu==0u?1u:0u;selected=0u;}
 else if(menu!=0u){
  int action=uiMenuInput(pressed&~HB_MINUS,held,tap,p,menu,selected,previous==2u?6u:previous==3u?2u:4u);
  if((pressed&HB_B)!=0u&&(previous==2u||previous==3u)){menu=1u;selected=previous==3u?2u:1u;}
  if(action>=0){
   if(previous==2u){const uint heights[6]=uint[6](1080u,720u,540u,480u,360u,240u);c.atlas[SB+86u]=heights[action];c.transition.z=float(48+action);c.transition.w=c.transition.w>=1000000.0?1.0:c.transition.w+1.0;menu=1u;selected=1u;}
   else if(previous==3u){c.atlas[SB+87u]=uint(action);c.transition.z=float(56+action);c.transition.w=c.transition.w>=1000000.0?1.0:c.transition.w+1.0;menu=1u;selected=2u;}
   else if(action==0){menu=0u;selected=0u;}
   else if(action==1){menu=2u;selected=1u;const uint heights[6]=uint[6](1080u,720u,540u,480u,360u,240u);for(uint i=0u;i<6u;i++)if(heights[i]==c.atlas[SB+86u])selected=i;}
   else if(action==2){menu=3u;selected=min(c.atlas[SB+87u],1u);}
   else{c.atlas[CB+1u]=0u;c.library.w=held?1u:0u;menu=0u;}
  }
 }
 c.atlas[SB+25u]=menu;c.atlas[SB+39u]=selected;c.atlas[CB+2u]=held?1u:0u;c.state.y=hbButtons();
}
#else
float vacooText(vec2 p,vec2 origin,uint label,float scale){
 const uint chars[37]=uint[37](86u,65u,67u,79u,79u,84u,85u,66u,69u,82u,69u,78u,68u,69u,82u,32u,82u,69u,83u,79u,76u,85u,84u,73u,79u,78u,49u,48u,56u,48u,80u,55u,50u,48u,80u,53u,52u);
 if(label<2u){uint first=label==0u?0u:9u,count=label==0u?9u:17u;vec2 q=p-origin;int i=int(floor(q.x/(22.0*scale)));return i<0||i>=int(count)?0.0:uiGlyph(q-vec2(float(i)*22.0*scale,0),chars[first+uint(i)],scale);}
 if(label>=8u){const uint engineChars[28]=uint[28](82u,69u,78u,68u,69u,82u,32u,69u,78u,71u,73u,78u,69u,67u,76u,65u,83u,83u,73u,67u,65u,68u,86u,65u,78u,67u,69u,68u);uint first=label==8u?0u:label==9u?13u:20u,count=label==8u?13u:label==9u?7u:8u;vec2 q=p-origin;int i=int(floor(q.x/(22.0*scale)));return i<0||i>=int(count)?0.0:uiGlyph(q-vec2(float(i)*22.0*scale,0),engineChars[first+uint(i)],scale);}
 const uint heights[6]=uint[6](1080u,720u,540u,480u,360u,240u);uint n=heights[label-2u],count=n==1080u?4u:3u;float ink=0.0;for(uint i=0u;i<count;i++){uint divisor=i==0u?(count==4u?1000u:100u):i==1u?(count==4u?100u:10u):i==2u?(count==4u?10u:1u):1u;ink=max(ink,uiGlyph(p-origin-vec2(float(i)*22.0*scale,0),48u+(n/divisor)%10u,scale));}return max(ink,uiGlyph(p-origin-vec2(float(count)*22.0*scale,0),80u,scale));
}
vec3 vacooMenuDraw(vec2 p){
 uint menu=c.atlas[SB+25u];bool ready=c.atlas[SB+63u]==0u&&c.atlas[SB+41u]==1u&&c.atlas[SB+69u]==1u;vec3 color=vec3(0);ivec2 size=ivec2(c.atlas[SB+4u],c.atlas[SB+5u]);
 if(ready&&c.atlas[SB+3u]!=0u&&all(greaterThan(size,ivec2(0)))){vec2 uv=p*vec2(size)/vec2(1920,1080)-.5;ivec2 q=ivec2(floor(uv));color=all(equal(size,ivec2(1920,1080)))?streamPixel(ivec2(p),size):streamBilinear(q,size,fract(uv));}
 // No badge, HUD or invisible touch hot zone when the menu is closed.
 if(menu==0u)return color;
 vec3 lime=vec3(.835,1,0),ink=vec3(.949,.945,.91),muted=vec3(.62,.66,.62);color*=.88;float panel=uiFill(uiBox(p,vec2(268,540),vec2(268,540),24.0));color=mix(color,vec3(.045,.058,.047),panel*.9);
 color=mix(color,lime,uiLabel(p,vec2(48,70),1u,.8));color=mix(color,ink,vacooText(p,vec2(48,186),menu==2u?1u:menu==3u?8u:0u,.9));color=mix(color,muted,uiLabel(p,vec2(48,245),14u,.64));
 uint count=menu==2u?6u:menu==3u?2u:4u;for(uint i=0u;i<count;i++){float y=354.0+float(i)*88.0;bool focus=c.atlas[SB+39u]==i;float d=uiBox(p,vec2(274,y),vec2(230,36),13.0);if(focus){color=mix(color,lime,uiFill(d)*.13);color=mix(color,lime,(1.0-smoothstep(.5,1.5,abs(d)))*.65);}float text=menu==3u?vacooText(p,vec2(70,y-9),i+9u,.84):menu==2u?vacooText(p,vec2(70,y-9),i+2u,.84):i==1u?vacooText(p,vec2(70,y-9),1u,.75):i==2u?vacooText(p,vec2(70,y-9),8u,.75):uiLabel(p,vec2(70,y-9),i==0u?2u:3u,.84);color=mix(color,focus?lime:ink,text);}
 return mix(color,muted,uiLabel(p,vec2(48,1000),18u,.66));
}
#endif
