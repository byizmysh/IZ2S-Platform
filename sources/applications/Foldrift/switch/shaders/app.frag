#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#include "layout.glsl"

const vec3 Base=vec3(.050,.074,.105), Surface=vec3(.083,.115,.155);
const vec3 RowDark=vec3(.095,.132,.177), Outline=vec3(.19,.25,.32);
const vec3 Ink=vec3(.91,.95,.99), Muted=vec3(.58,.69,.79);
const vec3 Mint=vec3(.32,.91,.76), Amber=vec3(1,.74,.43), Error=vec3(1,.57,.53);

float distanceBox(vec2 p,vec2 top,vec2 size,float radius){
    vec2 q=abs(p-(top+size*.5))-(size*.5-radius);
    return length(max(q,0.0))+min(max(q.x,q.y),0.0)-radius;
}
float box(vec2 p,vec2 top,vec2 size,float radius){
    return 1.0-smoothstep(-.75,.75,distanceBox(p,top,size,radius));
}
float rim(vec2 p,vec2 top,vec2 size,float radius){
    return 1.0-smoothstep(.3,1.7,abs(distanceBox(p,top,size,radius)));
}
bool inside(vec2 p,vec2 top,vec2 size){
    return all(greaterThanEqual(p,top))&&all(lessThan(p,top+size));
}
uint byteAt(uint byte){
    return (c.atlas[byte/4u]>>((byte%4u)*8u))&255u;
}
float glyph(vec2 q,uint ch){
    if(ch<32u||ch==127u||ch>=224u||any(lessThan(q,vec2(0)))||q.x>=10.0||q.y>=20.0)return 0.0;
    uint unit=ch<127u?ch-32u:95u+ch-128u;
    uint x=(unit%16u)*10u+uint(q.x),y=(unit/16u)*20u+uint(q.y);
    return float(byteAt(c.atlas[2]+y*160u+x))/255.0;
}
float text(vec2 p,vec2 top,uint offset,uint cap,float scale,bool truncate){
    vec2 q=(p-top)/scale;
    if(q.x<0.0||q.y<0.0||q.y>=20.0||q.x>=float(cap)*10.0)return 0.0;
    uint index=uint(q.x)/10u;
    uint ch=byteAt(offset+index);
    if(truncate&&index>=cap-3u&&byteAt(offset+cap)!=0u)ch=46u;
    return glyph(vec2(mod(q.x,10.0),q.y),ch);
}
float literal(vec2 p,vec2 top,uint label,float scale){
    uint slot=4u+label*2u;
    return text(p,top,c.atlas[1]+c.atlas[slot],c.atlas[slot+1u],scale,false);
}
float viewText(vec2 p,vec2 top,uint offset,uint cap,float scale){
    return text(p,top,Deck*4u+offset,cap,scale,true);
}

float number(vec2 p,vec2 top,uint value,float scale){
    vec2 q=(p-top)/scale;
    if(q.x<0.0||q.x>=40.0||q.y<0.0||q.y>=20.0)return 0.0;
    uint digits=value>=1000u?4u:value>=100u?3u:value>=10u?2u:1u;
    uint index=uint(q.x)/10u;
    if(index>=digits)return 0.0;
    const uint places[4]=uint[4](1u,10u,100u,1000u);
    uint ch=48u+(value/places[digits-1u-index])%10u;
    return glyph(vec2(mod(q.x,10.0),q.y),ch);
}

float folder(vec2 p,vec2 top){
    return max(box(p,top,vec2(26,18),4),box(p,top+vec2(2,-4),vec2(11,8),3));
}

vec3 panel(vec2 p,vec3 col){
    vec2 top=FilePanelTop, size=FilePanelSize;
    if(!inside(p,top-vec2(2),size+vec2(4)))return col;
    uint pane=PaneStart;
    col=mix(col,Surface,box(p,top,size,16));
    col=mix(col,Outline,rim(p,top,size,16)*.9);
    col=mix(col,Mint,literal(p,top+vec2(24,19),2u,1.15));
    col=mix(col,Ink,viewText(p,top+vec2(24,53),pane,117u,1.35));
    uint total=deck(pane+128u);
    if(total>0u){
        col=mix(col,Muted,number(p,top+vec2(1590,19),deck(pane+132u)/FileRowsPerPage+1u,1.1));
        col=mix(col,Muted,glyph((p-top-vec2(1632,19))/1.1,47u));
        col=mix(col,Muted,number(p,top+vec2(1653,19),(total+FileRowsPerPage-1u)/FileRowsPerPage,1.1));
    }
    vec2 head=top+vec2(24,100);
    col=mix(col,Muted,literal(p,head,4u,1.1));
    col=mix(col,Muted,literal(p,head+vec2(1620,0),5u,1.1));
    col=mix(col,Outline,box(p,top+vec2(22,124),vec2(1768,1),0));

    uint shown=min(deck(pane+136u),FileRowsPerPage);
    if(shown==0u){
        col=mix(col,Muted,literal(p,top+vec2(24,162),8u,1.3));
        return col;
    }
    vec2 from=FileRowsTop;
    if(p.y<from.y||p.y>=from.y+float(shown)*FileRowPitch)return col;
    uint shownIndex=uint((p.y-from.y)/FileRowPitch);
    vec2 row=from+vec2(0,FileRowPitch*float(shownIndex));
    uint bytes=pane+RowStart+shownIndex*RowBytes;
    bool focused=deck(pane+140u)==deck(pane+132u)+shownIndex;
    col=mix(col,focused?mix(RowDark,Mint,.16):RowDark,
            box(p,row,vec2(1788,51),7));
    if(focused)col=mix(col,Mint,box(p,row+vec2(0,5),vec2(3,41),1));
    bool directory=deck(bytes)!=0u;
    float type=directory?folder(p,row+vec2(14,17)):box(p,row+vec2(17,15),vec2(18,22),3);
    col=mix(col,directory?Mint:Muted,type*.9);
    col=mix(col,Ink,viewText(p,row+vec2(55,13),bytes+4u,58u,1.4));
    col=mix(col,Muted,viewText(p,row+vec2(1630,15),bytes+64u,13u,1.08));
    return col;
}

vec3 modal(vec2 p,vec3 col){
    uint mode=deck(72u);
    if(mode==ViewBrowse)return col;
    vec2 top=vec2(378,198),size=vec2(1164,704);
    col*=.42;
    col=mix(col,vec3(.085,.125,.17),box(p,top,size,20));
    col=mix(col,Outline,rim(p,top,size,20));
    col=mix(col,mode==ViewConfirm?Amber:Ink,viewText(p,top+vec2(45,32),256u,45u,2.05));
    col=mix(col,Outline,box(p,top+vec2(45,102),vec2(1074,2),1));
    vec2 footer=top+vec2(45,629);
    if(mode==ViewKeyboard){
        col=mix(col,RowDark,box(p,top+vec2(45,122),vec2(1074,58),8));
        col=mix(col,Ink,viewText(p,top+vec2(65,134),384u,61u,1.48));
        for(uint i=0u;i<40u;++i){
            vec2 key=top+vec2(45+float(i%10u)*107.0,210+float(i/10u)*89.0);
            if(!inside(p,key,vec2(91,71)))continue;
            bool current=i==deck(80u);
            col=mix(col,current?Mint:RowDark,box(p,key,vec2(91,71),10));
            uint ch=byteAt(c.atlas[3]+i);
            if(deck(96u)!=0u&&ch>=97u&&ch<=122u)ch-=32u;
            float mask=glyph((p-key-vec2(35,13))/2.2,ch);
            if(ch==32u)
                mask=max(glyph((p-key-vec2(29,18))/1.55,83u),
                         glyph((p-key-vec2(45,18))/1.55,80u));
            col=mix(col,current?Base:Ink,mask);
        }
        col=mix(col,Mint,literal(p,footer,14u,1.45));
    }else if(mode==ViewCopying){
        col=mix(col,Ink,viewText(p,top+vec2(45,141),512u,58u,1.72));
        vec2 track=top+vec2(45,292);
        float amount=clamp(float(deck(88u))*.001,0.0,1.0);
        col=mix(col,RowDark,box(p,track,vec2(1074,24),12));
        if(amount>0.0)col=mix(col,Mint,box(p,track,vec2(max(24.0,1074.0*amount),24),12));
        col=mix(col,Muted,literal(p,footer,15u,1.45));
    }else{
        uint count=min(deck(84u),12u), selected=deck(80u);
        for(uint i=0u;i<count;++i){
            float step=mode==ViewActions?50.0:33.0;
            vec2 line=top+vec2(45,123+step*float(i));
            if(mode==ViewActions&&i==selected)
                col=mix(col,mix(RowDark,Mint,.25),box(p,line-vec2(12,7),vec2(1050,48),8));
            col=mix(col,Ink,viewText(p,line,512u+i*96u,77u,mode==ViewPreview?1.36:1.65));
        }
        if(mode==ViewPreview)
            col=mix(col,Muted,viewText(p,top+vec2(45,559),384u,64u,1.3));
        uint hint=mode==ViewConfirm?12u:mode==ViewPreview?13u:11u;
        col=mix(col,mode==ViewConfirm?Amber:Mint,literal(p,footer,hint,1.45));
    }
    return col;
}

void main(){
    vec2 p=gl_FragCoord.xy;
    vec3 col=mix(Base,vec3(.075,.12,.164),clamp(1.0-p.y/540.0,0.0,1.0)*.8);
    col=mix(col,Mint,box(p,vec2(54,23),vec2(40,40),9));
    col=mix(col,Base,folder(p,vec2(61,34)));
    col=mix(col,Ink,literal(p,vec2(111,26),0u,1.65));
    if(deck(64u)==DeckReady)
        col=mix(col,Muted,literal(p,vec2(280,30),deck(100u)!=0u?17u:1u,1.25));
    if(deck(64u)==DeckReady){
        col=panel(p,col);
        col=modal(p,col);
        col=mix(col,deck(92u)!=0u?Error:Muted,viewText(p,vec2(54,954),128u,107u,1.34));
    }else{
        col=mix(col,Mint,literal(p,FilePanelTop+vec2(24,30),10u,1.8));
    }
    col=mix(col,Outline,box(p,vec2(54,993),vec2(1812,1),0));
    if(deck(72u)==ViewBrowse){
        col=mix(col,Ink,literal(p,vec2(54,1017),6u,1.35));
        col=mix(col,Muted,literal(p,vec2(966,1017),deck(76u)!=0u?3u:7u,1.28));
    }else col=mix(col,Muted,literal(p,vec2(54,1017),16u,1.35));
    outColor=vec4(col,1);
}
