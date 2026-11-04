#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#include "launcher-state.glsl"
#define HB_READONLY
#include "hb.glsl"
#include "resilience.glsl"
#include "launcher-text.glsl"
#include "iz-draw.glsl"
#include "focus-layout.glsl"
#include "focus-library.glsl"
#include "focus-ui.glsl"
#include "load-progress.glsl"
#include "load-progress-draw.glsl"
#include "run10-chrome.glsl"
#include "focus-icons.glsl"
#include "focus-light.glsl"
#include "features.glsl"
#include "app-menu.glsl"
#if defined(IZ_CALC) || defined(IZ_STREAM) || defined(IZ_CONTROLLERS)
#include "tiny-calc.glsl"
#include "mini-text.glsl"
#endif
#ifdef IZ_CALC
#include "tiny-calc-draw.glsl"
#endif
#ifdef IZ_STREAM
#include "pc-screen.glsl"
#define IZ_STREAM_FOCUS_TEXT 1
#include "pc-screen-draw.glsl"
#ifdef IZ_VACOOTUBE_MENU
#include "vacootube-menu.glsl"
#endif
#ifdef IZ_GTAV_HUD
#include "gtav-hud.glsl"
#endif
#endif
#ifdef IZ_CONTROLLERS
#include "controller-hub.glsl"
#include "controller-hub-draw.glsl"
#endif
vec3 notification(vec2 p,vec3 col){
 float left=uintBitsToFloat(c.atlas[TOAST+4u]);if(c.atlas[TOAST]!=0x495a4e31u||left<=0.0)return col;
 float age=TOAST_SECONDS-left,fade=smoothstep(0.0,.18,age)*smoothstep(0.0,.25,left);vec2 center=vec2(1604,915)+vec2(0,(1.0-fade)*12.0);float d=box(p,center,vec2(252,65),13.0);
 col=mix(col,vec3(.015),fill(box(p,center+vec2(0,7),vec2(254,67),15))*.55*fade);
 col=mix(col,vec3(.10,.12,.11),fill(d)*.96*fade);col=mix(col,vec3(.31,.37,.24),line(d,.65)*fade);
 float stripe=box(p,center-vec2(250,0),vec2(1.6,44),1.5);col=mix(col,BLUE,fill(stripe)*fade);
 vec4 pic=appIcon(p,center-vec2(203,1),TOAST_SLOT,48.0,BLUE);col=mix(col,pic.rgb,pic.a*fade);
 col=mix(col,MUTED,focusText(p,center+vec2(-159,-35),F_SUCCESS,1.0)*fade);
 float width=float(c.atlas[TOAST_SLOT+25600u]),scale=min(.9,315.0/max(width,1.0));col=mix(col,INK,focusLabel(p,center+vec2(-159,-3),TOAST_SLOT,scale)*fade);
 vec2 tick=center+vec2(213,-28);float mark=min(stroke(p,tick+vec2(-6,0),tick+vec2(-1,5)),stroke(p,tick+vec2(-1,5),tick+vec2(9,-6)));col=mix(col,BLUE,fill(mark-1.2)*fade);
 float track=box(p,center+vec2(24,45),vec2(183,1.2),1.0);col=mix(col,vec3(.24,.28,.21),fill(track)*fade);float w=183.0*clamp(left/TOAST_SECONDS,0,1);col=mix(col,BLUE,fill(box(p,center+vec2(-159+w,45),vec2(w,1.2),1.0))*fade);
 return col;
}
void focusHeader(vec2 p,inout vec3 col){
 if(p.y<64.0||p.y>132.0)return;
 vec2 centers[3]=vec2[3](vec2(1162,95),vec2(1335,95),vec2(1500,95));ivec4 labels[3]=ivec4[3](F_ALL,F_GAMES,F_TOOLS);
 for(uint i=0u;i<3u;i++){bool selected=c.rows[0]==i;float d=box(p,centers[i],vec2(57,25),25.0);if(selected)col=mix(col,BLUE,fill(d));col=mix(col,selected?BG:INK,focusText(p,centers[i]-vec2(labels[i].zw)*.5,labels[i],1));}
 col=mix(col,vec3(.35),fill(box(p,vec2(1251,95),vec2(.5,12),0)));col=mix(col,vec3(.35),fill(box(p,vec2(1425,95),vec2(.5,12),0)));col=mix(col,vec3(.35),fill(box(p,vec2(1610,95),vec2(.5,22),0)));
 col=mix(col,INK,line(length(p-vec2(1680,95))-20.0,1.4));col=mix(col,INK,focusText(p,vec2(1680,95)-vec2(F_Y.zw)*.5,F_Y,1));col=mix(col,INK,focusText(p,vec2(1720,86),F_SEARCH,1));
}
vec3 focusSearchDraw(vec2 p,vec3 col){
 col*=.28;float d=box(p,vec2(960,550),vec2(490,285),22);col=mix(col,vec3(.065,.075,.068),fill(d));col=mix(col,vec3(.35,.39,.29),line(d,.8));col=mix(col,INK,focusText(p,vec2(570,304),F_FIND,1));
 col=mix(col,vec3(.035),fill(box(p,vec2(960,389),vec2(390,30),7)));col=mix(col,c.rows[2]>0u?INK:MUTED,c.rows[2]>0u?focusQueryText(p,vec2(590,378)):focusText(p,vec2(590,381),F_SEARCH,1));
 for(uint i=0u;i<40u;i++){vec2 center=focusKeyCenter(i);if(any(greaterThan(abs(p-center),vec2(46,26))))continue;bool selected=i==c.rows[7];col=mix(col,selected?BLUE:PANEL,fill(box(p,center,vec2(44,24),7)));vec3 ink=selected?BG:INK;float m=0;
 if(i<36u){uint ch=focusKeyCode(i);if(ch>=97u)ch-=32u;m=focusAscii(p,center-vec2(9,10),ch);}else if(i==36u)m=focusText(p,center-vec2(F_SPACE.zw)*.5,F_SPACE,1);else if(i==38u)m=focusText(p,center-vec2(F_CLEAR.zw)*.5,F_CLEAR,1);else if(i==39u)m=focusText(p,center-vec2(F_DONE.zw)*.5,F_DONE,1);else m=fill(min(stroke(p,center+vec2(10,0),center-vec2(9,0)),min(stroke(p,center-vec2(9,0),center-vec2(2,7)),stroke(p,center-vec2(9,0),center+vec2(-2,-7))))-1.2);col=mix(col,ink,m);}
 col=mix(col,MUTED,focusText(p,vec2(570,785),F_ASELECT,1));col=mix(col,MUTED,focusText(p,vec2(765,785),F_BBACK,1));
 return col;
}
#include "menu-removal-draw.glsl"
#include "resilience-draw.glsl"
void main(){
 vec2 p=gl_FragCoord.xy;
#ifdef IZ_CONTROLLERS
 if(c.atlas[CB+7u]==CMAGIC&&c.atlas[CB+1u]==3u){outColor=vec4(controllerHubDraw(p),1);return;}
#endif
#ifdef IZ_CALC
 if(c.atlas[CB+7u]==CMAGIC&&c.atlas[CB+1u]==1u){outColor=vec4(notification(p,calcDraw(p)),1);return;}
#endif
#ifdef IZ_STREAM
 if(c.atlas[CB+7u]==CMAGIC&&c.atlas[CB+1u]==2u){vec3 video;
#ifdef IZ_GTAV_HUD
 if(c.atlas[SB+40u]==5u)video=gtavStreamDraw(p);else
#endif

#ifdef IZ_VACOOTUBE_MENU
 if(vacooMenuActive())video=vacooMenuDraw(p);else
#endif
 video=streamDraw(p);
 outColor=vec4(c.atlas[SB+41u]==1u?notification(p,video):video,1);return;}
#endif
 vec3 col=focusBackground(p);bool picker=c.piece.x!=0;uint installed=min(c.library.x,15u),realCount=focusCount(picker);
 uint chosen=picker?uint(max(0,c.piece.y)):c.state.x;uint ordinal=c.rows[19]==0x464f4355u?min(c.rows[11],realCount):chosen;bool add=!picker&&(chosen>=installed||ordinal>=realCount),empty=realCount==0u,nomatch=empty&&focusRawCount(picker)>0u;
 uint base=slotBase(picker?16u+min(chosen,15u):min(chosen,14u));
 menuHeader(p,col);focusHeader(p,col);
 if(picker)col=mix(col,MUTED,focusText(p,vec2(780,246),F_PACKAGES,1.0));
 vec2 q=focusHeroLocal(p);float hd=box(q,vec2(0),vec2(180,184),31.0);
 float extrusion=box(q+vec2(10,-2),vec2(0),vec2(180,184),31.0);col=mix(col,vec3(.13,.19,.02),fill(extrusion));
 col+=BLUE*exp(-max(hd,0.0)/26.0)*.12*step(0.0,hd);
 float gloss=pow(clamp(.5-q.y/600.0+q.x/1100.0,0,1),2.0);vec3 surface=vec3(.051,.060,.055)+vec3(.14,.14,.115)*gloss;
 col=mix(col,surface,fill(hd));col=mix(col,mix(BLUE,INK,.32+.25*smoothstep(-100,180,q.x)),line(hd,1.45));
 if(add||empty)col=mix(col,BLUE,focusPlus(HERO_CENTER+q,HERO_CENTER,47.0));
 else{vec4 pic=appIcon(HERO_CENTER+q,HERO_CENTER,base,316.0,INK);col=mix(col,pic.rgb,pic.a);}
 if(add||empty){col=mix(col,INK,focusText(p,vec2(780,309),nomatch?F_NORESULTS:F_ADD,1.0));col=mix(col,MUTED,focusText(p,vec2(784,420),nomatch?F_SEARCHHELP:F_BROWSE,1.0));}
 else{col=mix(col,INK,heroLabel(p,vec2(780,287),base));col=mix(col,MUTED,focusText(p,vec2(784,420),picker?F_LOAD:(c.atlas[base+25604u]==4u?F_PLAY:F_READY),1.0));}
 if(c.bagMeta.y==0u&&(!picker||realCount>0u)){vec2 action=ACTION_CENTER;float bd=box(p,action,vec2(123,40),40.0);col+=BLUE*exp(-abs(bd)/12.0)*.075;col=mix(col,BLUE,line(bd,2.1));col=mix(col,BLUE,line(length(p-(action-vec2(83,0)))-22.0,1.9));col=mix(col,BLUE,text(p,action-vec2(83,0)-vec2(TEXT_BUTTON_A.zw)*.5,TEXT_BUTTON_A));col=mix(col,BLUE,focusText(p,action-vec2(27,11),picker?F_INSTALL:(add||empty?F_BROWSEBTN:F_OPEN),1.0));}
 if(c.bagMeta.y!=0u){vec2 v=p-vec2(1200,515);float a=atan(v.y,v.x)-c.motion.x*5.0;col=mix(col,BLUE,line(length(v)-10.0,1.7)*smoothstep(-1.0,1.0,sin(a)));}
 // The add station stays fixed; only six neighbors and edge previews are drawn.
 bool addSelected=!picker&&add;float ad=length(p-ADD_CENTER)-68.0;
 col+=WARM*exp(-abs(ad)/12.0)*.035;vec3 addSurface=vec3(.045)+vec3(.034)*clamp((ADD_CENTER.y+70.0-p.y)/140.0,0,1);
 col=mix(col,addSurface,fill(ad));col=mix(col,addSelected?BLUE:INK,line(ad,addSelected?1.9:1.4));col=mix(col,addSelected?BLUE:INK,focusPlus(p,ADD_CENTER,23.0));col=mix(col,INK,focusText(p,ADD_CENTER+vec2(-float(F_ADDSMALL.z)*.5,81),F_ADDSMALL,1.0));
 // A narrow edge preview remains visible while the selected card gets more presence.
 int first=int(floor(c.flow.x));for(int i=-1;i<7;i++){
  int index=first+i;if(index<0||index>=int(realCount))continue;vec2 center=focusCardCenter(float(index));if(center.x<280.0||center.x>1870.0)continue;
  uint actual=focusIndex(uint(index)),atlas=picker?16u+actual:actual,b=slotBase(atlas);bool selected=!add&&actual==chosen;vec2 local=focusCardLocal(p,float(index));float scaleCard=selected?1.10:1.0;local/=scaleCard;
  float d=box(local,vec2(0),vec2(92,100),12.0),clip=smoothstep(268.0,292.0,p.x)*(1.0-smoothstep(1845.0,1872.0,p.x));vec3 cardInk=selected?BG:INK;
  if(selected)col+=BLUE*exp(-max(d,0.0)/16.0)*.19*step(0.0,d)*clip;
  float sheen=clamp(.5-local.y/350.0+local.x/650.0,0,1);vec3 material=selected?mix(BLUE,vec3(.88,1.0,.35),sheen*.32):mix(vec3(.036,.041,.040),vec3(.12,.125,.115),sheen*.42);
  col=mix(col,material,fill(d)*clip);col=mix(col,selected?INK:vec3(.75,.76,.69),line(d,selected?1.15:.75)*clip);
  vec2 iconPoint=center+vec2(local.x,local.y)-vec2(0,-15);vec4 pic=appIcon(iconPoint,center,b,90.0,cardInk);col=mix(col,pic.rgb,pic.a*clip);
  float stationY=focusRailY(center.x),bottom=center.y+100.0*scaleCard;col=mix(col,selected?BLUE:WARM,fill(box(p,vec2(center.x,(bottom+stationY)*.5),vec2(.65,max(0.0,(stationY-bottom)*.5)),.3))*.8*clip);col=focusStation(p,col,center.x,selected,clip);
  float width=float(c.atlas[b+25600u]),scale=min(.68,166.0/max(width,1.0));col=mix(col,cardInk,focusLabel(center+local,center+vec2(-width*scale*.5,62),b,scale)*clip);
  if(picker&&(c.bagMeta.z&(1u<<actual))!=0u){vec2 t=center+mix(vec2(74,-77),vec2(81.4,-84.7),bvec2(selected));float mark=min(stroke(p,t+vec2(-5,0),t+vec2(-1,4)),stroke(p,t+vec2(-1,4),t+vec2(7,-5)));col=mix(col,selected?BG:BLUE,fill(mark-1.2)*clip);}
 }
 if(realCount>0u){
  float prev=min(stroke(p,FOCUS_PREV+vec2(8,-16),FOCUS_PREV+vec2(-6,0)),stroke(p,FOCUS_PREV+vec2(-6,0),FOCUS_PREV+vec2(8,16)));
  float next=min(stroke(p,FOCUS_NEXT+vec2(-8,-16),FOCUS_NEXT+vec2(6,0)),stroke(p,FOCUS_NEXT+vec2(6,0),FOCUS_NEXT+vec2(-8,16)));col=mix(col,INK,fill(min(prev,next)-1.1));
  col=mix(col,MUTED,focusCounter(p,vec2(1744,858),add?0u:ordinal+1u,realCount));
  if(p.y>886.0&&p.y<916.0&&p.x>1400.0){uint tickCount=min(realCount,16u),selectedTick=min(ordinal,tickCount-1u);float start=1850.0-(float(tickCount-1u)*22.0+36.0);for(uint i=0u;i<tickCount;i++){bool on=i==selectedTick;float x=start+float(i)*22.0+(i>selectedTick?30.0:0.0)+(on?18.0:3.0);float d=on?box(p,vec2(x,901),vec2(18,3),3):length(p-vec2(x,901))-3.0;col=mix(col,on?BLUE:vec3(.29),fill(d));}}
 }

 loadProgressDraw(p,col,vec2(80,835),vec2(730,128));
 menuFooter(p,col);if(c.rows[1]!=0u)col=focusSearchDraw(p,col);col=menuRemovalDraw(p,col,1008.0);outColor=vec4(rsDraw(p,notification(p,col))*(1.0-c.transition.x),1);
}
