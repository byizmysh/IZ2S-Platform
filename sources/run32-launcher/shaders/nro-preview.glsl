// The catalog marks preview tiles at byte 102520. Menu scratch row 14
// stores the open preview's one-based slot.
bool nroPreviewTile(uint base) {
 return c.atlas[base+25630u]==0x314f524eu;
}

uint nroPreviewBase(uint index) {
 return 524288u+index*32768u;
}

const vec2 NRO_BACK_CENTER=vec2(960,686);
const vec2 NRO_BACK_HALF=vec2(104,28);

#ifndef HB_READONLY
bool nroPreviewInput(uint pressed) {
 if(c.rows[14]==0u)return false;
 uint slot=c.rows[14]-1u;
 if(slot>=min(c.library.x,15u)||!nroPreviewTile(nroPreviewBase(slot))){
  c.rows[14]=0u;
  return false;
 }
 bool touching=hbTouchCount()>0u&&(c.touch[6]&2u)==0u;
 bool back=touching&&c.library.w==0u&&all(lessThanEqual(abs(hbTouch(0u)*1.5-NRO_BACK_CENTER),NRO_BACK_HALF));
 if(back||(pressed&(HB_A|HB_B))!=0u)c.rows[14]=0u;
 c.library.w=touching?1u:0u;
 c.bag[7]=1u;
 hbEndFrame();
 return true;
}
#else
const uint NRO_GLYPHS[3]=uint[3](78u-32u,82u-32u,79u-32u);

float nroTypeWidth(float scale) {
 return (LOAD_ADVANCE[NRO_GLYPHS[0]]+LOAD_ADVANCE[NRO_GLYPHS[1]]+LOAD_ADVANCE[NRO_GLYPHS[2]])*scale;
}

float nroTypeText(vec2 p,vec2 origin,float scale) {
 vec2 local=p-origin;
 if(any(lessThan(local,vec2(0)))||any(greaterThanEqual(local,vec2(nroTypeWidth(scale),32.0*scale))))return 0.0;
 vec2 q=local/scale;
 float ink=0.0;
 for(uint i=0u;i<3u;i++){
  uint glyph=NRO_GLYPHS[i];
  ink=max(ink,loadProgressGlyph(q,glyph,0u));
  q.x-=LOAD_ADVANCE[glyph];
 }
 return ink;
}

float nroDescriptionPixel(ivec2 q,uint base) {
 ivec2 size=ivec2(min(c.atlas[base+25602u],512u),min(c.atlas[base+25603u],64u));
 if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,size)))return 0.0;
 uint pixel=uint(q.y+64)*512u+uint(q.x);
 return float((c.atlas[base+(pixel>>2u)]>>((pixel&3u)*8u))&255u)/255.0;
}

vec3 nroPreviewDraw(vec2 p,vec3 col) {
 if(c.rows[14]==0u||c.rows[14]>min(c.library.x,15u))return col;
 uint base=nroPreviewBase(c.rows[14]-1u);
 if(!nroPreviewTile(base))return col;
 col*=.24;
 float panel=box(p,vec2(960,540),vec2(390,190),18);
 col=mix(col,PANEL,fill(panel));
 col=mix(col,MUTED,line(panel,.7)*.5);
 col=mix(col,BLUE,nroTypeText(p,vec2(622,396),1.0));
 vec4 icon=focusIcon(p,vec2(685,507),base,112);
 col=mix(col,icon.rgb,icon.a);
 float width=float(c.atlas[base+25600u]);
 col=mix(col,INK,focusLabel(p,vec2(770,466),base,min(1.0,520.0/max(1.0,width))));
 col=mix(col,MUTED,nroDescriptionPixel(ivec2(floor(p-vec2(770,530))),base));
 col=mix(col,BLUE,fill(box(p,NRO_BACK_CENTER,NRO_BACK_HALF,10)));
 return mix(col,BG,focusText(p,NRO_BACK_CENTER-vec2(F_BBACK.zw)*.5,F_BBACK,1));
}
#endif
