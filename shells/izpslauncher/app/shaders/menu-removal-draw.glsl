// Shared removal confirmation, using the shell font and palette.
const uint REMOVE_TEXT_0[20]=uint[20](82u,101u,109u,111u,118u,101u,32u,102u,114u,111u,109u,32u,115u,101u,115u,115u,105u,111u,110u,63u);
const uint REMOVE_TEXT_1[31]=uint[31](84u,104u,101u,32u,80u,67u,32u,112u,97u,99u,107u,97u,103u,101u,32u,115u,116u,97u,121u,115u,32u,97u,118u,97u,105u,108u,97u,98u,108u,101u,46u);
const uint REMOVE_TEXT_2[9]=uint[9](65u,32u,32u,82u,101u,109u,111u,118u,101u);
const uint REMOVE_TEXT_3[9]=uint[9](66u,32u,32u,67u,97u,110u,99u,101u,108u);
const uint REMOVE_TEXT_4[8]=uint[8](45u,32u,82u,101u,109u,111u,118u,101u);
const uint REMOVE_TEXT_7[31]=uint[31](82u,101u,109u,111u,118u,97u,108u,32u,102u,97u,105u,108u,101u,100u,46u,32u,65u,32u,47u,32u,66u,32u,116u,111u,32u,99u,108u,111u,115u,101u,46u);
const uint REMOVE_TEXT_6[33]=uint[33](71u,97u,109u,101u,115u,44u,32u,66u,73u,79u,83u,32u,97u,110u,100u,32u,115u,97u,118u,101u,115u,32u,115u,116u,97u,121u,32u,111u,110u,32u,83u,68u,46u);
const uint REMOVE_TEXT_5[15]=uint[15](82u,101u,109u,111u,118u,101u,32u,102u,114u,111u,109u,32u,83u,68u,63u);
uint removalChar(uint which,uint i){if(which==0u&&i<20u)return REMOVE_TEXT_0[i];if(which==1u&&i<31u)return REMOVE_TEXT_1[i];if(which==2u&&i<9u)return REMOVE_TEXT_2[i];if(which==3u&&i<9u)return REMOVE_TEXT_3[i];if(which==4u&&i<8u)return REMOVE_TEXT_4[i];if(which==5u&&i<15u)return REMOVE_TEXT_5[i];if(which==6u&&i<33u)return REMOVE_TEXT_6[i];if(which==7u&&i<31u)return REMOVE_TEXT_7[i];return 0u;}
float removalTextWidth(uint which,float scale){uint weight=which==0u||which==5u?0u:1u;float width=0.0;for(uint i=0u;i<33u;i++){uint ch=removalChar(which,i);if(ch==0u)break;width+=LOAD_ADVANCE[weight*96u+ch-32u];}return width*scale;}
float removalText(vec2 p,vec2 origin,uint which,float scale){vec2 q=(p-origin)/scale;if(q.x<0.0||q.y<0.0||q.y>=32.0)return 0.0;uint weight=which==0u||which==5u?0u:1u;float cursor=0.0,mask=0.0;for(uint i=0u;i<33u;i++){uint ch=removalChar(which,i);if(ch==0u||cursor>q.x+1.0)break;uint glyph=ch-32u;if(q.x<=cursor+32.0)mask=max(mask,loadProgressGlyph(q-vec2(cursor,0),glyph,weight));cursor+=LOAD_ADVANCE[weight*96u+glyph];}return mask;}
vec3 menuRemovalDraw(vec2 p,vec3 col,float footerY){

if(c.rows[15]==0u&&c.rows[16]==0u&&c.rows[18]==0u)return col;col*=.24;float panel=box(p,vec2(960,540),vec2(390,180),18);col=mix(col,vec3(.025,.08,.20),fill(panel));col=mix(col,MUTED,line(panel,.7)*.35);
if(c.rows[18]!=0u){col=mix(col,INK,removalText(p,vec2(660,520),7u,.9));return col;}
uint selected=(c.rows[15]!=0u?c.rows[15]:c.rows[16])-1u;uint base=slotBase(min(selected,14u));bool sd=c.atlas[(0x700d7cu-1024u)/4u]==1u||c.atlas[base+25628u]==0x44534453u;col=mix(col,INK,removalText(p,vec2(660,394),sd?5u:0u,1.15));vec4 icon=focusIcon(p,vec2(688,492),base,64);col=mix(col,icon.rgb,icon.a);float width=float(c.atlas[base+25600u]);col=mix(col,INK,focusLabel(p,vec2(748,480),base,min(1.0,510.0/max(1.0,width))));col=mix(col,MUTED,removalText(p,vec2(660,546),sd?6u:1u,.85));
for(uint i=0u;i<2u;i++){vec2 center=vec2(i==0u?790:1130,646);float button=box(p,center,vec2(140,28),10);col=mix(col,i==0u?BLUE:vec3(.16,.23,.34),fill(button));col=mix(col,i==0u?BG:INK,removalText(p,center-vec2(removalTextWidth(i+2u,.9)*.5,14.4),i+2u,.9));}return col;}
