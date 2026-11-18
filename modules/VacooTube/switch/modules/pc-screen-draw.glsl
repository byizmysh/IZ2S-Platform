vec3 streamColor(uint rgb){
 return vec3(float((rgb>>11u)&31u)/31.0,float((rgb>>5u)&63u)/63.0,float(rgb&31u)/31.0);
}
vec3 streamPixel(ivec2 p,ivec2 size){
 p=clamp(p,ivec2(0),size-1);uint base=VIDEO0+(c.atlas[SB+3u]&1u)*VIDEO_STRIDE;
 if(c.atlas[SB+7u]==2u){uint i=uint(p.y*size.x+p.x),index=(streamWord(base+256u+(i>>2u))>>((i&3u)*8u))&255u,rgba=streamWord(base+index);return vec3(rgba&255u,(rgba>>8u)&255u,(rgba>>16u)&255u)/255.0;}
 if(c.atlas[SB+7u]==1u){
  uint block=uint((p.y>>2)*(size.x>>2)+(p.x>>2))*2u,word=streamWord(base+block);
  uint index=(streamWord(base+block+1u)>>uint(((p.y&3)*4+(p.x&3))*2))&3u;
  vec3 a=streamColor(word&65535u),b=streamColor(word>>16u);
  return index==0u?a:index==1u?b:index==2u?(2.0*a+b)/3.0:(a+2.0*b)/3.0;
 }
 uint i=uint(p.y*size.x+p.x),word=streamWord(base+(i>>1u));
 return streamColor((word>>((i&1u)*16u))&65535u);
}
vec3 streamBlockColor(vec3 a,vec3 b,uint indices,ivec2 p){
 uint index=(indices>>uint(((p.y&3)*4+(p.x&3))*2))&3u;
 return index==0u?a:index==1u?b:index==2u?(2.0*a+b)/3.0:(a+2.0*b)/3.0;
}
vec3 streamBilinear(ivec2 q,ivec2 size,vec2 f){
 ivec2 lo=clamp(q,ivec2(0),size-1),hi=clamp(q+ivec2(1),ivec2(0),size-1);
 // Most neighboring samples share a BC1 block. Fetch/decode its endpoints once.
 if(c.atlas[SB+7u]==1u&&all(equal(lo>>2,hi>>2))){
  uint base=VIDEO0+(c.atlas[SB+3u]&1u)*VIDEO_STRIDE;
  uint block=uint((lo.y>>2)*(size.x>>2)+(lo.x>>2))*2u,word=streamWord(base+block),indices=streamWord(base+block+1u);
  vec3 a=streamColor(word&65535u),b=streamColor(word>>16u);
  return mix(mix(streamBlockColor(a,b,indices,lo),streamBlockColor(a,b,indices,ivec2(hi.x,lo.y)),f.x),mix(streamBlockColor(a,b,indices,ivec2(lo.x,hi.y)),streamBlockColor(a,b,indices,hi),f.x),f.y);
 }
 return mix(mix(streamPixel(q,size),streamPixel(q+ivec2(1,0),size),f.x),mix(streamPixel(q+ivec2(0,1),size),streamPixel(q+ivec2(1),size),f.x),f.y);
}
float streamMenuText(vec2 p,vec2 origin,float size,int label){
 const uint chars[64]=uint[64](82u,69u,84u,85u,82u,78u,32u,84u,79u,32u,76u,65u,85u,78u,67u,72u,69u,82u,65u,32u,67u,79u,78u,70u,73u,82u,77u,32u,32u,32u,66u,32u,82u,69u,83u,85u,77u,69u,76u,43u,82u,43u,88u,32u,77u,69u,78u,85u,65u,32u,83u,84u,65u,82u,84u,32u,32u,32u,66u,32u,66u,65u,67u,75u);
 const int starts[4]=int[4](0,18,38,48),counts[4]=int[4](18,20,10,16);vec2 q=p-origin;int i=int(floor(q.x/(5.0*size)));
 return i<0||i>=counts[label]?0.0:calcGlyph(q-vec2(float(i)*5.0*size,0),chars[starts[label]+i],size);
}
// Rendered on the console after video decoding, so compression never blurs it.
float streamHudGlyph(vec2 p,uint ch){
 ivec2 q=ivec2(floor(p/4.0));if(any(lessThan(q,ivec2(0)))||q.x>=5||q.y>=7)return 0.0;uint bits=0u;
 if(ch==70u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(554648095u>>at):(4u>>(at-32u)))&1u);}
 if(ch==80u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(554649150u>>at):(4u>>(at-32u)))&1u);}
 if(ch==83u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2182562319u>>at):(7u>>(at-32u)))&1u);}
 if(ch==82u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1699694142u>>at):(4u>>(at-32u)))&1u);}
 if(ch==69u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(3775873567u>>at):(7u>>(at-32u)))&1u);}
 if(ch==78u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1731913521u>>at):(4u>>(at-32u)))&1u);}
 if(ch==67u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(3775414799u>>at):(3u>>(at-32u)))&1u);}
 if(ch==84u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(138547359u>>at):(1u>>(at-32u)))&1u);}
 if(ch==76u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(3775414800u>>at):(7u>>(at-32u)))&1u);}
 if(ch==65u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1663026734u>>at):(4u>>(at-32u)))&1u);}
 if(ch==77u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1662703473u>>at):(4u>>(at-32u)))&1u);}
 if(ch==88u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1654794801u>>at):(4u>>(at-32u)))&1u);}
 if(ch==48u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2744831534u>>at):(3u>>(at-32u)))&1u);}
 if(ch==49u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2286031236u>>at):(3u>>(at-32u)))&1u);}
 if(ch==50u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(3493922350u>>at):(7u>>(at-32u)))&1u);}
 if(ch==51u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2182546494u>>at):(7u>>(at-32u)))&1u);}
 if(ch==52u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2247698626u>>at):(0u>>(at-32u)))&1u);}
 if(ch==53u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2183086623u>>at):(7u>>(at-32u)))&1u);}
 if(ch==54u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2736734734u>>at):(3u>>(at-32u)))&1u);}
 if(ch==55u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(276957247u>>at):(2u>>(at-32u)))&1u);}
 if(ch==56u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2736211502u>>at):(3u>>(at-32u)))&1u);}
 if(ch==57u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(2182596142u>>at):(3u>>(at-32u)))&1u);}
 if(ch==46u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(0u>>at):(1u>>(at-32u)))&1u);}
 if(ch==45u){uint at=uint(q.y*5+4-q.x);return float((at<32u?(1015808u>>at):(0u>>(at-32u)))&1u);}
 return 0.0;
}
float streamHudLabel(vec2 p,vec2 origin,uint word){
 vec2 q=p-origin;int i=int(floor(q.x/30.0));if(i<0||i>=3)return 0.0;
 return streamHudGlyph(q-vec2(float(i)*30.0,0),(word>>uint(i*8))&255u);
}
float streamHudValue(vec2 p,vec2 origin,uint raw,bool decimal){
 uint v=decimal?(min(raw,9999990u)+5u)/10u:raw,whole=decimal?v/10u:v;
 int n=whole>=10000u?5:whole>=1000u?4:whole>=100u?3:whole>=10u?2:1;
 vec2 q=p-origin;int i=int(floor(q.x/30.0));if(i<0||i>=n+(decimal?2:0))return 0.0;
 uint ch;if(i==n)ch=46u;else if(i==n+1)ch=48u+v%10u;else{uint divisor=1u;for(int k=0;k<4;k++)if(k<n-i-1)divisor*=10u;ch=48u+(whole/divisor)%10u;}
 return streamHudGlyph(q-vec2(float(i)*30.0,0),ch);
}
float streamHudValueWidth(uint raw,bool decimal){
 uint whole=decimal?((min(raw,9999990u)+5u)/10u)/10u:raw;
 int n=whole>=10000u?5:whole>=1000u?4:whole>=100u?3:whole>=10u?2:1;
 return float(n+(decimal?2:0))*30.0;
}
vec3 streamHud(vec3 col,vec2 p){
 if((c.atlas[SB+24u]&4u)==0u)return col;
 // Keep the text crisp at 80% scale; let the game show through the backdrop.
 p=vec2(24)+(p-vec2(16))/.8;
 float edge=box(p,vec2(248,142),vec2(224,118),10);
 if(edge>2.0)return col;
 col=mix(col,vec3(.015,.022,.018),fill(edge)*.58);col=mix(col,BLUE,line(edge,1.0)*.45);
 const uint labels[5]=uint[5](0x535046u,0x534552u,0x434e45u,0x54454eu,0x54414cu);
 int row=int(floor((p.y-36.0)/42.0));if(row<0||row>=5)return col;float y=42.0+float(row)*42.0;
 col=mix(col,row==0?BLUE:vec3(.88,.92,.88),streamHudLabel(p,vec2(42,y),labels[row]));
 if(c.atlas[SB+26u]!=0x495a4d35u){for(int i=0;i<2;i++)col=mix(col,vec3(.96),streamHudGlyph(p-vec2(158.0+30.0*float(i),y),45u));return col;}
 if(row==1){col=mix(col,vec3(.98),streamHudValue(p,vec2(158,y),c.atlas[SB+4u],false));col=mix(col,vec3(.88),streamHudGlyph(p-vec2(170.0+streamHudValueWidth(c.atlas[SB+4u],false),y),88u));col=mix(col,vec3(.98),streamHudValue(p,vec2(212.0+streamHudValueWidth(c.atlas[SB+4u],false),y),c.atlas[SB+5u],false));}
 else{uint value=c.atlas[SB+uint(row==0?27:row+27)];col=mix(col,row==0?BLUE:vec3(.98),streamHudValue(p,vec2(158,y),value,true));if(row>1)col=mix(col,vec3(.88),streamHudLabel(p,vec2(168.0+streamHudValueWidth(value,true),y),0x534du));}
 return col;
}
float streamStartText(vec2 p,vec2 origin,float size,int label){
 const uint chars[15]=uint[15](83u,84u,65u,82u,84u,87u,73u,78u,68u,79u,87u,68u,79u,79u,77u);
 int first=label==0?0:label==1?5:11,count=label==0?5:label==1?6:4;vec2 q=p-origin;int i=int(floor(q.x/(5.0*size)));
 return i<0||i>=count?0.0:calcGlyph(q-vec2(float(i)*5.0*size,0),chars[first+i],size);
}
// Runtime render settings use the same command channel as application launch.
// They remain reachable while video is gated during a network or size change.
float streamSettingLabel(vec2 p,vec2 origin,uint label,float scale){
 return blitLabel(p,origin,label,scale);
}
vec3 streamMenuDraw(vec2 p,vec3 color,uint menu,uint selected,uint title,bool start){
 if(menu==0u&&(c.atlas[SB+24u]&4u)!=0u)return color;
 if(start||menu==0u||c.atlas[SB+40u]==2u||c.atlas[SB+40u]==4u)return uiMenuDrawWithHint(p,color,menu,selected,title,start,true);
 vec3 lime=vec3(.835,1,0),ink=vec3(.949,.945,.91),muted=vec3(.62,.66,.62);bool sizeMenu=menu==2u||menu==4u||menu==5u,profiles=menu==3u;
 color*=.88;float panel=uiFill(uiBox(p,vec2(268,540),vec2(268,540),24.0));color=mix(color,vec3(.045,.058,.047),panel*.9);
 color=mix(color,lime,uiLabel(p,vec2(48,70),1u,.8));color=mix(color,ink,(sizeMenu||profiles)?streamSettingLabel(p,vec2(48,186),menu==4u?9u:menu==5u?10u:0u,.98):uiLabel(p,vec2(48,186),title,1.05));color=mix(color,muted,uiLabel(p,vec2(48,245),14u,.64));
 for(int j=0;j<2;j++){vec2 v=p-vec2(492,90);float l=abs(v.y-(j==0?v.x:-v.x))*.707;float mark=(1.0-smoothstep(1.0,2.6,l))*step(abs(v.x),12.0);color=mix(color,ink,mark);}
 uint count=sizeMenu?7u:3u;for(uint i=0u;i<count;i++){float y=354.0+float(i)*88.0;bool focus=selected==i;float d=uiBox(p,vec2(274,y),vec2(230,36),13.0);if(focus){color=mix(color,lime,uiFill(d)*.13);color=mix(color,lime,(1.0-smoothstep(.5,1.5,abs(d)))*.65);color=mix(color,lime,uiFill(uiBox(p,vec2(48,y),vec2(2,14),2.0)));}float text=sizeMenu?streamSettingLabel(p,vec2(70,y-9),i==0u&&menu!=2u?11u:i+1u,.84):profiles?streamSettingLabel(p,vec2(70,y-9),8u+i,.84):i==1u?streamSettingLabel(p,vec2(70,y-9),0u,.84):uiLabel(p,vec2(70,y-9),i==0u?2u:3u,.84);color=mix(color,focus?lime:ink,text);}
 color=mix(color,muted,uiLabel(p,vec2(48,920),4u,.68));color=mix(color,muted,uiLabel(p,vec2(48,960),18u,.68));return mix(color,muted,uiLabel(p,vec2(48,1000),19u,.68));
}
vec3 streamDraw(vec2 p){
 uint menu=c.atlas[SB+25u];bool routePending=c.atlas[SB+63u]!=0u,displayReady=!routePending&&c.atlas[SB+41u]==1u&&c.atlas[SB+69u]==1u;vec3 background=vec3(0);
 // The host opens this gate only after a fresh, verified frame is acknowledged.
 // Compute still latches/ACKs while the display waits; user-opened menus work.
 if(!displayReady&&menu==0u)return blitWaiting(p,c.atlas[SB+37u]);
 bool stretch=(c.atlas[SB+24u]&1u)!=0u;
 vec3 color=background;ivec2 size=ivec2(c.atlas[SB+4u],c.atlas[SB+5u]);
 if(displayReady&&c.atlas[SB+3u]!=0u&&all(greaterThan(size,ivec2(0)))){
  float scale=min(1.0,min(1920.0/float(size.x),1080.0/float(size.y)));
  vec2 extent=stretch?vec2(1920,1080):vec2(size)*scale,origin=(vec2(1920,1080)-extent)*.5;
  if(all(greaterThanEqual(p,origin))&&all(lessThan(p,origin+extent))){
   // At 1:1 pixel centers bilinear filtering is exactly one sample.
   // Avoid four BC1/palette decodes per display pixel in native mode.
   if(all(equal(extent,vec2(size))))color=streamPixel(ivec2(p-origin),size);
   else{
    vec2 uv=(p-origin)*vec2(size)/extent-.5;ivec2 q=ivec2(floor(uv));vec2 f=fract(uv);
    color=streamBilinear(q,size,f);
   }
  }
 }
 uint kind=c.atlas[SB+40u];
 if(displayReady&&menu==0u)color=streamHud(color,p);
 return streamMenuDraw(p,color,menu,c.atlas[SB+39u],kind==2u?6u:kind==3u?7u:kind==4u?8u:5u,c.atlas[SB+38u]!=0u);
}
