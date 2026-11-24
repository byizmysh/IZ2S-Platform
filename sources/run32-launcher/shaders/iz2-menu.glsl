// Visual-only library scene. All catalog records, launch requests and overlays
// continue to use the existing ABI-1 buffers and compute state machine.
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
 vec3 typeInk=nro?vec3(1,.89,.58):vec3(.73,.89,1);
 col=mix(col,typeInk,line(d,.7)*.45);
 vec2 q=(p-origin-vec2(8,3))/scale;float ink=0.0;
 for(uint i=0u;i<4u;i++){uint glyph=(nro?nativeType[i]:appType[i])-32u;ink=max(ink,loadProgressGlyph(q,glyph,0u));q.x-=LOAD_ADVANCE[glyph];}
 return mix(col,typeInk,ink);
}
#ifdef IZ_CORE_LOCAL_ADD
vec3 iz2StoreVersion(vec2 cardPoint,vec3 col,uint card);
#endif
vec3 iz2Card(vec2 p,vec3 col,uint b,uint identity,vec2 center,bool selected,bool marked,bool showVersion){
 float grow=iz2Grow((b-524288u)/32768u);
 float scaleCard=1.0+.023*grow;
 center.y-=grow*18.0;
 vec2 q=(p-center)/scaleCard;
 if(any(greaterThan(abs(q),FOCUS_CARD_HALF+vec2(22,28))))return col;
 float d=box(q,vec2(0),FOCUS_CARD_HALF,19.0);
 uint kind=iz2CoverKind(b,identity);
 vec3 accent=iz2CoverAccent(kind);
 float time=c.motion.x;
 float focusMotion=selected?smoothstep(0.0,.62,uintBitsToFloat(c.atlas[IZ2_FOCUS_AGE]))*grow:0.0;
 if(d>0.0){
  float shadow=1.0-smoothstep(0.0,21.0,box(q-vec2(0,8),vec2(0),FOCUS_CARD_HALF,19.0));
  col=mix(col,vec3(.009,.014,.032),shadow*.42);
  float halo=1.0-smoothstep(0.0,19.0,d);
  col+=accent*halo*(.009+.095*grow);
 }
 if(d<1.0){
  vec2 cover=q/FOCUS_CARD_HALF;
  vec2 lightAt=vec2(-.46,-.74)+focusMotion*vec2(.63*sin(time*1.38),.21*cos(time*1.04));
  float light=1.0-smoothstep(.04,1.61,length((cover-lightAt)*vec2(.89,1)));
  vec3 card=mix(vec3(.042,.055,.103),accent,.065+(.35+.24*grow)*light);
  float pattern=0.0;
  vec2 art=cover+focusMotion*vec2(.075*sin(time*.89),.12*sin(time*1.18));
  if(kind==0u){
   vec2 cell=fract((art+vec2(art.y*.38,0))*1.12)-.5;
   pattern=1.0-smoothstep(.18,.22,length(cell));
  }else if(kind==1u){
   pattern=1.0-smoothstep(.032,.065,abs(fract((art.x*.54+art.y)*1.75)-.5));
  }else if(kind==3u){
   vec2 grid=abs(fract(art*vec2(3.0,4.0))-.5);
   pattern=smoothstep(.438,.468,max(grid.x,grid.y))*.68;
  }else if(kind==4u){
   pattern=smoothstep(.05,.085,art.x-art.y*.79)*.68;
  }else{
   vec2 knot=vec2(kind==2u?-1.07:.86,-.61);
   float rings=length((art-knot)*vec2(1.0,1.34));
   pattern=1.0-smoothstep(.032,.10,abs(fract(rings*2.2)-.5));
  }
  float patternFade=1.0-smoothstep(-.24,.63,cover.y);
  card=mix(card,mix(accent,vec3(.79,.87,1),.18),pattern*patternFade*(.19+.13*grow));
  float glimmer=1.0-smoothstep(.02,.31,abs(cover.x+cover.y*.55-.77*sin(time*1.52)));
  card+=mix(accent,vec3(.88,.94,1),.46)*glimmer*patternFade*focusMotion*.19;
  float shade=smoothstep(.21,.92,cover.y);
  card=mix(card,vec3(.027,.037,.065),shade*.93);
  vec2 artwork=vec2(0,-11.0-5.0*grow);
  float well=box(q,artwork+vec2(0,2),vec2(96,102),19);
  card=mix(card,vec3(.027,.034,.059),fill(well)*.26);
  float bevel=line(well,.55)*smoothstep(-.5,.4,-cover.y);
  card=mix(card,accent,bevel*(.08+.14*grow));
  vec4 pic=appIcon(center+q,center+artwork,b,179.0+6.0*grow,INK);
  card=mix(card,pic.rgb,pic.a);
  col=mix(col,card,fill(d));
  float width=float(c.atlas[b+25600u]),size=min(.86,208.0/max(width,1.0));
  float titleY=c.atlas[b+25632u]==0x32465454u?157.0-float(c.atlas[b+25601u])*size*.5:146.0;
  col=mix(col,INK,focusLabel(center+q,center+vec2(-width*size*.5,titleY),b,size));
  col=iz2Storage(center+q,col,center+vec2(-104,-190),b);
  col=iz2ExecutableType(center+q,col,center+vec2(104,-190),b);
#ifdef IZ_CORE_LOCAL_ADD
  if(showVersion)col=iz2StoreVersion(q,col,identity);
#endif
  if(marked){vec2 tick=q-vec2(88,114);float mark=min(stroke(tick,vec2(-6,0),vec2(-1,5)),stroke(tick,vec2(-1,5),vec2(10,-7)));col=mix(col,BLUE,fill(mark-1.5));}
 }
 vec3 border=mix(mix(accent,vec3(.70,.82,1),.45),vec3(.93,.98,1),grow);
 col=mix(col,border,line(d,.8+grow*.85)*(.27+.66*grow));
 return col;
}
vec3 iz2AppVersion(vec2 p,vec3 col,uint slot,vec2 origin){
 uint base=IZ2_MENU_VERSIONS+4u*slot;
 if((c.atlas[base]&255u)==0u)return col;
 const float scale=.92;
 float width=LOAD_ADVANCE[96u+86u]+3.0;
 for(uint i=0u;i<15u;i++){
  uint ch=(c.atlas[base+i/4u]>>((i&3u)*8u))&255u;
  if(ch<32u||ch>126u)break;
  width+=LOAD_ADVANCE[96u+ch-32u];
 }
 width=width*scale+24.0;
 float d=box(p,origin+vec2(width*.5,0),vec2(width*.5,18),9);
 if(d>1.0)return col;
 col=mix(col,vec3(.19,.26,.38),fill(d)*.65);
 col=mix(col,vec3(.53,.68,.82),line(d,.7)*.35);
 vec2 q=(p-origin-vec2(12,-15))/scale;
 float ink=loadProgressGlyph(q,86u,1u),x=LOAD_ADVANCE[96u+86u]+3.0;
 for(uint i=0u;i<15u;i++){
  uint ch=(c.atlas[base+i/4u]>>((i&3u)*8u))&255u;
  if(ch<32u||ch>126u)break;
  uint glyph=ch-32u;
  if(q.x>=x-1.0&&q.x<x+32.0)ink=max(ink,loadProgressGlyph(q-vec2(x,0),glyph,1u));
  x+=LOAD_ADVANCE[96u+glyph];
 }
 return mix(col,vec3(.80,.87,.95),ink);
}
float iz2ActivityPixel(ivec2 q){
 if(any(lessThan(q,ivec2(0)))||q.x>=1024||q.y>=512)return 0.0;uint n=uint(q.y*1024+q.x);return float((c.atlas[IZ2_ACTIVITY_TEXT+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;
}
float iz2ActivityText(vec2 p,vec2 origin,uint row,ivec4 bounds,float scale){
 vec2 q=(p-origin)/scale;
 if(any(lessThan(q,vec2(0)))||any(greaterThanEqual(q,vec2(bounds.zw))))return 0.0;
 q+=vec2(bounds.xy)+vec2(0,float(row)*64.0);
 ivec2 xy=ivec2(floor(q));vec2 f=fract(q);
 return mix(mix(iz2ActivityPixel(xy),iz2ActivityPixel(xy+ivec2(1,0)),f.x),mix(iz2ActivityPixel(xy+ivec2(0,1)),iz2ActivityPixel(xy+ivec2(1)),f.x),f.y);
}
void iz2ActivityDraw(vec2 p,inout vec3 col){
 if(p.x<80.0||p.x>1336.0||p.y<778.0)return;
 col=mix(col,INK,focusText(p,vec2(80,778),F_RECENT,1.14));
 uint count=iz2ActivityCount();
 if(count==0u){
  vec2 clock=p-vec2(94,840);
  float face=line(length(clock)-10.0,1.0);
  float hands=min(stroke(clock,vec2(0),vec2(0,-5)),stroke(clock,vec2(0),vec2(4,2)));
  col=mix(col,MUTED,max(face,fill(hands-.85))*.80);
  col=mix(col,MUTED,focusText(p,vec2(120,828),F_NOACTIVITY,1.15));
  return;
 }
 if(p.y<IZ2_ACTIVITY_TOP||p.y>=IZ2_ACTIVITY_TOP+float(count)*IZ2_ACTIVITY_ROW)return;
 uint row=uint((p.y-IZ2_ACTIVITY_TOP)/IZ2_ACTIVITY_ROW);
 float y=IZ2_ACTIVITY_TOP+float(row)*IZ2_ACTIVITY_ROW;
 float panel=box(p,vec2(708,y+25),vec2(628,25),10.0);
 col=mix(col,vec3(.055,.078,.141),fill(panel)*.88);
 uint index=c.atlas[IZ2_ACTIVITY+4u+row*4u];
 uint progress=c.atlas[IZ2_ACTIVITY+6u+row*4u];
 vec2 center=vec2(113,y+25);
 if(index<min(c.library.x,15u)){
  vec4 pic=focusIcon(p,center,slotBase(index),37.0);col=mix(col,pic.rgb,pic.a);
 }else{
  vec2 mark=p-center;
  float tick=min(stroke(mark,vec2(-6,0),vec2(-2,4)),stroke(mark,vec2(-2,4),vec2(7,-5)));
  float symbol=progress==0u?fill(tick-.9):fill(length(mark)-3.0);
  col=mix(col,BLUE,max(symbol,line(length(mark)-12.0,1.0)));
 }
 col=mix(col,INK,iz2ActivityText(p,vec2(146,y+3),row,ivec4(0,0,496,24),1.03));
 col=mix(col,MUTED,iz2ActivityText(p,vec2(146,y+27),row,ivec4(0,28,496,23),1.0));
 col=mix(col,MUTED,iz2ActivityText(p,vec2(1054,y+13),row,ivec4(580,0,208,27),1.17));
 if(progress==0u)return;
 const float left=760.0,width=238.0;
 float done=width*clamp(float(progress-1u)/100.0,0.0,1.0);
 float track=box(p,vec2(left+width*.5,y+27),vec2(width*.5,3),3);
 col=mix(col,vec3(.15,.2,.34),fill(track));
 float halfSize=done>0.0?done*.5:13.0;
 float offset=done>0.0?halfSize:width*fract(c.motion.x*.62);
 float bar=box(p,vec2(left+offset,y+27),vec2(halfSize,3),min(halfSize,3.0));
 col=mix(col,BLUE,fill(bar)*fill(track));
}
#ifdef IZ_CORE_LOCAL_ADD
// The standalone launcher publishes the SD import receipt in this shared slot.
const uint CORE_FTP=CORE_GRID+4u;
// The native text lives below the legacy bitmap glyphs.
const uint CORE_FTP_TEXT=CORE_FTP+(1024u*48u-32u)/4u;
const float CORE_FTP_WIDTH=352.0,CORE_SHORTCUT_GAP=16.0,CORE_STATUS_WIDTH=148.0;
const float CORE_PC_WIDTH=352.0,CORE_NET_GAP=12.0;
// Keep Status, PC Link and FTP together at the right edge of one footer row.
float coreShortcutLeft(){
 return hasPcLinkShortcuts()?1840.0-CORE_STATUS_WIDTH-2.0*CORE_NET_GAP-CORE_PC_WIDTH-CORE_FTP_WIDTH:
   1840.0-CORE_STATUS_WIDTH-CORE_SHORTCUT_GAP-CORE_FTP_WIDTH;
}
float corePcLeft(){return coreShortcutLeft()+CORE_STATUS_WIDTH+CORE_NET_GAP;}
float coreFtpLeft(){return hasPcLinkShortcuts()?corePcLeft()+CORE_PC_WIDTH+CORE_NET_GAP:
 coreShortcutLeft()+CORE_STATUS_WIDTH+CORE_SHORTCUT_GAP;}
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

float iz2NetworkChord(vec2 p,bool pc,bool updated){
 vec2 left=vec2(36,1008),right=vec2(84,1008),plus=(left+right)*.5;
 float first=updated?max(line(box(p,left,vec2(18,13),7),.9),
   focusAscii((p-left)/.65,vec2(-8,-9),pc?82u:76u)):rsStickKeycap(p,left,.65,76u);
 float chord=max(first,rsStickKeycap(p,right,.65,pc?76u:82u));
 return max(chord,fill(min(stroke(p,plus-vec2(3,0),plus+vec2(3,0)),stroke(p,plus-vec2(0,3),plus+vec2(0,3)))-.65));
}

void iz2PcFooter(vec2 p,inout vec3 col){
 if(!hasPcLinkShortcuts()||!rsInlineAllowed()||loadProgressVisible())return;
 p.x-=corePcLeft();
 if(p.x< -1.0||p.x>CORE_PC_WIDTH+1.0||abs(p.y-1008.0)>21.0)return;
 uint state=c.atlas[PC_LINK_MENU+1u]&255u;
 vec3 accent=state==2u?vec3(.63,.94,.77):state==1u?vec3(.98,.78,.38):vec3(.66,.73,.85);
 float frame=box(p,vec2(CORE_PC_WIDTH*.5,1008),vec2(CORE_PC_WIDTH*.5,20),13);
 col=mix(col,accent*.22,fill(frame)*.90);
 col=mix(col,accent,line(frame,.65)*.65);
 col=mix(col,accent,iz2NetworkChord(p,true,true));
 const uint label[12]=uint[12](80u,67u,32u,76u,73u,78u,75u,0u,0u,0u,0u,0u);
 float ink=rsType(p,vec2(124,1008-9*.56),label,.56);
 col=mix(col,accent,ink);
}

void iz2FtpFooter(vec2 p,inout vec3 col){
 p.x-=coreFtpLeft();
 if(p.x< -1.0||p.x>=CORE_FTP_WIDTH+2.0||p.y<987.0||p.y>=1029.0||
    !rsInlineAllowed()||loadProgressVisible())return;
 uint ftp=c.atlas[CORE_GRID+3u];
 vec2 halfSize=vec2(CORE_FTP_WIDTH*.5,20);
 float frame=box(p,vec2(halfSize.x,1008),halfSize,13);
 vec3 border=vec3(.52,.77,.65);
 vec3 tint=mix(vec3(.14,.28,.24),vec3(.18,.35,.29),clamp(p.x/CORE_FTP_WIDTH,0.0,1.0));
 if(ftp==1u)tint+=vec3(.025,.045,.020);
 col=mix(col,tint,fill(frame)*.90);
 col=mix(col,border,line(frame,.65)*.70);

 vec2 left=vec2(36,1008),right=vec2(84,1008);
 float keys=max(fill(box(p,left,vec2(18,13),7)),fill(box(p,right,vec2(18,13),7)));
 col=mix(col,vec3(.09,.21,.19),keys*.68);
 float chord=iz2NetworkChord(p,false,hasPcLinkShortcuts());
 col=mix(col,vec3(.77,.92,.83),chord);
 col=mix(col,border,fill(box(p,vec2(108,1008),vec2(.55,9),0))*.45);

 if(p.x<118.0)return;
 const float scale=.48;
 vec2 q=(p-vec2(118,1008-9*scale))/scale;
 const uint enable[12]=uint[12](69u,78u,65u,66u,76u,69u,32u,70u,84u,80u,0u,0u);
 // Older runtimes still report the listening flag. Their FTP server binds
 // this fixed endpoint, even when they do not publish the text payload.
 const uint endpoint[19]=uint[19](70u,84u,80u,32u,49u,48u,46u,52u,50u,46u,48u,46u,50u,48u,58u,49u,52u,50u,53u);
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
void iz2Connections(vec2 p,inout vec3 col){
 if(!rsInlineAllowed()||loadProgressVisible())return;
 const uint network[12]=uint[12](78u,101u,116u,119u,111u,114u,107u,0u,0u,0u,0u,0u);
 const uint title[12]=uint[12](67u,79u,78u,78u,69u,67u,84u,73u,79u,78u,83u,0u);
 bool online=c.atlas[CORE_GRID+3u]==1u||(hasPcLinkShortcuts()&&(c.atlas[PC_LINK_MENU+1u]&255u)==2u);
 if(p.x>=586.0&&p.x<=790.0&&abs(p.y-1008.0)<21.0){
  float key=rsStickKeycap(p,vec2(610,1008),.75,76u);
  float label=chromeWord(p,vec2(640,1008-17.0*1.15),network,7u,1.15);
  col=mix(col,INK,max(key,label));
  if(online)col=mix(col,vec3(.67,1,.59),fill(length(p-vec2(756,1008))-3.2));
 }
 if(c.atlas[IZ2_ANIM]!=IZ2_ANIM_MAGIC)return;
 float progress=uintBitsToFloat(c.atlas[IZ2_NETWORK_OPEN]);
 if(progress<.006)return;
 const float width=428.0;
 float slide=(1.0-progress)*width;
 col*=1.0-.17*progress;
 vec2 drawer=p+vec2(slide,0);
 float frame=box(drawer,vec2(206,861),vec2(222,94),16);
 if(frame>1.0)return;
 col=mix(col,vec3(.063,.084,.151),fill(frame));
 col=mix(col,vec3(.34,.43,.65),line(frame,.7));
 col=mix(col,INK,rsType(drawer,vec2(35,789),title,.83));
 col=mix(col,MUTED,focusText(drawer,vec2(323,789),F_BBACK,.91));
 iz2FtpFooter(drawer+vec2(coreFtpLeft()-35,1008-843),col);
 iz2PcFooter(drawer+vec2(corePcLeft()-35,1008-901),col);
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
 bool add=false,empty=realCount==0u,nomatch=empty&&focusRawCount(picker)>0u;
 uint base=slotBase(picker?16u+min(chosen,15u):min(chosen,14u));
 if(p.y<304.0){
  if(picker||empty||add){
   ivec4 label=picker?F_PACKAGES:empty?F_LIBRARY:F_ADDSMALL;
   col=mix(col,INK,focusText(p,vec2(80,179),label,2.4));
  }else{
   // One title row: icon, native-resolution bold name, centre-aligned version.
   vec3 accent=iz2CoverAccent(iz2CoverKind(base,chosen));
   vec2 titleCenter=vec2(136,207);
   float bloom=exp(-dot((p-titleCenter)/vec2(142,92),(p-titleCenter)/vec2(142,92))*1.9);
   col+=accent*bloom*.13;
   if(p.x<218.0){
    float well=box(p,titleCenter,vec2(54),17);
    col=mix(col,vec3(.045,.065,.11),fill(well)*.85);
    col=mix(col,accent,line(well,.8)*.48);
    vec4 pic=focusIcon(p,titleCenter,base,83.0);col=mix(col,pic.rgb,pic.a);
   }
   // Quiet enlarged artwork in the background, with a bounded drawing region.
   if(p.x>1390.0&&p.x<1840.0){
    vec2 center=vec2(1660,229);
    vec4 ghost=focusIcon(p,center,base,260.0);
    float fade=smoothstep(1450.0,1620.0,p.x)*(1.0-smoothstep(250.0,308.0,p.y));
    col=mix(col,mix(ghost.rgb,accent,.25),ghost.a*fade*.15);
   }
   float width=float(c.atlas[base+25605u]),height=float(c.atlas[base+25606u]);
   if(width>0.0&&width<=512.0&&height>0.0&&height<=72.0){
    col=mix(col,INK,heroLabel(p,vec2(220,207-height*.5),base));
   }else{
    width=float(c.atlas[base+25600u]);height=float(c.atlas[base+25601u]);
    float size=min(1.4,54.0/max(height,1.0));
    col=mix(col,INK,focusLabel(p,vec2(220,207-height*size*.5),base,size));
    width*=size;
   }
   col=iz2AppVersion(p,col,chosen,vec2(220+width+24,207));
   col=mix(col,accent,fill(box(p,vec2(245,262),vec2(25,2),2))*.9);
  }
 }
 int first=int(floor(c.flow.x));
 bool selectedVisible=false;
 vec2 selectedCenter=vec2(0);
 for(int i=-1;i<=7;i++){
  int index=first+i;if(index<0||index>=int(realCount))continue;
  vec2 center=focusCardCenter(float(index));if(!focusCardVisible(center))continue;
  uint actual=localPicker?uint(index):focusIndex(uint(index)),b=slotBase(picker?16u+actual:actual);
  bool selected=!add&&actual==chosen,marked=picker&&(c.bagMeta.z&(1u<<actual))!=0u;
  if(selected){selectedVisible=true;selectedCenter=center;}
  else col=iz2Card(p,col,b,actual,center,false,marked,localPicker);
 }
 if(selectedVisible){
  bool marked=picker&&(c.bagMeta.z&(1u<<chosen))!=0u;
  col=iz2Card(p,col,base,chosen,selectedCenter,true,marked,localPicker);
 }
 if(empty){
  col=iz2Glass(p,col,vec2(1055,550),vec2(740,174),16,.32);
#ifdef IZ_CORE_LOCAL_ADD
  if(picker&&c.atlas[CORE_LOCAL_ADD]==0x495a4c41u){
   col=mix(col,INK,iz2LocalAddText(p,vec2(408,488),0u));
   col=mix(col,MUTED,iz2LocalAddText(p,vec2(408,572),1u));
  }else{
#endif
  col=mix(col,INK,focusText(p,vec2(408,488),nomatch?F_NORESULTS:F_ADD,.82));
  col=mix(col,MUTED,focusText(p,vec2(410,572),nomatch?F_SEARCHHELP:F_BROWSE,1.1));
#ifdef IZ_CORE_LOCAL_ADD
  }
#endif
 }
 // Navigation controls use the same shared centres as touch hit testing.
 if(realCount>7u){
  float prev=min(stroke(p,FOCUS_PREV+vec2(7,-13),FOCUS_PREV+vec2(-5,0)),stroke(p,FOCUS_PREV+vec2(-5,0),FOCUS_PREV+vec2(7,13)));
  float next=min(stroke(p,FOCUS_NEXT+vec2(-7,-13),FOCUS_NEXT+vec2(5,0)),stroke(p,FOCUS_NEXT+vec2(5,0),FOCUS_NEXT+vec2(-7,13)));col=mix(col,INK,fill(min(prev,next)-1.1));
 }
 iz2ActivityDraw(p,col);
 return col;
}
vec3 iz2MenuDraw(vec2 p){
 vec3 col=focusBackground(p);float scroll=clamp(uintBitsToFloat(c.rows[13]),0.0,iz2ScrollMax());
 if(iz2ContentHit(p)){vec3 content=iz2Content(p+vec2(0,scroll),col);col=mix(col,content,smoothstep(IZ2_CONTENT_TOP,IZ2_CONTENT_TOP+18.0,p.y)*(1.0-smoothstep(IZ2_CONTENT_BOTTOM-20.0,IZ2_CONTENT_BOTTOM,p.y)));}
 vec2 header=p+vec2(0,FOCUS_HEADER_RISE);
 menuHeader(header,col);focusHeader(header,col);iz2ClockDraw(header,col);iz2BatteryDraw(header,col);

 return col;
}

vec3 iz2LaunchTransition(vec2 p,vec3 col){
 float phase=clamp(c.transition.x,0.0,1.0);
 if(phase<=0.0)return col;
 if(c.transition.y==0.0||c.state.x>=min(c.library.x,15u))return col*(1.0-phase);
 float progress=smoothstep(0.0,.83,phase);
 vec2 origin=focusCardCenter(float(c.rows[11]));
 origin.y-=24.0+clamp(uintBitsToFloat(c.rows[13]),0.0,iz2ScrollMax());
 vec2 center=mix(origin,vec2(960,540),progress);
 vec2 halfSize=mix(FOCUS_CARD_HALF*1.03,vec2(970,550),progress);
 float panel=fill(box(p,center,halfSize,17.0*(1.0-progress)));
 uint base=slotBase(c.state.x);
 vec3 tint=iz2CoverAccent(iz2CoverKind(base,c.state.x));
 float glow=1.0-smoothstep(0.0,1020.0,length(p-center));
 vec3 screen=mix(vec3(.032,.041,.086),tint,.29*glow);
 vec4 mark=appIcon(p,center-vec2(0,22),base,mix(194.0,246.0,progress),INK);
 screen=mix(screen,mark.rgb,mark.a);
 col=mix(col,screen,panel*smoothstep(0.0,.23,phase));
 return col*(1.0-smoothstep(.70,1.0,phase));
}
