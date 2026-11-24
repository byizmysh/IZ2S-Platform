// Menu-only scratch. Filtering runs once in compute, never per display pixel.
uint focusRawCount(bool picker){return picker?min(c.bagMeta.x,16u):min(c.library.x,15u);}
uint focusCount(bool picker){return c.rows[19]==0x464f4355u?min(c.rows[10],16u):focusRawCount(picker);}
uint focusIndex(uint ordinal){if(c.rows[19]!=0x464f4355u)return ordinal;return (c.rows[8u+ordinal/8u]>>((ordinal%8u)*4u))&15u;}
uint focusOrdinal(uint actual,bool picker){uint count=focusCount(picker);for(uint i=0u;i<count;i++)if(focusIndex(i)==actual)return i;return count;}
uint focusQuery(uint i){return (c.rows[3u+i/4u]>>((i%4u)*8u))&255u;}
vec2 focusKeyCenter(uint i){return vec2(610.0+float(i%8u)*100.0,475.0+float(i/8u)*62.0);}
uint focusKeyCode(uint i){return i<26u?97u+i:i<36u?48u+i-26u:32u;}
#ifndef HB_READONLY
bool focusMatches(uint actual,bool picker){
 uint b=524288u+(actual+(picker?16u:0u))*32768u,kind=c.atlas[b+25604u];
 uint category=c.atlas[b+25627u];bool game=category==1u||(category==0u&&(kind==4u||kind==5u||kind==8u));if(c.rows[0]==1u&&!game)return false;if(c.rows[0]==2u&&game)return false;
 uint n=min(c.rows[2],16u);if(n==0u)return true;
 for(uint start=0u;start<64u;start++){bool same=true;for(uint j=0u;j<n;j++){uint k=start+j;if(k>=64u){same=false;break;}uint ch=(c.atlas[b+25608u+k/4u]>>((k%4u)*8u))&255u;if(ch==0u)return false;if(ch!=focusQuery(j)){same=false;break;}}if(same)return true;}return false;
}
void focusRebuild(bool picker){c.rows[8]=0u;c.rows[9]=0u;c.rows[10]=0u;c.rows[19]=0x464f4355u;uint rawCount=focusRawCount(picker);for(uint i=0u;i<rawCount;i++){if(!focusMatches(i,picker))continue;uint n=c.rows[10]++;c.rows[8u+n/8u]|=i<<((n%8u)*4u);}c.rows[11]=focusOrdinal(picker?uint(max(0,c.piece.y)):c.state.x,picker);}
void focusPut(uint i,uint ch){uint k=3u+i/4u,shift=(i%4u)*8u;c.rows[k]=(c.rows[k]&~(255u<<shift))|((ch&255u)<<shift);}
void focusKey(uint i){uint n=min(c.rows[2],16u);if(i==39u){c.rows[1]=0u;return;}if(i==38u){c.rows[2]=0u;return;}if(i==37u){if(n>0u){c.rows[2]=n-1u;focusPut(n-1u,0u);}return;}if(n<16u){focusPut(n,focusKeyCode(i));c.rows[2]=n+1u;}}
void focusSearchInput(uint pressed){
 uint k=min(c.rows[7],39u);int step=0;if((pressed&HB_LEFT)!=0u)step=-1;if((pressed&HB_RIGHT)!=0u)step=1;if((pressed&HB_UP)!=0u)step=-8;if((pressed&HB_DOWN)!=0u)step=8;
 vec2 stick=hbStick(false);c.motion.w=max(0.0,c.motion.w-FOCUS_DT);if(length(stick)>.55&&c.motion.w<=0.0){step=abs(stick.x)>abs(stick.y)?(stick.x>0.0?1:-1):(stick.y>0.0?-8:8);c.motion.w=.18;}
 k=uint((int(k)+step+40)%40);if((pressed&HB_A)!=0u)focusKey(k);if((pressed&(HB_B|HB_Y|HB_X))!=0u)c.rows[1]=0u;
 bool touch=hbTouchCount()>0u&&(c.touch[6]&2u)==0u;if(touch&&c.library.w==0u){vec2 p=hbTouch(0u)*1.5;for(uint i=0u;i<40u;i++)if(all(lessThan(abs(p-focusKeyCenter(i)),vec2(45,25)))){k=i;focusKey(i);}}
 c.library.w=touch?1u:0u;c.rows[7]=k;
}
#endif
