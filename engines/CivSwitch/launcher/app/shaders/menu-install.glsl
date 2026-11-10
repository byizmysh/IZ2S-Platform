// Common picker dialog. Menu scratch: index=784, choice=788, target=792,
// availability=796 (low 16 SD, high 16 session). No host request on open.
bool menuInstallAllowed(uint index,uint target){
 if(index>=16u)return false;
 if(c.atlas[(0x700d7cu-1024u)/4u]==1u)return target==2u;
 return (c.game.w&(1u<<(index+(target==1u?16u:0u))))!=0u;
}
#ifndef HB_READONLY
bool menuLaunchErrorInput(uint pressed){
 if(c.state.w==0u)return false;
 c.transition=vec4(0);c.reserved.y=0u;c.reserved.z=0u;
 if((pressed&(HB_A|HB_B|HB_X))!=0u)c.state.w=0u;
 hbEndFrame();return true;
}
void menuInstallOpen(uint index){c.game.x=index+1u;c.game.y=menuInstallAllowed(index,2u)?2u:1u;c.game.z=0u;}
bool menuInstallInput(uint pressed){
 if(c.piece.x==0||c.game.x==0u)return false;
 uint index=c.game.x-1u;bool touching=hbTouchCount()>0u&&(c.touch[6]&2u)==0u;
 vec2 p=touching?hbTouch(0u)*1.5:vec2(0);bool tap=touching&&c.library.w==0u;
 if((pressed&(HB_B|HB_MINUS|HB_X))!=0u){c.game.x=0u;c.game.z=0u;}
 else{
  c.motion.w=max(0.0,c.motion.w-FOCUS_DT);vec2 stick=hbStick(false);
  if((pressed&(HB_LEFT|HB_RIGHT|HB_UP|HB_DOWN))!=0u||(max(abs(stick.x),abs(stick.y))>.55&&c.motion.w<=0.0)){
   uint next=c.game.y==2u?1u:2u;if(menuInstallAllowed(index,next))c.game.y=next;c.motion.w=.18;
  }
  if(tap&&p.y>=490.0&&p.y<=650.0){uint next=p.x>=590.0&&p.x<960.0?2u:p.x>=960.0&&p.x<=1330.0?1u:0u;if(next!=0u&&menuInstallAllowed(index,next))c.game.y=next;}
  bool cancel=tap&&all(lessThanEqual(abs(p-vec2(1210,723)),vec2(120,26)));
  bool confirm=(pressed&HB_A)!=0u||(tap&&all(lessThanEqual(abs(p-vec2(740,723)),vec2(150,26))));
  if(cancel){c.game.x=0u;c.game.z=0u;}
  else if(confirm&&menuInstallAllowed(index,c.game.y)){c.game.z=c.game.y;c.piece.z=c.piece.z>=0x7ffffffe?1:c.piece.z+1;c.piece.w=int(index+1u);c.bagMeta.y=1u;c.game.x=0u;}
 }
 c.library.w=touching?1u:0u;c.bag[7]=1u;hbEndFrame();return true;
}
#else
// Reuse the existing proportional glyphs and their measured advances.
vec3 menuStorageDraw(vec2 p,vec3 col,vec2 origin,uint base){
 bool sd=c.atlas[base+25628u]==0x44534453u;if(!sd&&c.atlas[(0x700d7cu-1024u)/4u]!=2u)return col;
 uvec3 chars=sd?uvec3(83u,68u,0u):uvec3(82u,65u,77u);uint count=sd?2u:3u;float width=0.0;
 for(uint i=0u;i<count;i++)width+=LOAD_ADVANCE[chars[i]-32u]*.65;
 float edge=box(p,origin+vec2((width+12.0)*.5,12),vec2((width+12.0)*.5,12),5);if(edge>1.0)return col;
 col=mix(col,vec3(.025,.04,.065),fill(edge)*.94);vec2 q=(p-origin-vec2(6,1))/.65;float cursor=0.0,mask=0.0;
 for(uint i=0u;i<count;i++){uint glyph=chars[i]-32u;mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,0u));cursor+=LOAD_ADVANCE[glyph];}
 return mix(col,sd?BLUE:INK,mask);
}
const uint INSTALL_CHARS[165]=uint[165](73u,110u,115u,116u,97u,108u,108u,32u,97u,112u,112u,108u,105u,99u,97u,116u,105u,111u,110u,83u,68u,32u,99u,97u,114u,100u,79u,102u,102u,108u,105u,110u,101u,83u,101u,115u,115u,105u,111u,110u,32u,109u,101u,109u,111u,114u,121u,80u,67u,32u,115u,101u,114u,118u,101u,114u,85u,110u,97u,118u,97u,105u,108u,97u,98u,108u,101u,65u,32u,73u,110u,115u,116u,97u,108u,108u,66u,32u,67u,97u,110u,99u,101u,108u,85u,110u,97u,98u,108u,101u,32u,116u,111u,32u,115u,116u,97u,114u,116u,80u,67u,32u,99u,111u,110u,110u,101u,99u,116u,105u,111u,110u,32u,114u,101u,113u,117u,105u,114u,101u,100u,65u,112u,112u,108u,105u,99u,97u,116u,105u,111u,110u,32u,99u,111u,117u,108u,100u,32u,110u,111u,116u,32u,98u,101u,32u,115u,116u,97u,114u,116u,101u,100u,46u,65u,32u,47u,32u,66u,32u,32u,66u,97u,99u,107u);
const uvec2 INSTALL_LABELS[12]=uvec2[12](uvec2(0u,19u),uvec2(19u,7u),uvec2(26u,7u),uvec2(33u,14u),uvec2(47u,9u),uvec2(56u,11u),uvec2(67u,9u),uvec2(76u,8u),uvec2(84u,15u),uvec2(99u,22u),uvec2(121u,33u),uvec2(154u,11u));
uint installWeight(uint which){return which==0u||which==1u||which==3u?0u:1u;}
float installTextWidth(uint which){uvec2 t=INSTALL_LABELS[which];uint weight=installWeight(which);float width=0.0;for(uint i=0u;i<t.y;i++)width+=LOAD_ADVANCE[weight*96u+INSTALL_CHARS[t.x+i]-32u];return width;}
float installText(vec2 p,vec2 origin,uint which,float scale){
 uvec2 t=INSTALL_LABELS[which];uint weight=installWeight(which);vec2 q=(p-origin)/scale;if(q.x<0.0||q.y<0.0||q.y>=32.0)return 0.0;
 float cursor=0.0,mask=0.0;for(uint i=0u;i<t.y;i++){if(cursor>q.x+1.0)break;uint glyph=INSTALL_CHARS[t.x+i]-32u;if(q.x<=cursor+32.0)mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,weight));cursor+=LOAD_ADVANCE[weight*96u+glyph];}return mask;
}
float installPortraitPixel(ivec2 q,uint base){if(any(lessThan(q,ivec2(0)))||q.x>=int(c.atlas[base+25625u])||q.y>=int(c.atlas[base+25626u]))return 0.0;uint n=uint(q.y)*512u+uint(q.x);return float((c.atlas[base+30720u+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;}
float installAppName(vec2 p,vec2 origin,uint base,float scale,bool portrait){if(!portrait)return focusLabel(p,origin,base,scale);vec2 q=(p-origin)/scale;ivec2 at=ivec2(floor(q));vec2 f=fract(q);return mix(mix(installPortraitPixel(at,base),installPortraitPixel(at+ivec2(1,0),base),f.x),mix(installPortraitPixel(at+ivec2(0,1),base),installPortraitPixel(at+ivec2(1),base),f.x),f.y);}
vec3 menuInstallDraw(vec2 p,vec3 col){
 if(c.piece.x==0||c.game.x==0u)return col;
 uint index=min(c.game.x-1u,15u),base=slotBase(16u+index);bool portrait=c.atlas[base+25624u]==0x50534356u;vec3 accent=portrait?vec3(.16,.95,1):BLUE,pane=portrait?vec3(.025,.08,.20):PANEL,field=portrait?vec3(.016,.05,.13):BG,ink=portrait?vec3(.96,.98,1):INK,muted=portrait?vec3(.65,.78,.96):MUTED;col*=.20;
 float shadow=box(p,vec2(960,551),vec2(424,243),26);col=mix(col,vec3(0),fill(shadow)*.5);
 float panel=box(p,vec2(960,540),vec2(420,240),24);col=mix(col,pane,fill(panel));col=mix(col,accent,line(panel,.65)*.45);
 col=mix(col,ink,installText(p,vec2(590,332),0u,1.2));vec4 icon=focusIcon(p,vec2(625,424),base,64);col=mix(col,icon.rgb,icon.a);
 float width=float(c.atlas[base+(portrait?25625u:25600u)]);col=mix(col,ink,installAppName(p,vec2(685,414),base,min(1.0,630.0/max(1.0,width)),portrait));
 for(uint i=0u;i<2u;i++){uint target=i==0u?2u:1u;vec2 center=vec2(i==0u?775:1145,570);bool enabled=menuInstallAllowed(index,target),selected=enabled&&c.game.y==target;float card=box(p,center,vec2(174,80),15);col=mix(col,selected?mix(field,accent,.13):field,fill(card));col=mix(col,selected?accent:muted,line(card,selected?1.2:.6)*(enabled?1.0:.3));
  col=mix(col,enabled?ink:muted*.45,installText(p,center+vec2(-145,-30),i==0u?1u:3u,1));col=mix(col,enabled?muted:muted*.45,installText(p,center+vec2(-145,24),!enabled?5u:i==0u?(c.atlas[base+25629u]==2u?4u:2u):4u,.90));
 }
 col=mix(col,accent,installText(p,vec2(740.0-installTextWidth(6u)*.5,703),6u,1));col=mix(col,muted,installText(p,vec2(1210.0-installTextWidth(7u)*.5,703),7u,1));return col;
}
vec3 menuLaunchErrorDraw(vec2 p,vec3 col){
 if(c.state.w==0u)return col;
 col*=.2;float pane=box(p,vec2(960,540),vec2(420,170),24);col=mix(col,PANEL,fill(pane));col=mix(col,BLUE,line(pane,.8));
 col=mix(col,INK,installText(p,vec2(600,405),8u,1.2));uint message=c.state.w==2u?9u:10u;float scale=min(1.0,700.0/max(1.0,installTextWidth(message)));col=mix(col,MUTED,installText(p,vec2(600,505),message,scale));
 col=mix(col,BLUE,installText(p,vec2(960.0-installTextWidth(11u)*.5,625),11u,1));return col;
}
#endif
