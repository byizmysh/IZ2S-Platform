// Run10 visual identity: analytic mouse, stationary crumbs and original controls.
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

// Shared DejaVu Sans typography from the existing progress atlas (21 px).
// Origins use measured ink bounds, rather than the padded 32 px glyph cells.
float chromeWord(vec2 p,vec2 origin,uint text[12],uint count,float scale){
 vec2 q=(p-origin)/scale;
 if(q.y<0.0||q.y>32.0||q.x<-.5||q.x>300.0)return 0.0;
 float ink=0.0,cursor=0.0;
 for(uint i=0u;i<count;i++){
  uint glyph=text[i]-32u;
  ink=max(ink,loadProgressGlyph(q-vec2(cursor,0),glyph,1u));
  cursor+=LOAD_ADVANCE[96u+glyph];
 }
 return ink;
}
float chromeKey(vec2 p,vec2 center,uint ch){
 float halfWidth=ch==65u?7.5:ch==66u?7.0:7.0;
 return loadProgressGlyph((p-center)/1.15+vec2(halfWidth,17.5),ch-32u,1u);
}
void menuHeader(vec2 p,inout vec3 col){
 if(p.x>=76.0&&p.x<149.0&&p.y>=66.0&&p.y<126.0)
  col=mix(col,vec3(.76,1.0,.34),mouse((p-vec2(76,68))/.40,c.motion.x*.10));
 if(p.x>=158.0&&p.x<370.0&&abs(p.y-95.0)<27.0){
  const uint letters[4]=uint[4](41u,58u,18u,51u); // IZ2S
  vec2 title=(p-vec2(158,70))/1.52;
  float ink=0.0,advance=0.0;
  for(uint i=0u;i<4u;i++){
   uint letter=letters[i];
   ink=max(ink,loadProgressGlyph(title-vec2(advance,0),letter,0u));
   advance+=LOAD_ADVANCE[letter]+1.1;
  }
  col=mix(col,vec3(.94,.97,1),ink);
  float badge=box(p,vec2(317,95),vec2(41,17),8);
  col=mix(col,vec3(.38,.52,.23),fill(badge)*.42);
  col=mix(col,vec3(.76,1.0,.34),line(badge,.7)*.45);
  const uint revision[12]=uint[12](48u,46u,51u,46u,53u,0u,0u,0u,0u,0u,0u,0u);
  float version=chromeWord(p,vec2(317,95)-vec2(26.5312,17.5)*.98,revision,5u,.98);
  col=mix(col,vec3(.82,1.0,.58),version);
 }
 float glass=box(p,vec2(1467.0-iz2HeaderShift(),95),vec2(382,37),37.0);
 col=mix(col,vec3(.15,.20,.39),fill(glass)*.36);
 col=mix(col,vec3(.75,.85,1),line(glass,.65)*.23);
}

void iz2ClockDraw(vec2 p,inout vec3 col){
 // One fixed status capsule: the header never shifts when telemetry arrives.
 if(p.x<1603.0||p.x>1851.0||abs(p.y-95.0)>39.0)return;
 float capsule=box(p,vec2(1727,95),vec2(122,37),37.0);
 col=mix(col,vec3(.15,.20,.39),fill(capsule)*.36);
 col=mix(col,vec3(.75,.85,1),line(capsule,.65)*.23);
 uint minuteOfDay=c.atlas[IZ2_CLOCK+1u];
 bool valid=c.atlas[IZ2_CLOCK]==IZ2_CLOCK_MAGIC&&minuteOfDay<1440u;
 uint hours=minuteOfDay/60u,minutes=minuteOfDay%60u;
 uint label[12]=uint[12](valid?48u+hours/10u:45u,valid?48u+hours%10u:45u,58u,
  valid?48u+minutes/10u:45u,valid?48u+minutes%10u:45u,0u,0u,0u,0u,0u,0u,0u);
 float ink=chromeWord(p,vec2(1658,95)-vec2(valid?30.0781:18.5469,17.5)*1.25,label,5u,1.25);
 col=mix(col,valid?INK:MUTED,ink);
 col=mix(col,MUTED,fill(box(p,vec2(1710,95),vec2(.5,12),.5))*.28);
}
void iz2BatteryDraw(vec2 p,inout vec3 col){
 if(p.x<1720.0||p.x>1841.0||abs(p.y-95.0)>20.0)return;
 bool valid=iz2BatteryAvailable();
 uint battery=c.atlas[IZ2_BATTERY+1u],level=valid?battery&255u:0u;
 bool charging=valid&&(battery&256u)!=0u;
 vec3 ink=!valid?MUTED:charging?vec3(.69,.94,.64):level<=15u?vec3(1,.73,.46):INK;
 const vec2 center=vec2(1741,95);
 float outline=line(box(p,center,vec2(16,8.5),3.0),1.1);
 float tip=fill(box(p,center+vec2(18.6,0),vec2(1.5,3.0),1.0));
 col=mix(col,ink,max(outline,tip)*.9);
 if(valid&&level>0u){
  float span=25.0*float(level)/100.0;
  col=mix(col,ink,fill(box(p,center+vec2(-12.5+span*.5,0),vec2(span*.5,5.0),min(1.4,span*.5)))*.8);
 }
 if(charging){
  vec2 q=p-center;
  float bolt=min(stroke(q,vec2(1.8,-5.8),vec2(-1.8,0)),min(stroke(q,vec2(-1.8,0),vec2(1.8,0)),stroke(q,vec2(1.8,0),vec2(-1.5,5.8))));
  col=mix(col,vec3(.067,.10,.15),fill(bolt-1.2));
 }
 uint label[12];for(uint i=0u;i<12u;i++)label[i]=0u;
 uint count=0u;
 if(valid){if(level==100u)label[count++]=49u;if(level>=10u)label[count++]=48u+(level/10u)%10u;label[count++]=48u+level%10u;label[count++]=37u;}
 else {label[count++]=45u;label[count++]=45u;}
 float width=valid?(float(count-1u)*13.3594+20.0):15.1563;
 col=mix(col,ink,chromeWord(p,vec2(1803.5-width*.55,95-17.5*1.1),label,count,1.1));
}
void menuFooter(vec2 p,inout vec3 col){
 if(p.y<970.0||p.y>1040.0)return;
 if(c.rows[1]!=0u||c.rows[14]!=0u||c.bagMeta.y!=0u)return;
 const uint openLabel[12]=uint[12](79u,112u,101u,110u,0u,0u,0u,0u,0u,0u,0u,0u);
 bool picker=c.piece.x!=0;uint count=focusCount(picker);
#ifdef IZ_CORE_LOCAL_ADD
 const uint addGrid=(0x704000u+98308u-1024u)/4u;
 if(picker&&c.atlas[addGrid]==0x495a4752u)count=c.atlas[addGrid+1u];
#endif
 bool appSelected=!picker&&c.state.x<min(c.library.x,15u)&&focusOrdinal(c.state.x,false)<count;
 bool showA=count>0u&&(picker||appSelected),showB=picker,showX=!picker;
#ifdef IZ_CORE_LOCAL_ADD
 const uint storeMeta=(0x704000u-1024u)/4u;
 if(picker&&c.atlas[addGrid]==0x495a4752u){
  uint selected=c.atlas[addGrid+2u]%15u;
  if((c.atlas[storeMeta+37350u]&(1u<<selected))!=0u)showA=false;
 }
#endif
 float cursor=95.0;vec2 a=vec2(cursor,1008),b,x;
 if(showA){ivec4 caption=picker?F_INSTALL:appSelected?F_OPEN:F_ADDSMALL;col=mix(col,INK,line(length(p-a)-19.0,1.4));col=mix(col,INK,chromeKey(p,a,65u));col=mix(col,INK,(!picker&&appSelected?chromeWord(p,a+vec2(35,-19.5*1.15),openLabel,4u,1.15):focusText(p,a+vec2(35,-float(caption.w)*.5),caption,1)));cursor+=35.0+float(caption.z)+48.0;}
 b=vec2(cursor,1008);
 if(showB){col=mix(col,INK,line(length(p-b)-19.0,1.4));col=mix(col,INK,chromeKey(p,b,66u));col=mix(col,INK,focusText(p,b+vec2(35,-float(F_BACK.w)*.5),F_BACK,1));cursor+=35.0+float(F_BACK.z)+48.0;}
 x=vec2(cursor,1008);
 if(showX){col=mix(col,INK,line(length(p-x)-19.0,1.4));col=mix(col,INK,chromeKey(p,x,88u));col=mix(col,INK,focusText(p,x+vec2(35,-float(F_ADDSMALL.w)*.5),F_ADDSMALL,1));cursor+=35.0+float(F_ADDSMALL.z)+48.0;}
 if(appSelected&&c.transition.y==0.0&&!nroPreviewTile(nroPreviewBase(c.state.x))){vec2 key=F_REMOVE_KEY;col=mix(col,INK,line(length(p-key)-19.0,1.4));col=mix(col,INK,fill(box(p,key,vec2(7,1),1)));col=mix(col,INK,focusText(p,key+vec2(35,-float(F_REMOVE.w)*.5),F_REMOVE,1));}
}

void iz2AuthorCredit(vec2 p,inout vec3 col){
 if(p.x<1250.0||p.x>1803.0||p.y<981.0||p.y>1031.0)return;
 // Created by IZMYSH ( · @byizmysh )
 const uint before[18]=uint[18](67u,114u,101u,97u,116u,101u,100u,32u,98u,121u,32u,73u,90u,77u,89u,83u,72u,32u);
 const uint profile[10]=uint[10](64u,98u,121u,105u,122u,109u,121u,115u,104u,41u);
 const float scale=1.00,markSize=23.0,gap=7.0;
 float width=LOAD_ADVANCE[8]+(markSize+gap*2.0)/scale;
 for(uint i=0u;i<18u;i++)width+=LOAD_ADVANCE[before[i]-32u];
 for(uint i=0u;i<10u;i++)width+=LOAD_ADVANCE[profile[i]-32u];
 vec2 origin=vec2(1800.0-width*scale,993);
 vec2 glyphPoint=(p-origin)/scale;
 float cursor=0.0,ink=0.0;
 for(uint i=0u;i<18u;i++){
  uint glyph=before[i]-32u;
  ink=max(ink,loadProgressGlyph(glyphPoint-vec2(cursor,0),glyph,0u));
  cursor+=LOAD_ADVANCE[glyph];
 }
 ink=max(ink,loadProgressGlyph(glyphPoint-vec2(cursor,0),8u,0u));
 cursor+=LOAD_ADVANCE[8];
 vec2 markOrigin=origin+vec2(cursor*scale+gap,4.0);
 cursor+=(markSize+gap*2.0)/scale;
 for(uint i=0u;i<10u;i++){
  uint glyph=profile[i]-32u;
  ink=max(ink,loadProgressGlyph(glyphPoint-vec2(cursor,0),glyph,0u));
  cursor+=LOAD_ADVANCE[glyph];
 }
 // The GitHub mark uses the same Primer Octicons artwork as IZ2S Cheese.
 const uint mark[24]=uint[24](
  0x000000u,0x00ff00u,0x03ffc0u,0x07ffe0u,0x0ffff0u,0x1ffff8u,
  0x3cff3cu,0x3c003cu,0x7c003eu,0x7c003eu,0x7c003eu,0xf8001fu,
  0xf8001fu,0xfc003fu,0x7c003eu,0x7e007eu,0x7f00feu,0x3fc3ccu,
  0x3f819cu,0x1f8038u,0x0f8070u,0x0781e0u,0x0381c0u,0x000000u);
 ivec2 pixel=ivec2(floor((p-markOrigin)*(24.0/markSize)));
 if(all(greaterThanEqual(pixel,ivec2(0)))&&all(lessThan(pixel,ivec2(24))))
  ink=max(ink,float((mark[pixel.y]>>uint(pixel.x))&1u));
 col=mix(col,mix(MUTED,INK,.63),ink);
}
