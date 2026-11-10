// Visual-only library scene. All catalog records, launch requests and overlays
// continue to use the existing ABI-1 buffers and compute state machine.
vec3 iz2Accent(uint i){
 const vec3 palette[8]=vec3[8](vec3(.33,.67,.88),vec3(.76,.34,.57),vec3(.52,.36,.81),vec3(.91,.48,.23),vec3(.15,.65,.65),vec3(.41,.47,.85),vec3(.70,.37,.79),vec3(.26,.61,.80));
 return palette[i%8u];
}
vec3 iz2Glass(vec2 p,vec3 col,vec2 center,vec2 halfSize,float radius,float opacity){
 float d=box(p,center,halfSize,radius);
 col=mix(col,vec3(.06,.09,.22),fill(d)*opacity);
 return mix(col,vec3(.72,.83,1),line(d,.65)*.22);
}
// Destination is always visible, including ordinary RAM sessions.
vec3 iz2Storage(vec2 p,vec3 col,vec2 origin,uint base){
 bool sd=c.atlas[base+25628u]==0x44534453u;uvec3 chars=sd?uvec3(83u,68u,0u):uvec3(82u,65u,77u);uint count=sd?2u:3u;
 float width=0.0;for(uint i=0u;i<count;i++)width+=LOAD_ADVANCE[chars[i]-32u]*.70;
 float d=box(p,origin+vec2((width+20.0)*.5,14),vec2((width+20.0)*.5,14),7);if(d>1.0)return col;
 vec3 ink=sd?vec3(1.0,.85,.48):BLUE;col=mix(col,vec3(.025,.04,.09),fill(d)*.96);col=mix(col,ink,line(d,.7)*.75);
 vec2 q=(p-origin-vec2(10,3))/.70;float cursor=0.0,mask=0.0;
 for(uint i=0u;i<count;i++){uint glyph=chars[i]-32u;mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,0u));cursor+=LOAD_ADVANCE[glyph];}
 return mix(col,ink,mask);
}
vec3 iz2Card(vec2 p,vec3 col,uint b,uint identity,vec2 center,bool selected,bool marked){
 float grow=iz2Grow((b-524288u)/32768u),t=c.motion.x;
 float scaleCard=1.0+.075*grow;center.y-=(7.0+1.5*sin(t*1.5))*grow;vec2 q=(p-center)/scaleCard;
 if(any(greaterThan(abs(q),vec2(116,194))))return col;
 float d=box(q,vec2(0),vec2(98,174),18.0);vec3 accent=iz2Accent(identity);
 if(grow>.001){float halo=1.0-smoothstep(0.0,16.0,max(d,0.0));col+=mix(accent,BLUE,.6)*halo*.20*grow*step(0.0,d);}
 if(d<1.0){
  float light=1.0-smoothstep(0.0,290.0,length(q-vec2(-54,-127)));
  vec3 card=mix(vec3(.025,.035,.09),accent,.24+.52*light);
  // A softly lit inset gives square artwork its own quiet display surface.
  float plate=box(q,vec2(0,-27),vec2(87,92),23.0);
  card=mix(card,vec3(.025,.035,.075),fill(plate)*.50);
  card=mix(card,vec3(.85,.92,1),line(plate,.65)*.12);
  vec2 artwork=center+vec2(0,-29-2.0*grow*sin(t*1.5));
  vec4 pic=appIcon(center+q,artwork,b,164.0,INK);card=mix(card,pic.rgb,pic.a);
  // The slow glass reflection is analytic, clipped to the card, and needs no texture.
  float sweep=mod(t*24.0+float(identity)*57.0,650.0)-325.0;
  float sheen=1.0-smoothstep(0.0,34.0,abs(q.x+q.y*.38-sweep));
  card+=vec3(.16,.21,.26)*sheen*(.10+.26*grow);
  card=mix(card,vec3(.025,.035,.085),smoothstep(68.0,111.0,q.y)*.96);
  card=mix(card,accent,fill(box(q,vec2(0,96),vec2(24,.8),1))*.65);
  col=mix(col,card,fill(d));
  float width=float(c.atlas[b+25600u]),size=min(.79,170.0/max(width,1.0));
  col=mix(col,INK,focusLabel(center+q,center+vec2(-width*size*.5,121),b,size));
  if(c.piece.x==0)col=iz2Storage(center+q,col,center+vec2(-80,-154),b);
  if(marked){vec2 tick=q-vec2(72,-146);float mark=min(stroke(tick,vec2(-6,0),vec2(-1,5)),stroke(tick,vec2(-1,5),vec2(10,-7)));col=mix(col,BLUE,fill(mark-1.5));}
 }
 col=mix(col,mix(vec3(.70,.82,1),vec3(.93,.99,1),grow),line(d,.7+grow)*(0.36+.64*grow));
 return col;
}
float iz2ActivityPixel(ivec2 q){
 if(any(lessThan(q,ivec2(0)))||q.x>=1024||q.y>=512)return 0.0;uint n=uint(q.y*1024+q.x);return float((c.atlas[IZ2_ACTIVITY_TEXT+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;
}
float iz2ActivityText(vec2 p,vec2 origin,uint row,bool date){
 vec2 q=p-origin;if(q.y<0.0||q.y>=58.0||q.x<(date?500.0:0.0)||q.x>=(date?788.0:500.0))return 0.0;
 q.y+=float(row)*64.0;ivec2 xy=ivec2(floor(q));vec2 f=fract(q);return mix(mix(iz2ActivityPixel(xy),iz2ActivityPixel(xy+ivec2(1,0)),f.x),mix(iz2ActivityPixel(xy+ivec2(0,1)),iz2ActivityPixel(xy+ivec2(1)),f.x),f.y);
}
void iz2ActivityDraw(vec2 p,inout vec3 col){
 col=mix(col,INK,focusText(p,vec2(80,662),F_RECENT,1.0));uint count=iz2ActivityCount();
 if(count==0u){col=iz2Glass(p,col,vec2(960,IZ2_ACTIVITY_TOP+44.0),vec2(880,42),13,.30);vec2 clock=vec2(132,IZ2_ACTIVITY_TOP+44);col=mix(col,MUTED,line(length(p-clock)-13,1));col=mix(col,MUTED,fill(min(stroke(p,clock,clock+vec2(0,-7)),stroke(p,clock,clock+vec2(6,3)))-.8));col=mix(col,MUTED,focusText(p,vec2(174,IZ2_ACTIVITY_TOP+34),F_NOACTIVITY,1));return;}
 for(uint i=0u;i<count;i++){
  float y=IZ2_ACTIVITY_TOP+float(i)*92.0;if(p.y<y||p.y>y+88.0)continue;
  col=iz2Glass(p,col,vec2(960,y+44),vec2(880,42),13,.30);uint index=c.atlas[IZ2_ACTIVITY+4u+i*4u];
  if(index<min(c.library.x,15u)){vec4 pic=focusIcon(p,vec2(132,y+44),slotBase(index),48);col=mix(col,pic.rgb,pic.a);}
  else{vec2 mark=p-vec2(132,y+44);col=mix(col,BLUE,line(length(mark)-15,1));float tick=min(stroke(mark,vec2(-7,0),vec2(-2,5)),stroke(mark,vec2(-2,5),vec2(8,-6)));col=mix(col,BLUE,fill(tick-1.1));}
  col=mix(col,INK,iz2ActivityText(p,vec2(180,y+18),i,false));col=mix(col,MUTED,iz2ActivityText(p,vec2(1020,y+18),i,true));
 }
}
vec3 iz2Content(vec2 p,vec3 col){
 bool picker=c.piece.x!=0;
 uint installed=min(c.library.x,15u),realCount=focusCount(picker);
 uint chosen=picker?uint(max(0,c.piece.y)):c.state.x;
 uint ordinal=c.rows[19]==0x464f4355u?min(c.rows[11],realCount):chosen;
 bool add=!picker&&(chosen>=installed||ordinal>=realCount),empty=realCount==0u,nomatch=empty&&focusRawCount(picker)>0u;
 uint base=slotBase(picker?16u+min(chosen,15u):min(chosen,14u));
 col=mix(col,INK,focusText(p,vec2(80,197),picker?F_PACKAGES:F_LIBRARY,1.30));
 col=mix(col,MUTED,focusCounter(p,vec2(1740,198),add?0u:ordinal+1u,realCount));
 // A fixed Add app station is part of the existing horizontal navigation.
 float addGrow=iz2Grow(32u);vec2 addPoint=ADD_CENTER+(p-ADD_CENTER)/(1.0+.06*addGrow);float ad=box(addPoint,ADD_CENTER,vec2(84,174),15);bool addSelected=add&&!picker;
 col=mix(col,vec3(.16,.21,.42),fill(ad)*.62);
 col=mix(col,addSelected?INK:vec3(.72,.83,1),line(ad,addSelected?1.7:.7)*(addSelected?1.0:.36));
 col=mix(col,addSelected?INK:BLUE,focusPlus(p,ADD_CENTER-vec2(0,22),25));
 col=mix(col,INK,focusText(p,ADD_CENTER+vec2(-float(F_ADDSMALL.z)*.5,120),F_ADDSMALL,1));
 int first=int(floor(c.flow.x));
 for(int i=-1;i<7;i++){
  int index=first+i;if(index<0||index>=int(realCount))continue;
  vec2 center=focusCardCenter(float(index));if(center.x<280.0||center.x>1870.0)continue;
  uint actual=focusIndex(uint(index)),b=slotBase(picker?16u+actual:actual);
  bool selected=!add&&actual==chosen,marked=picker&&(c.bagMeta.z&(1u<<actual))!=0u;
  col=iz2Card(p,col,b,actual,center,selected,marked);
 }
 if(empty){
  col=iz2Glass(p,col,vec2(1055,430),vec2(740,174),16,.32);
  col=mix(col,INK,focusText(p,vec2(408,368),nomatch?F_NORESULTS:F_ADD,.82));
  col=mix(col,MUTED,focusText(p,vec2(410,452),nomatch?F_SEARCHHELP:F_BROWSE,1.1));
 }
 // Navigation controls use the same shared centres as touch hit testing.
 if(realCount>0u){
  float prev=min(stroke(p,FOCUS_PREV+vec2(7,-13),FOCUS_PREV+vec2(-5,0)),stroke(p,FOCUS_PREV+vec2(-5,0),FOCUS_PREV+vec2(7,13)));
  float next=min(stroke(p,FOCUS_NEXT+vec2(-7,-13),FOCUS_NEXT+vec2(5,0)),stroke(p,FOCUS_NEXT+vec2(5,0),FOCUS_NEXT+vec2(-7,13)));col=mix(col,INK,fill(min(prev,next)-1.1));
 }
 iz2ActivityDraw(p,col);
 return col;
}
vec3 iz2MenuDraw(vec2 p){
 vec3 col=focusBackground(p);float scroll=clamp(uintBitsToFloat(c.rows[13]),0.0,iz2ScrollMax());
 if(iz2ContentHit(p)){vec3 content=iz2Content(p+vec2(0,scroll),col);col=mix(col,content,smoothstep(IZ2_CONTENT_TOP,IZ2_CONTENT_TOP+18.0,p.y)*(1.0-smoothstep(IZ2_CONTENT_BOTTOM-20.0,IZ2_CONTENT_BOTTOM,p.y)));}
 menuHeader(p,col);focusHeader(p,col);
 col=mix(col,vec3(.75,.84,1),fill(box(p,vec2(960,969),vec2(880,.45),0))*.18);

 return col;
}
