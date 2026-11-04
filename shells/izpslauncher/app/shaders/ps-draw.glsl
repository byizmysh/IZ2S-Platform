// IZPSLauncher: blue wave shell. All coordinates use the native 1920x1080 canvas.
#include "ps-text.glsl"
const vec3 PS_WHITE=vec3(.96,.98,1),PS_MUTED=vec3(.65,.78,.96),PS_CYAN=vec3(.16,.95,1);
const vec3 PS_LIME=vec3(.76,1,.30);
#include "ps-brand.glsl"
#include "ps-navigation-icons.glsl"
#include "ps-mascot.glsl"
float psTypePixel(ivec2 q,ivec4 rect){if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,rect.zw)))return 0.0;uint n=uint(q.y+rect.y)*1024u+uint(q.x);return float((c.atlas[98304u+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;}
float psText(vec2 p,vec2 origin,ivec4 rect,float scale){vec2 q=(p-origin)/scale;ivec2 at=ivec2(floor(q));vec2 f=fract(q);return mix(mix(psTypePixel(at,rect),psTypePixel(at+ivec2(1,0),rect),f.x),mix(psTypePixel(at+ivec2(0,1),rect),psTypePixel(at+ivec2(1),rect),f.x),f.y);}
void psLabel(vec2 p,inout vec3 col,vec2 pos,ivec4 label,vec3 ink){col=mix(col,ink,psText(p,pos,label,1));}
void psPanel(vec2 p,inout vec3 col,vec2 center,vec2 size,float radius,float opacity){float d=box(p,center,size,radius);col=mix(col,vec3(.28,.46,.83),fill(d)*opacity);col=mix(col,vec3(.67,.84,1),line(d,.5)*.16);}
#include "ps-activity.glsl"
vec3 psBackground(vec2 p){
 vec2 uv=p/vec2(1920,1080);float t=c.motion.x*.06;
 vec3 col=mix(vec3(.012,.24,.70),vec3(.015,.08,.32),uv.y);
 col+=vec3(.015,.16,.29)*exp(-length((uv-vec2(.28,.25))*vec2(1.2,2.0))*2.5);
 float wave=.63+.17*sin(uv.x*4.3+t)-uv.x*.12;
 float w=uv.y-wave;col=mix(col,vec3(.16,.035,.40),smoothstep(-.03,.18,w)*.62);
 float wave2=.85+.25*sin(uv.x*4.1+1.5+t*.8);float w2=uv.y-wave2;
 col+=vec3(.01,.17,.38)*exp(-abs(w2)/.085);
 col+=vec3(.04,.11,.25)*exp(-abs(w)/.1)*.6;
 col+=vec3(.08,.16,.26)*exp(-abs(w+.1)/.035)*.17;
 col*=1.0-.16*length((uv-.5)*vec2(1,.6));return col;
}
float psSymbols(vec2 p,vec2 center,float s){
 vec2 q=(p-center)/s;float m=line(box(q,vec2(-.48,-.4),vec2(.22),0)*s,.025*s);
 vec2 tr=q-vec2(.42,-.4);m=max(m,line((max(abs(tr.x)*.866-tr.y*.5,tr.y)-.22)*s,.025*s));
 m=max(m,line((length(q-vec2(.43,.4))-.25)*s,.025*s));vec2 x=q-vec2(-.48,.4);
 m=max(m,fill(min(stroke(x,vec2(-.2,-.2),vec2(.2,.2)),stroke(x,vec2(-.2,.2),vec2(.2,-.2)))*s-.025*s));return m;
}
float psPad(vec2 p,vec2 center,float scale){vec2 q=(p-center)/scale;float m=fill(box(q,vec2(0,0),vec2(15,8),6));m=max(m,fill(box(q,vec2(-12,6),vec2(5,7),3)));m=max(m,fill(box(q,vec2(12,6),vec2(5,7),3)));return m;}
vec3 psCoverPixel(ivec2 q,uint base){q=clamp(q,ivec2(0),ivec2(127,191));uint n=uint(q.y*128+q.x),v=(c.atlas[base+(n>>1u)]>>((n&1u)*16u))&65535u;return vec3(float((v>>11u)&31u)/31.0,float((v>>5u)&63u)/63.0,float(v&31u)/31.0);}
vec3 psCover(vec2 q,uint base){vec2 v=q*vec2(128,192)-.5;ivec2 xy=ivec2(floor(v));vec2 f=fract(v);return mix(mix(psCoverPixel(xy,base),psCoverPixel(xy+ivec2(1,0),base),f.x),mix(psCoverPixel(xy+ivec2(0,1),base),psCoverPixel(xy+ivec2(1),base),f.x),f.y);}
void psButton(vec2 p,inout vec3 col,vec2 center,ivec4 label){
 float d=box(p,center,vec2(128,28),14);col=mix(col,PS_WHITE,fill(d)*.94);
 vec2 key=center-vec2(96,0);col=mix(col,vec3(.04,.20,.51),line(length(p-key)-11.0,1));
 col=mix(col,vec3(.04,.20,.51),text(p,key-vec2(TEXT_BUTTON_A.zw)*.5,TEXT_BUTTON_A));
 col=mix(col,vec3(.04,.20,.51),psText(p,center+vec2(-71,-float(label.w)*.5),label,1));
}
ivec4 psDigit(uint n){if(n==0u)return PS_DIGIT0;if(n==1u)return PS_DIGIT1;if(n==2u)return PS_DIGIT2;if(n==3u)return PS_DIGIT3;if(n==4u)return PS_DIGIT4;if(n==5u)return PS_DIGIT5;if(n==6u)return PS_DIGIT6;if(n==7u)return PS_DIGIT7;if(n==8u)return PS_DIGIT8;return PS_DIGIT9;}
float psAppTitlePixel(ivec2 q,uint base){if(any(lessThan(q,ivec2(0)))||q.x>=int(c.atlas[base+25625u])||q.y>=int(c.atlas[base+25626u]))return 0.0;uint n=uint(q.y)*512u+uint(q.x);return float((c.atlas[base+30720u+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;}
float psAppTitle(vec2 p,vec2 origin,uint base,float scale){vec2 q=(p-origin)/scale;ivec2 at=ivec2(floor(q));vec2 f=fract(q);return mix(mix(psAppTitlePixel(at,base),psAppTitlePixel(at+ivec2(1,0),base),f.x),mix(psAppTitlePixel(at+ivec2(0,1),base),psAppTitlePixel(at+ivec2(1),base),f.x),f.y);}
float psCoverTitlePixel(ivec2 q,uint base){if(any(lessThan(q,ivec2(0)))||q.x>=256||q.y>=64)return 0.0;uint n=uint(q.y*256+q.x);return float((c.atlas[base+29696u+(n>>4u)]>>((n&15u)*2u))&3u)/3.0;}
float psCoverTitle(vec2 p,vec2 origin,uint base){vec2 q=(p-origin)/.8;ivec2 at=ivec2(floor(q));vec2 f=fract(q);return mix(mix(psCoverTitlePixel(at,base),psCoverTitlePixel(at+ivec2(1,0),base),f.x),mix(psCoverTitlePixel(at+ivec2(0,1),base),psCoverTitlePixel(at+ivec2(1),base),f.x),f.y);}
void psHeader(vec2 p,inout vec3 col){
 // Brand mark and wordmark share the header's optical center.
 col=psBrandMark(p,col,vec2(64,62));psLabel(p,col,vec2(136,66),PS_BRAND,PS_WHITE);
 psLabel(p,col,vec2(136+float(PS_BRAND.z-PS_SIGNATURE.z),102),PS_SIGNATURE,PS_MUTED);
 // Detailed IZ mouse walks between the brand and compact navigation.
 float t=c.motion.x,phase=mod(t,18.0)*.34906585,walk=.5-.5*cos(phase),direction=sin(phase)>=0.0?1.0:-1.0;
 float mx=mix(PS_MOUSE_LEFT,PS_MOUSE_RIGHT,walk),ground=110.0,bob=1.5*sin(t*10.0)*min(1.0,abs(sin(phase))*4.0);
 vec2 mq=vec2((p.x-mx)*direction,p.y-ground-bob)/.70+vec2(116,110);
 if(mq.x>=5.0&&mq.x<=198.0&&mq.y>=18.0&&mq.y<=120.0)col=psMouseDraw(mq,col,t);
 uint clockBase=81920u;
 if(c.atlas[clockBase]==0x5053544du){uint hour=c.atlas[clockBase+1u],minute=c.atlas[clockBase+2u];
  for(uint digit=0u;digit<4u;digit++){uint n=digit==0u?hour/10u:digit==1u?hour%10u:digit==2u?minute/10u:minute%10u;float x=PS_CLOCK_ORIGIN.x+float(digit)*24.0+(digit>=2u?13.0:0.0);col=mix(col,PS_WHITE,psText(p,vec2(x,PS_CLOCK_ORIGIN.y),psDigit(n),1));}
  col=mix(col,PS_WHITE,fill(min(length(p-vec2(PS_HEADER_RIGHT-54.0,82)),length(p-vec2(PS_HEADER_RIGHT-54.0,94)))-1.4));
 }else psLabel(p,col,PS_CLOCK_ORIGIN,PS_CLOCK,PS_WHITE);
 ivec4 labels[3]=ivec4[3](PS_ALL,PS_GAMES,PS_TOOLS);
 for(uint i=0u;i<3u;i++){bool tabOn=c.rows[0]==i;float d=box(p,psCategoryCenter(i),psCategoryHalf(i),20);col=mix(col,PS_WHITE,fill(d)*(tabOn?.17:.0));col=mix(col,tabOn?PS_WHITE:PS_MUTED,psText(p,psCategoryCenter(i)-vec2(labels[i].zw)*.5,labels[i],1));}
 // Search uses the same 40px height and optical center as the category tabs.
 float searchBox=box(p,PS_SEARCH_CENTER,vec2(68,20),20);col=mix(col,PS_WHITE,fill(searchBox)*.065);col=mix(col,PS_WHITE,line(searchBox,.6)*.12);
 float searchTextX=PS_SEARCH_CENTER.x-(float(PS_SEARCH.z)+30.0)*.5+30.0;vec2 lens=vec2(searchTextX-23.0,PS_HEADER_Y-2.0);col=mix(col,PS_MUTED,line(length(p-lens)-7.0,1));col=mix(col,PS_MUTED,fill(stroke(p,lens+vec2(5,5),lens+vec2(11,11))-1));
 psLabel(p,col,vec2(searchTextX,PS_HEADER_Y-float(PS_SEARCH.w)*.5),PS_SEARCH,PS_MUTED);

}
vec3 psMenuContent(vec2 p,vec3 col){
 bool picker=c.piece.x!=0;uint installed=min(c.library.x,15u),count=focusCount(picker),chosen=picker?uint(max(c.piece.y,0)):c.state.x;
 uint ordinal=c.rows[19]==0x464f4355u?min(c.rows[11],count):chosen;bool home=!picker&&(chosen>=installed||ordinal>=count);uint selectedBase=slotBase(picker?16u+min(chosen,15u):min(chosen,14u));
 // Exchange tile at the left of the horizontal rail.
 float store=box(p,ADD_CENTER,PS_EXCHANGE_HALF,7);col=mix(col,vec3(.19,.13,.55),fill(store));col=mix(col,vec3(.6,.67,1),line(store,.5)*.2);
 col=psExchangeGlyph(p,col,ADD_CENTER+vec2(0,-24));psLabel(p,col,ADD_CENTER+vec2(-68,88),PS_EXCHANGE,PS_WHITE);

 // What's New stays in the same place as the reference's large focus card.
 vec2 nc=HERO_CENTER;vec2 nh=PS_CARD_HALF;float nd=box(p,nc,nh,8);
 if(home)col+=PS_CYAN*exp(-abs(nd)/15.0)*.29;
 col=mix(col,vec3(.18,.13,.53)+vec3(.065,.03,.13)*clamp((530.0-p.y)/360.0,0,1),fill(nd));
 col=mix(col,home?PS_CYAN:vec3(.53,.55,.87),line(nd,home?2.7:.5)*(home?1.0:.3));
 col=psNewsGlyph(p,col,nc);

 // Portrait covers use real package icons and titles, rasterized by the host.
 int first=int(floor(c.flow.x));for(int i=-1;i<7;i++){
  int index=first+i;if(index<0||index>=int(count))continue;vec2 center=focusCardCenter(float(index));if(center.x<500.0||center.x>2040.0)continue;
  uint actual=focusIndex(uint(index)),base=slotBase(actual+(picker?16u:0u));bool on=!home&&actual==chosen;
  vec2 halfSize=PS_CARD_HALF;float d=box(p,center,halfSize,7);
  if(on)col+=PS_CYAN*exp(-abs(d)/14)*.28;
  if(d<1.0){vec2 uv=(p-center+halfSize)/(halfSize*2.0);vec3 cover=psCover(uv,base);if(c.atlas[base+25624u]!=0x50534356u){cover=vec3(.07,.16,.31);vec4 ic=focusIcon(p,center,base,170);cover=mix(cover,ic.rgb,ic.a);}col=mix(col,cover,fill(d));}
  col=mix(col,PS_WHITE,psCoverTitle(p,center+vec2(-88,96),base));
  col=mix(col,on?PS_CYAN:vec3(.58,.69,.9),line(d,on?2.7:.5)*(on?1.0:.25));
  if(picker&&(c.bagMeta.z&(1u<<actual))!=0u){vec2 badge=center+vec2(halfSize.x-22,-halfSize.y+24);col=mix(col,PS_CYAN,fill(length(p-badge)-12));float mark=min(stroke(p,badge-vec2(6,0),badge+vec2(-1,5)),stroke(p,badge+vec2(-1,5),badge+vec2(7,-5)));col=mix(col,vec3(.03,.14,.38),fill(mark-1.4));}
 }
 // Every selection uses the same left edge, text baselines and action boundary.
 if(home)psLabel(p,col,vec2(PS_LEFT,572),PS_NEWS,PS_WHITE);
 else if(count>0u)col=mix(col,PS_WHITE,psAppTitle(p,vec2(PS_LEFT,572),selectedBase,1.2));
 else psLabel(p,col,vec2(PS_LEFT,572),PS_NORESULT,PS_WHITE);
 psLabel(p,col,vec2(PS_LEFT,624),home?PS_DISCOVERSUB:count>0u?PS_READY:PS_SEARCHHELP,PS_MUTED);
 if(home||count>0u)psButton(p,col,PS_ACTION,home?PS_BROWSE:picker?PS_INSTALL:PS_OPEN);

 // Aligned two-column section. Labels, top edges and bottom edges share one grid.
 psActivityPanel(p,col);
 psLabel(p,col,vec2(PS_SIDE_LEFT,688),PS_COLLECTION,PS_MUTED);
 for(uint i=0u;i<2u;i++){
  vec2 center=psCollectionCenter(i);bool on=count>i&&!home&&focusIndex(i)==chosen;psPanel(p,col,center,PS_COLLECTION_HALF,16,on?.34:.24);
  if(on)col=mix(col,PS_CYAN,line(box(p,center,PS_COLLECTION_HALF,16),1.4));
  if(count>i){uint base=slotBase(focusIndex(i)+(picker?16u:0u));vec4 pic=focusIcon(p,vec2(1310,center.y),base,60);col=mix(col,pic.rgb,pic.a);col=mix(col,PS_WHITE,psAppTitle(p,vec2(1372,center.y-24.0),base,.8));psLabel(p,col,vec2(1372,center.y+14.0),PS_READY,PS_MUTED);}
  else col=mix(col,PS_MUTED,psText(p,vec2(1280,center.y-8),PS_EMPTY,.7));
 }

 return col;
}
void psFooter(vec2 p,inout vec3 col){
 col=mix(col,PS_WHITE,fill(box(p,vec2(960,1024),vec2(928,.4),0))*.14);
 ivec4 hints[5]=ivec4[5](PS_SELECT,PS_BACK,PS_LIBHINT,PS_FIND,PS_FILTER);float cursor=PS_EDGE+10.0;
 for(int i=0;i<5;i++){vec2 key=vec2(cursor,1056);col=mix(col,PS_MUTED,line(length(p-key)-10.0,.9));uint ch=i==0?65u:i==1?66u:i==2?88u:i==3?89u:82u;col=mix(col,PS_WHITE,focusAscii((p-key)/.48,vec2(-9,-10),ch));col=mix(col,PS_MUTED,psText(p,key+vec2(18,-float(hints[i].w)*.5),hints[i],1));cursor+=18.0+float(hints[i].z)+34.0;}
 if(psScrollMax()>0.0){vec2 at=vec2(cursor,1056);float arrow=min(stroke(p,at+vec2(-5,-2),at+vec2(0,3)),stroke(p,at+vec2(0,3),at+vec2(5,-2)));col=mix(col,PS_MUTED,fill(arrow-.9));psLabel(p,col,vec2(cursor+18.0,1050),PS_UPDOWN,PS_MUTED);}
 psLabel(p,col,vec2(PS_HEADER_RIGHT-float(PS_FOOTER.z),1049),PS_FOOTER,PS_WHITE);

}
vec3 psMenu(vec2 p){
 vec3 col=psBackground(p);float scroll=clamp(uintBitsToFloat(c.rows[13]),0.0,psScrollMax());
 if(psContentHit(p)){vec3 content=psMenuContent(p+vec2(0,scroll),col);col=mix(col,content,scroll>1.0?smoothstep(PS_CONTENT_TOP,218.0,p.y):smoothstep(PS_CONTENT_TOP,194.0,p.y));}
 psHeader(p,col);psFooter(p,col);
 // Thin scroll track and a low-key down cue, both anchored to the screen.
 if(psScrollMax()>0.0){
  float bottom=psContentBottom(),trackTop=667.0,trackBottom=bottom-19.0,trackHalf=(trackBottom-trackTop)*.5;
  float track=box(p,vec2(1880,(trackTop+trackBottom)*.5),vec2(1,trackHalf),1);col=mix(col,PS_WHITE,fill(track)*.1);
  float thumbHalf=min(55.0,trackHalf*.34),thumbY=mix(trackTop+thumbHalf,trackBottom-thumbHalf,scroll/psScrollMax());
  float thumb=box(p,vec2(1880,thumbY),vec2(2,thumbHalf),2);col=mix(col,PS_CYAN,fill(thumb)*.65);
  if(scroll<10.0)col=mix(col,PS_MUTED,psText(p,vec2(PS_RIGHT-float(PS_SCROLL.z),bottom-26.0),PS_SCROLL,.9));
 }
 return col;
}
