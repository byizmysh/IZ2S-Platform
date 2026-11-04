#include "load-progress-type.glsl"
float loadProgressGlyphPixel(ivec2 q,uint glyph,uint weight){
 if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,ivec2(32))))return 0.0;
 ivec2 at=q+ivec2(int(glyph%32u)*32,LOAD_GLYPH_Y+int(weight)*96+int(glyph/32u)*32);
 uint pixel=uint(at.y)*1024u+uint(at.x);return float((c.atlas[98304u+(pixel>>4u)]>>((pixel&15u)*2u))&3u)/3.0;
}
float loadProgressGlyph(vec2 q,uint glyph,uint weight){
 ivec2 at=ivec2(floor(q));vec2 f=fract(q);
 return mix(mix(loadProgressGlyphPixel(at,glyph,weight),loadProgressGlyphPixel(at+ivec2(1,0),glyph,weight),f.x),mix(loadProgressGlyphPixel(at+ivec2(0,1),glyph,weight),loadProgressGlyphPixel(at+ivec2(1),glyph,weight),f.x),f.y);
}
float loadProgressText(vec2 p,vec2 origin,uint row,float available){
 if(p.y<origin.y||p.y>=origin.y+32.0||p.x<origin.x||p.x>=origin.x+available)return 0.0;
 uint weight=row==0u?0u:1u,base=LOAD_PROGRESS+16u+row*64u;float width=0.0;
 for(uint i=0u;i<64u;i++){uint ch=c.atlas[base+i];if(ch==0u)break;uint glyph=clamp(ch,32u,127u)-32u;width+=LOAD_ADVANCE[weight*96u+glyph];}
 vec2 q=p-origin;float cursor=0.0,mask=0.0;
 uint dot=14u;float ellipsis=3.0*LOAD_ADVANCE[weight*96u+dot];bool overflow=width>available;
 float limit=overflow?available-ellipsis:available;
 for(uint i=0u;i<64u;i++){
  uint ch=c.atlas[base+i];if(ch==0u||cursor>q.x)break;
  uint glyph=clamp(ch,32u,127u)-32u;float advance=LOAD_ADVANCE[weight*96u+glyph];
  if(overflow&&cursor+advance>limit){
   for(uint n=0u;n<3u;n++)mask=max(mask,loadProgressGlyph(q-vec2(cursor+float(n)*LOAD_ADVANCE[weight*96u+dot],0),dot,weight));
   break;
  }
  if(q.x>=cursor-1.0&&q.x<=cursor+32.0)mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,weight));
  cursor+=advance;
 }
 return mask;
}
void loadProgressDraw(vec2 p,inout vec3 col,vec2 origin,vec2 size){
 if(!loadProgressVisible()||any(lessThan(p,origin-vec2(14)))||any(greaterThan(p,origin+size+vec2(14))))return;
 uint phase=c.atlas[LOAD_PROGRESS+2u],percent=c.atlas[LOAD_PROGRESS+3u];vec3 accent=phase==7u?vec3(1,.45,.42):vec3(.16,.95,1);
 vec2 center=origin+size*.5;float d=box(p,center,size*.5,16);
 col=mix(col,vec3(.005,.02,.08),fill(box(p,center+vec2(0,6),size*.5+vec2(2),18))*.38);
 col=mix(col,vec3(.035,.10,.25),fill(d));col=mix(col,vec3(.45,.68,.93),line(d,.7)*.5);
 col=mix(col,accent,fill(box(p,origin+vec2(2,size.y*.5),vec2(2,size.y*.5-18.0),2)));
 float width=size.x-48.0;
 col=mix(col,vec3(.96,.98,1),loadProgressText(p,origin+vec2(24,12),0u,width));
 col=mix(col,vec3(.65,.78,.96),loadProgressText(p,origin+vec2(24,47),1u,width));
 col=mix(col,vec3(.87,.93,1),loadProgressText(p,origin+vec2(24,78),2u,width));
 vec2 track=origin+vec2(24+width*.5,size.y-20.0);
 col=mix(col,vec3(.18,.28,.45),fill(box(p,track,vec2(width*.5,3),3)));
 if(percent<=100u&&percent>0u){float bar=width*float(percent)/100.0;col=mix(col,accent,fill(box(p,origin+vec2(24+bar*.5,size.y-20.0),vec2(bar*.5,3),3)));}
 else if(percent>100u){for(int i=0;i<3;i++)col=mix(col,accent,fill(length(p-(origin+vec2(30+float(i)*14.0,size.y-20.0)))-2.0));}
}
