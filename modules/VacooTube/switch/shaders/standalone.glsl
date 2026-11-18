// Shared standalone overlay. The compiled modules keep their own copy.
const uint MediaOverlay=25u,MediaSelection=39u,MediaFit=108u,MediaHud=109u,MediaRestart=107u;
const uint MediaExitPending=110u;
const float RowLeft=76.0,RowRight=636.0,RowHalfHeight=33.0;
const vec2 CloseCenter=vec2(602,134);

float mediaRowY(uint i){return 364.0+float(i)*80.0;}

#ifndef HB_READONLY
void mediaInput(uint pressed){
    uint panel=c.atlas[SB+MediaOverlay],choice=c.atlas[SB+MediaSelection];
    const uint count=5u;
    bool held=c.touch[2]==1u&&(c.touch[6]&2u)==0u,tap=held&&c.atlas[CB+2u]==0u;
    vec2 p=hbTouch(0u)*1.5;
    bool closeTap=tap&&all(lessThan(abs(p-CloseCenter),vec2(25)));
    // Release the activating input before handing control back to IZ2S.
    if(c.atlas[SB+MediaExitPending]!=0u){
        if(hbButtons()==0u&&hbTouchCount()==0u){
            c.atlas[SB+MediaExitPending]=0u;
            hbExit();
        }
    }else if((pressed&HB_MINUS)!=0u||(panel!=0u&&closeTap)){
        panel=panel==0u?1u:0u;
        choice=0u;
    }else if(panel!=0u){
        if((pressed&HB_B)!=0u){
            panel=0u;
            choice=0u;
        }else{
            if((pressed&HB_UP)!=0u)choice=(choice+count-1u)%count;
            if((pressed&HB_DOWN)!=0u)choice=(choice+1u)%count;
            bool picked=false;
            if(tap&&p.x>=RowLeft&&p.x<=RowRight){
                for(uint i=0u;i<count;i++){
                    if(abs(p.y-mediaRowY(i))<=RowHalfHeight){choice=i;picked=true;}
                }
            }
            if((pressed&HB_A)!=0u||picked){
                if(choice==0u)panel=0u;
                else if(choice==1u)c.atlas[SB+MediaFit]=(c.atlas[SB+MediaFit]+1u)%3u;
                else if(choice==2u)c.atlas[SB+MediaHud]^=1u;
                else if(choice==3u)c.atlas[SB+MediaRestart]=1u;
                else c.atlas[SB+MediaExitPending]=1u;
            }
        }
    }
    c.atlas[SB+MediaOverlay]=panel;
    c.atlas[SB+MediaSelection]=choice;
    c.atlas[CB+2u]=held?1u:0u;
    // This standalone app always reserves Minus for the overlay.
    c.atlas[SB+24u]&=0x1f00ffffu;
    hbEndFrame();
}
#else
#include "overlay-font.glsl"

const vec3 MediaBase=vec3(.037,.052,.085),MediaText=vec3(.944,.966,.994);
const vec3 MediaMuted=vec3(.66,.72,.81),MediaDanger=vec3(1.,.62,.64);
#ifdef VACOO_THEME
const vec3 MediaAccent=vec3(1.,.66,.65),MediaGlow=vec3(.42,.14,.24);
#else
const vec3 MediaAccent=vec3(.46,.97,.84),MediaGlow=vec3(.067,.30,.33);
#endif

float textWidth(uint label){return float(MediaTextBoxes[label].y);}
float mediaText(vec2 p,vec2 origin,uint label){
    uvec3 item=MediaTextBoxes[label];
    ivec2 q=ivec2(floor(p-origin));
    if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,ivec2(item.yz))))return 0.;
    uint at=item.x+uint(q.y)*item.y+uint(q.x);
    return float((c.atlas[at>>2u]>>((at&3u)*8u))&255u)/255.;
}

float mediaLine(vec2 p,vec2 a,vec2 b,float width){
    vec2 d=b-a;
    return fill(length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.,1.))-width);
}

float mediaIcon(vec2 p,uint icon){
    if(length(p)>27.)return 0.;
    if(icon==0u){
        return fill(max(abs(p.y)*1.7+p.x-15.,-p.x-10.));
    }
    if(icon==1u){
        float frame=line(box(p,vec2(0),vec2(15,11),3.),1.4);
        return max(frame,mediaLine(p,vec2(-7,6),vec2(7,-6),1.35));
    }
    if(icon==2u){
        float a=fill(box(p,vec2(-10,5),vec2(1.8,6),1.6));
        float b=fill(box(p,vec2(0,1),vec2(1.8,10),1.6));
        return max(max(a,b),fill(box(p,vec2(10,-4),vec2(1.8,15),1.6)));
    }
    if(icon==3u){
        float arc=line(length(p)-12.,1.5)*(1.-step(4.,p.x)*step(0.,-p.y));
        float arrow=max(mediaLine(p,vec2(7,-2),vec2(14,-2),1.45),
                        mediaLine(p,vec2(14,-11),vec2(14,-2),1.45));
        return max(arc,arrow);
    }
    if(icon==4u){
        float door=line(box(p+vec2(5,0),vec2(0),vec2(10,13),2.),1.4)*(1.-step(0.,p.x)*step(abs(p.y),7.));
        float arrow=max(mediaLine(p,vec2(-3,0),vec2(15,0),1.5),
                        mediaLine(p,vec2(9,sign(p.y)*6.),vec2(15,0),1.5));
        return max(door,arrow);
    }
    return max(mediaLine(p,vec2(-7,-7),vec2(7,7),1.6),mediaLine(p,vec2(-7,7),vec2(7,-7),1.6));
}

vec3 mediaKey(vec3 color,vec2 p,vec2 center,uint button){
    float key=box(p,center,vec2(15),7.);
    color=mix(color,vec3(.15,.18,.24),fill(key));
    color=mix(color,MediaMuted,line(key,.8)*.58);
    float glyph=mediaText(p,vec2(center.x-textWidth(button)*.5,center.y-10.),button);
    return mix(color,MediaText,glyph);
}

vec3 mediaHint(vec3 color,vec2 p,vec2 center,uint button,uint label){
    color=mediaKey(color,p,center,button);
    return mix(color,MediaMuted,mediaText(p,center+vec2(24,-11),label));
}

vec3 mediaBackdrop(vec2 p){
    vec2 q=(p-vec2(1296,520))/vec2(850,780);
    vec3 color=mix(MediaBase,MediaGlow,exp(-dot(q,q)*1.6));
    vec2 center=vec2(1360,520);
    float distance=length((p-center)*vec2(1.,1.06));
    float rings=line(mod(distance+42.,84.)-42.,1.);
    color=mix(color,MediaAccent,rings*.15*exp(-distance/1000.));
    float field=box(p,center,vec2(158,105),37.);
    color=mix(color,mix(MediaBase,MediaGlow,.48),fill(field)*.95);
    color=mix(color,MediaAccent,line(field,1.4)*.64);
    color=mix(color,MediaAccent,mediaIcon((p-center)/2.85,0u)*.9);
    return color;
}

bool mediaReady(){
    return c.atlas[SB+41u]==1u&&c.atlas[SB+3u]!=0u&&c.atlas[SB+4u]>0u&&c.atlas[SB+5u]>0u;
}

vec3 mediaWaiting(vec2 p){
    vec3 color=mediaBackdrop(p);
    color=mix(color,MediaAccent,mediaText(p,vec2(113,340),TextBrand));
    color=mix(color,MediaText,mediaText(p,vec2(109,418),TextWaitingTitle));
    color=mix(color,MediaMuted,mediaText(p,vec2(113,514),TextStartHelp));
    color=mix(color,MediaMuted,mediaText(p,vec2(113,560),TextFirstFrameHelp));
    return mediaHint(color,p,vec2(130,674),TextButtonMinus,TextOpenHint);
}

vec3 mediaOverlay(vec3 color,vec2 p,uint selected,bool ready){
    const float bottom=970.;
    vec2 center=vec2(356,(bottom+56.)*.5),extent=vec2(316,(bottom-56.)*.5);
    float rail=box(p,center,extent,32.);
    color*=mix(.50,.92,smoothstep(655.,1660.,p.x));
    color*=1.-.38*exp(-max(rail,0.)/29.);
    if(rail>2.)return color;
    vec3 surface=mix(MediaBase,MediaGlow,.12+.18*exp(-max(p.y-56.,0.)/188.));
    color=mix(color,surface,fill(rail));
    color=mix(color,MediaAccent,line(rail,1.1)*.40);
    float mark=box(p,vec2(102,132),vec2(26),13.);
    color=mix(color,mix(surface,MediaAccent,.19),fill(mark));
    color=mix(color,MediaAccent,mediaIcon(p-vec2(101,132),0u));
    color=mix(color,MediaText,mediaText(p,vec2(144,119),TextBrand));
    float close=box(p,CloseCenter,vec2(21),10.);
    color=mix(color,vec3(.15,.18,.24),fill(close));
    color=mix(color,MediaMuted,mediaIcon(p-CloseCenter,5u));
    color=mix(color,MediaText,mediaText(p,vec2(76,185),TextPlayback));

    uint message=ready?TextLive:TextWaiting;
    vec3 status=ready?MediaAccent:vec3(.99,.80,.52);
    float chipWidth=textWidth(message)+52.;
    float chip=box(p,vec2(RowLeft+chipWidth*.5,286),vec2(chipWidth*.5,21),18.);
    color=mix(color,mix(surface,status,.14),fill(chip));
    color=mix(color,status,fill(length(p-vec2(95,286))-4.5));
    color=mix(color,status,mediaText(p,vec2(110,275),message));

    const uint count=5u;
    for(uint i=0u;i<count;i++){
        float y=mediaRowY(i);
        if(abs(p.y-y)>RowHalfHeight+2.)continue;
        bool focus=i==selected,danger=i==4u;
        vec3 accent=danger?MediaDanger:MediaAccent;
        float tile=box(p,vec2(center.x,y),vec2((RowRight-RowLeft)*.5,RowHalfHeight),15.);
        vec3 tileColor=focus?mix(surface,accent,.22):mix(surface,vec3(.17,.21,.28),.44);
        color=mix(color,tileColor,fill(tile));
        color=mix(color,focus?accent:vec3(.27,.32,.41),line(tile,focus?1.7:.8)*(focus?.89:.54));
        if(focus)color=mix(color,accent,fill(box(p,vec2(78,y),vec2(3,16),2.)));
        float iconPlate=box(p,vec2(113,y),vec2(19),8.);
        color=mix(color,focus?accent:mix(tileColor,accent,.15),fill(iconPlate));
        uint icon=i;
        color=mix(color,focus?MediaBase:accent,mediaIcon(p-vec2(113,y),icon));
        uint label=i==4u?TextExit:TextResume+i;
        color=mix(color,danger?MediaDanger:MediaText,mediaText(p,vec2(151,y-15.),label));

        if(i==1u||i==2u){
            uint value=i==1u?TextFit+c.atlas[SB+MediaFit]%3u:(c.atlas[SB+MediaHud]==0u?TextOff:TextOn);
            float width=textWidth(value);
            float badge=box(p,vec2(600.-width*.5,y),vec2(width*.5+13.,17.),8.);
            color=mix(color,MediaBase,fill(badge)*.89);
            color=mix(color,focus?accent:MediaMuted,mediaText(p,vec2(600.-width,y-11.),value));
        }else if(focus){
            float chevron=max(mediaLine(p,vec2(599,y-7.),vec2(606,y),1.5),
                              mediaLine(p,vec2(599,y+7.),vec2(606,y),1.5));
            color=mix(color,accent,chevron);
        }
    }

    float footer=bottom-62.;
    color=mix(color,MediaMuted,fill(box(p,vec2(center.x,footer-52.),vec2(280,.7),.7))*.27);
    color=mediaHint(color,p,vec2(94,footer),TextButtonA,TextSelectHint);
    color=mediaHint(color,p,vec2(228,footer),TextButtonDpad,TextNavigateHint);
    color=mediaHint(color,p,vec2(356,footer),TextButtonB,TextCloseHint);
    return mediaHint(color,p,vec2(490,footer),TextButtonMinus,TextCloseHint);
}

vec3 mediaScreen(vec2 p){
    bool ready=mediaReady();
    uint panel=c.atlas[SB+MediaOverlay],selected=c.atlas[SB+MediaSelection];
    vec3 color=vec3(.016,.026,.037);
    if(ready){
        ivec2 size=ivec2(c.atlas[SB+4u],c.atlas[SB+5u]);
        vec2 ratio=vec2(1920,1080)/vec2(size);
        uint view=c.atlas[SB+MediaFit];
        float scale=view==1u?max(ratio.x,ratio.y):view==2u?1.0:min(ratio.x,ratio.y);
        vec2 extent=vec2(size)*scale,origin=(vec2(1920,1080)-extent)*.5;
        if(all(greaterThanEqual(p,origin))&&all(lessThan(p,origin+extent))){
            vec2 uv=(p-origin)/scale-.5;
            ivec2 q=ivec2(floor(uv));
            color=scale==1.0?streamPixel(ivec2(p-origin),size):streamBilinear(q,size,fract(uv));
        }
        if(panel==0u&&c.atlas[SB+MediaHud]!=0u)color=streamHudValues(color,p);
    }else{
        color=panel==0u?mediaWaiting(p):mediaBackdrop(p);
    }
    return panel==0u?color:mediaOverlay(color,p,selected,ready);
}
#endif
