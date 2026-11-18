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
 bool sd=c.atlas[base+25628u]==0x44534453u||c.atlas[base+25631u]==1u;
 bool needsPc=sd&&c.atlas[base+25629u]==2u;
 const uint card[5]=uint[5](83u,68u,32u,80u,67u);
 const uint ram[5]=uint[5](82u,65u,77u,32u,32u);
 uint count=needsPc?5u:sd?2u:3u;
 float width=0.0;for(uint i=0u;i<count;i++)width+=LOAD_ADVANCE[(sd?card[i]:ram[i])-32u]*.70;
 float d=box(p,origin+vec2((width+20.0)*.5,14),vec2((width+20.0)*.5,14),7);if(d>1.0)return col;
 vec3 ink=sd?vec3(1.0,.85,.48):BLUE;col=mix(col,vec3(.025,.04,.09),fill(d)*.96);col=mix(col,ink,line(d,.7)*.75);
 vec2 q=(p-origin-vec2(10,3))/.70;float cursor=0.0,mask=0.0;
 for(uint i=0u;i<count;i++){uint glyph=(sd?card[i]:ram[i])-32u;mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,0u));cursor+=LOAD_ADVANCE[glyph];}
 return mix(col,ink,mask);
}
// Executable extension, aligned to the upper-right inset of every library card.
vec3 iz2ExecutableType(vec2 p,vec3 col,vec2 right,uint base){
 bool nro=nroPreviewTile(base)||c.atlas[base+25631u]==1u;
 const uint nativeType[4]=uint[4](46u,110u,114u,111u);
 const uint appType[4]=uint[4](46u,105u,122u,97u);
 float scale=.70,width=0.0;
 for(uint i=0u;i<4u;i++)width+=LOAD_ADVANCE[(nro?nativeType[i]:appType[i])-32u]*scale;
 vec2 origin=right-vec2(width+16.0,0);
 float d=box(p,origin+vec2((width+16.0)*.5,14),vec2((width+16.0)*.5,14),7);
 if(d>1.0)return col;
 col=mix(col,vec3(.025,.04,.09),fill(d)*.96);
 col=mix(col,BLUE,line(d,.7)*.75);
 vec2 q=(p-origin-vec2(8,3))/scale;float ink=0.0;
 for(uint i=0u;i<4u;i++){uint glyph=(nro?nativeType[i]:appType[i])-32u;ink=max(ink,loadProgressGlyph(q,glyph,0u));q.x-=LOAD_ADVANCE[glyph];}
 return mix(col,BLUE,ink);
}
#ifdef IZ_CORE_LOCAL_ADD
vec3 iz2StoreVersion(vec2 cardPoint,vec3 col,uint card);
#endif
vec3 iz2Card(vec2 p,vec3 col,uint b,uint identity,vec2 center,bool selected,bool marked,bool showVersion){
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
  float titleY=c.atlas[b+25632u]==0x32465454u?136.0-float(c.atlas[b+25601u])*size*.5:121.0;
  col=mix(col,INK,focusLabel(center+q,center+vec2(-width*size*.5,titleY),b,size));
  {col=iz2Storage(center+q,col,center+vec2(-84,-154),b);col=iz2ExecutableType(center+q,col,center+vec2(84,-154),b);}
#ifdef IZ_CORE_LOCAL_ADD
  if(showVersion)col=iz2StoreVersion(q,col,identity);
#endif
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
  uint progress=c.atlas[IZ2_ACTIVITY+6u+i*4u];
  if(progress!=0u){
   const float left=1060.0, width=690.0;
   float done=width*clamp(float(progress-1u)/100.0,0.0,1.0);
   float track=box(p,vec2(left+width*.5,y+55),vec2(width*.5,4),4);
   col=mix(col,vec3(.15,.2,.34),fill(track));
   if(done>0.0){
    float bar=box(p,vec2(left+done*.5,y+55),vec2(done*.5,4),min(4.0,done*.5));
    float shimmer=.17+.16*sin(c.motion.x*6.0-(p.x-left)*.045);
    col=mix(col,mix(BLUE,INK,shimmer),fill(bar));
   }else{
    float pulse=width*fract(c.motion.x*.62);
    float glow=box(p,vec2(left+pulse,y+55),vec2(14,3.5),3);
    col=mix(col,BLUE,fill(glow)*fill(track));
   }
  }
 }
}
#ifdef IZ_CORE_LOCAL_ADD
// The standalone launcher publishes the SD import receipt in this shared slot.
const uint CORE_LOCAL_ADD=(0x704000u-1024u)/4u;
const uint CORE_GRID=CORE_LOCAL_ADD+24577u;
const uint CORE_FTP=CORE_GRID+4u;
// The native text lives below the legacy bitmap glyphs.
const uint CORE_FTP_TEXT=CORE_FTP+(1024u*48u-32u)/4u;
const float CORE_FTP_WIDTH=458.0,CORE_SHORTCUT_GAP=24.0,CORE_STATUS_WIDTH=148.0;
// Center both shortcuts between Remove and the first pixel of the author credit.
const float CORE_SHORTCUT_LEFT=(533.0+1389.0-CORE_FTP_WIDTH-CORE_SHORTCUT_GAP-CORE_STATUS_WIDTH)*.5;
const float CORE_FTP_LEFT=CORE_SHORTCUT_LEFT+CORE_STATUS_WIDTH+CORE_SHORTCUT_GAP;
bool iz2HasFtpFooter(){
 return c.atlas[CORE_GRID]==0x495a4752u&&c.atlas[CORE_LOCAL_ADD+37289u]==1u;
}
float iz2AddMask(vec2 p,vec2 origin,uint base,uvec2 size){
 vec2 q=p-origin;
 if(any(lessThan(q,vec2(0)))||any(greaterThanEqual(q,vec2(size))))return 0.0;
 uint index=uint(q.y)*size.x+uint(q.x);
 uint pixel=(c.atlas[base+index/4u]>>(8u*(index%4u)))&255u;
 return float(pixel)/255.0;
}
float iz2LocalAddText(vec2 p,vec2 origin,uint line){
 return iz2AddMask(p,origin,CORE_LOCAL_ADD+1u+line*12288u,uvec2(1024,48));
}
bool iz2LocalGrid(){return c.piece.x!=0&&c.atlas[CORE_GRID]==0x495a4752u;}
vec3 iz2StoreVersion(vec2 cardPoint,vec3 col,uint card){
 const uint versions=CORE_LOCAL_ADD+37290u;
 bool installed=(c.atlas[versions+60u]&(1u<<card))!=0u;
 if((c.atlas[versions+card*4u]&255u)==0u)return col;
 vec2 local=(cardPoint-vec2(-82,73))/.50;
 float ink=0.0;
 if(local.x>=0.0&&local.x<285.0&&local.y>=0.0&&local.y<34.0){
  uint index=uint(local.x/19.0);
  if(index==0u)ink=focusAscii(local,vec2(0),118u);
  else{
   uint at=index-1u;
   uint ch=(c.atlas[versions+card*4u+at/4u]>>(8u*(at%4u)))&255u;
   if(ch>=32u&&ch<=126u)ink=focusAscii(local,vec2(float(index)*19.0,0),ch);
  }
 }
 vec3 green=vec3(.62,.9,.67);
 col=mix(col,installed?green:INK,ink);
 if(installed){
  vec2 mark=cardPoint-vec2(71,81);
  float tick=min(stroke(mark,vec2(-5,0),vec2(-1,4)),stroke(mark,vec2(-1,4),vec2(6,-5)));
  col=mix(col,green,fill(tick-.8));
 }
 return col;
}

void iz2FtpFooter(vec2 p,inout vec3 col){
 p.x-=CORE_FTP_LEFT;
 if(p.x< -1.0||p.x>=CORE_FTP_WIDTH+2.0||p.y<987.0||p.y>=1029.0||
    !rsInlineAllowed()||loadProgressVisible())return;
 uint ftp=c.atlas[CORE_GRID+3u];
 vec2 halfSize=vec2(CORE_FTP_WIDTH*.5,20);
 float frame=box(p,vec2(halfSize.x,1008),halfSize,13);
 vec3 border=vec3(.52,.77,.65);
 vec3 tint=mix(vec3(.16,.32,.30),vec3(.20,.40,.34),clamp(p.x/CORE_FTP_WIDTH,0.0,1.0));
 if(ftp==1u)tint+=vec3(.025,.045,.020);
 col=mix(col,tint,fill(frame)*.90);
 col=mix(col,border,line(frame,.65)*.70);

 vec2 left=vec2(36,1008),right=vec2(84,1008),plus=(left+right)*.5;
 float keys=max(fill(box(p,left,vec2(18,13),7)),fill(box(p,right,vec2(18,13),7)));
 col=mix(col,vec3(.09,.21,.19),keys*.68);
 float chord=max(rsStickKeycap(p,left,.65,76u),rsStickKeycap(p,right,.65,82u));
 chord=max(chord,fill(min(stroke(p,plus-vec2(3,0),plus+vec2(3,0)),stroke(p,plus-vec2(0,3),plus+vec2(0,3)))-.65));
 col=mix(col,vec3(.77,.92,.83),chord);
 col=mix(col,border,fill(box(p,vec2(114,1008),vec2(.55,9),0))*.45);

 if(p.x<128.0)return;
 const float scale=.65;
 vec2 q=(p-vec2(128,1008-9*scale))/scale;
 const uint enable[12]=uint[12](69u,78u,65u,66u,76u,69u,32u,70u,84u,80u,0u,0u);
 // Older runtimes still report the listening flag. Their FTP server binds
 // this fixed endpoint, even when they do not publish the text payload.
 const uint endpoint[19]=uint[19](70u,84u,80u,32u,49u,48u,46u,52u,50u,46u,48u,46u,50u,48u,58u,49u,51u,51u,55u);
 const uint error[12]=uint[12](70u,84u,80u,32u,69u,82u,82u,79u,82u,0u,0u,0u);
 float ink=0.0;
 bool hasText=c.atlas[CORE_FTP_TEXT]==0x31505446u;
 if(ftp==1u||(ftp==2u&&hasText)){
  // 25 glyphs at this advance fit with at least 16px to the right edge.
  for(uint i=0u;i<(hasText?25u:19u);i++){
   uint ch=hasText?(c.atlas[CORE_FTP_TEXT+1u+i/4u]>>(8u*(i%4u)))&255u:endpoint[i];
   if(ch==0u)break;
   // ASCII tiles are top-aligned. Restore the punctuation baseline.
   float baseline=ch==46u?15.0:ch==58u?3.0:0.0;
   ink=max(ink,focusAscii(q,vec2(float(i)*19.0,baseline),ch));
  }
 }else{
  ink=rsText(q,vec2(0),ftp==2u?error:enable);
 }
 col=mix(col,ftp==2u?vec3(1,.82,.65):vec3(.88,1,.91),ink);
}
#endif
vec3 iz2Content(vec2 p,vec3 col){
 bool picker=c.piece.x!=0;
 uint installed=min(c.library.x,15u),realCount=focusCount(picker);
 uint chosen=picker?uint(max(0,c.piece.y)):c.state.x;
 bool localPicker=false;
 uint total=realCount,globalSelected=chosen;
#ifdef IZ_CORE_LOCAL_ADD
 localPicker=iz2LocalGrid();
 if(localPicker){
  total=min(c.atlas[CORE_GRID+1u],64u);
  globalSelected=min(c.atlas[CORE_GRID+2u],max(total,1u)-1u);
  realCount=min(total-globalSelected/15u*15u,15u);
  chosen=globalSelected%15u;
 }
#endif
 uint ordinal=c.rows[19]==0x464f4355u?min(c.rows[11],realCount):chosen;
 bool add=!picker&&(chosen>=installed||ordinal>=realCount),empty=realCount==0u,nomatch=empty&&focusRawCount(picker)>0u;
 uint base=slotBase(picker?16u+min(chosen,15u):min(chosen,14u));
 col=mix(col,INK,focusText(p,vec2(80,197),picker?F_PACKAGES:F_LIBRARY,1.30));
 if(realCount>0u)col=mix(col,MUTED,focusCounter(p,vec2(1740,198),localPicker?globalSelected+1u:add?0u:ordinal+1u,localPicker?total:realCount));
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
  uint actual=localPicker?uint(index):focusIndex(uint(index)),b=slotBase(picker?16u+actual:actual);
  bool selected=!add&&actual==chosen,marked=picker&&(c.bagMeta.z&(1u<<actual))!=0u;
  col=iz2Card(p,col,b,actual,center,selected,marked,localPicker);
 }
 if(empty){
  col=iz2Glass(p,col,vec2(1055,430),vec2(740,174),16,.32);
#ifdef IZ_CORE_LOCAL_ADD
  if(picker&&c.atlas[CORE_LOCAL_ADD]==0x495a4c41u){
   col=mix(col,INK,iz2LocalAddText(p,vec2(408,368),0u));
   col=mix(col,MUTED,iz2LocalAddText(p,vec2(408,452),1u));
  }else{
#endif
  col=mix(col,INK,focusText(p,vec2(408,368),nomatch?F_NORESULTS:F_ADD,.82));
  col=mix(col,MUTED,focusText(p,vec2(410,452),nomatch?F_SEARCHHELP:F_BROWSE,1.1));
#ifdef IZ_CORE_LOCAL_ADD
  }
#endif
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
