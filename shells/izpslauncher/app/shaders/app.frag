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
#include "load-progress.glsl"
#include "focus-layout.glsl"
#include "focus-library.glsl"
#include "focus-ui.glsl"
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
#include "ps-draw.glsl"
#include "ps-removal.glsl"
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
 vec3 col=psMenu(p);if(c.rows[1]!=0u)col=focusSearchDraw(p,col);col=psRemovalDraw(p,col);col=rsDraw(p,notification(p,col));outColor=vec4(col*(1.0-c.transition.x),1);
}
