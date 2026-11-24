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
#ifdef IZ_STREAM_FOCUS_TEXT
 if(ch==46u)p.y-=23.0;if(ch==45u)p.y-=12.0;
 uint i=clamp(ch,32u,127u)-32u;return focusText(p,vec2(0),ivec4(int(i%32u)*24,F_ASCII.y+int(i/32u)*32,24,32),1.5);
#else
 return calcGlyph(p,ch,4.0);
#endif
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
vec3 streamDraw(vec2 p){
 uint menu=c.atlas[SB+25u];bool displayReady=c.atlas[SB+41u]==1u&&c.atlas[SB+63u]==0u&&c.atlas[SB+69u]==1u;
 // The host opens this gate only after a fresh, verified frame is acknowledged.
 // Compute still latches/ACKs while the display waits; user-opened menus work.
 if(!displayReady&&menu==0u)return vec3(0);
 bool stretch=(c.atlas[SB+24u]&1u)!=0u;
 vec3 color=vec3(0);ivec2 size=ivec2(c.atlas[SB+4u],c.atlas[SB+5u]);
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
 if(menu==1u){
  color*=.22;float panel=box(p,vec2(960,570),vec2(480,220),8);
  color=mix(color,vec3(.075),fill(panel));color=mix(color,BLUE,line(panel,1.2));
  color=mix(color,c.atlas[SB+39u]==0u?BLUE:INK,streamMenuText(p,vec2(604,475),8.0,0));
  color=mix(color,BLUE,streamMenuText(p,vec2(675,600),6.0,1));
  color=mix(color,c.atlas[SB+39u]==1u?BLUE:MUTED,streamStartText(p,vec2(860,690),8.0,0));
 }
 if(menu==2u){
  color*=.2;color=mix(color,PANEL,fill(box(p,vec2(960,510),vec2(440,200),12)));
  color=mix(color,INK,streamStartText(p,vec2(860,365),8.0,0));
  for(int i=0;i<2;i++){vec2 center=vec2(i==0?760:1160,540);float d=box(p,center,vec2(170,72),8);bool selected=c.atlas[SB+39u]==uint(i);color=mix(color,selected?BLUE:BG,fill(d));color=mix(color,selected?BLUE:MUTED,line(d,1));color=mix(color,selected?BG:INK,streamStartText(p,center-vec2(i==0?116:76,24),8.0,i+1));}
  color=mix(color,MUTED,streamMenuText(p,vec2(735,650),5.0,3));
 }else if(menu==0u&&c.atlas[SB+38u]!=0u){
  float d=box(p,vec2(960,540),vec2(180,56),8);color=mix(color,BLUE,fill(d));color=mix(color,BG,streamStartText(p,vec2(860,512),8.0,0));
 }
 if(menu!=0u||c.atlas[SB+38u]!=0u)color=mix(color,MUTED,streamMenuText(p,vec2(825,818),5.0,2));
 return displayReady?streamHud(color,p):color;
}
