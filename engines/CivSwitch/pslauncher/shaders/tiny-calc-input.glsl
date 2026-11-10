// The menu's scratch is saved while calculator arithmetic owns it.
void calcMenu(bool save){
 uint b=CB+64u;
 for(uint i=0u;i<4u;i++){
  if(save){c.atlas[b+i]=c.game[i];c.atlas[b+4u+i]=uint(c.piece[i]);c.atlas[b+8u+i]=floatBitsToUint(c.flow[i]);c.atlas[b+12u+i]=c.bagMeta[i];c.atlas[b+44u+i]=c.stats[i];}
  else{c.game[i]=c.atlas[b+i];c.piece[i]=int(c.atlas[b+4u+i]);c.flow[i]=uintBitsToFloat(c.atlas[b+8u+i]);c.bagMeta[i]=c.atlas[b+12u+i];c.stats[i]=c.atlas[b+44u+i];}
 }
 for(uint i=0u;i<8u;i++){if(save)c.atlas[b+16u+i]=c.bag[i];else c.bag[i]=c.atlas[b+16u+i];}
 for(uint i=0u;i<20u;i++){if(save)c.atlas[b+24u+i]=c.rows[i];else c.rows[i]=c.atlas[b+24u+i];}
}
void calcPaint(){
 uint base=CB+320u;for(uint i=0u;i<32u;i++)c.atlas[base+i]=0u;
 for(int i=0;i<c.piece.x;i++)c.atlas[base+uint(i)/4u]|=c.rows[i]<<uint((i%4)*8);
 c.atlas[base+8u]=c.bag[1];uint op=c.game.w;
 c.atlas[base+9u]=op==1u?43u:op==2u?45u:op==3u?120u:op==4u?47u:0u;
}
void calcOpen(){
 calcMenu(true);c.game=uvec4(0);c.piece=ivec4(0);c.flow=vec4(0);c.bagMeta=uvec4(0);c.stats=uvec4(0);
 for(int i=0;i<8;i++)c.bag[i]=0u;for(int i=0;i<20;i++)c.rows[i]=0u;
 clearCalc();calcPaint();c.atlas[CB]=0u;c.atlas[CB+1u]=1u;c.atlas[CB+2u]=c.touch[2]>0u?1u:0u;c.atlas[CB+4u]=25u;
 c.transition.xy=vec2(0);
}
void calcInput(uint pressed){
 if(c.atlas[CB+3u]>0u)c.atlas[CB+3u]--;
 bool held=c.touch[2]>0u,tap=c.touch[2]==1u&&(c.touch[6]&2u)==0u&&c.atlas[CB+2u]==0u;
 vec2 p=vec2(c.touch[8],c.touch[9])*1.5;
 if((pressed&(HB_B|HB_MINUS))!=0u||(tap&&all(lessThanEqual(abs(p-vec2(1272,96)),vec2(28,20))))){
  calcMenu(false);c.atlas[CB+1u]=0u;c.library.w=held?1u:0u;c.state.y=hbButtons();return;
 }
 int key=-1,sel=int(c.atlas[CB+4u]);
 if((pressed&HB_LEFT)!=0u)sel=(sel+27)%28;if((pressed&HB_RIGHT)!=0u)sel=(sel+1)%28;
 if((pressed&HB_UP)!=0u)sel=sel>=25?sel==25?20:sel==26?22:23:max(0,sel-5);
 if((pressed&HB_DOWN)!=0u)sel=sel>=20?sel==20||sel==21?25:sel==22?26:sel==23?27:24:min(24,sel+5);
 c.atlas[CB+4u]=uint(sel);
 if((pressed&HB_A)!=0u)key=sel;if((pressed&HB_PLUS)!=0u)key=24;if((pressed&HB_X)!=0u)key=5;if((pressed&HB_Y)!=0u)key=7;
 if(tap)key=calcHit(p);
 if(key>=0){activateKey(key);calcPaint();c.atlas[CB]++;c.atlas[CB+4u]=uint(key);c.atlas[CB+3u]=7u;}
 c.atlas[CB+2u]=held?1u:0u;c.state.y=hbButtons();
}
