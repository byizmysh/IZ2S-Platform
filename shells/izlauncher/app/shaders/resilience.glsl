// RUN18. Reserved shell-owned region [0x7e0000,0x7e4000) in binding 0.
// This protects menu data only; it cannot retain a destroyed device/process.
const uint RS=(0x7e0000u-1024u)/4u, RS_WORDS=4096u;
const uint RS_MAGIC=0x495a5238u,RS_GUARD=0x18c0ffeeu,RS_CORE=53u;
const uint RS_A=64u,RS_B=192u,RS_LOG=320u,RS_SLOTS=256u;
uint rs(uint i){return c.atlas[RS+i];}
bool rsVisible(){return rs(0u)==RS_MAGIC&&rs(8u)!=0u;}
const uint RS_TOGGLE=16u; // Left stick click (L3).
bool rsInlineAllowed(){
 const uint toast=0x1f0000u/4u;
 return c.rows[1]==0u&&c.rows[15]==0u&&c.rows[16]==0u&&c.transition.y==0.0&&c.bagMeta.y==0u&&
 !(c.atlas[toast]==0x495a4e31u&&uintBitsToFloat(c.atlas[toast+4u])>0.0);
}
bool rsInlineVisible(){return rsVisible()&&rsInlineAllowed();}
#ifndef HB_READONLY
void rsPut(uint i,uint v){c.atlas[RS+i]=v;}
uint rsCore(uint i){
 if(i==0u)return c.state.x;
 if(i<5u)return floatBitsToUint(c.motion[i-1u]);
 if(i<9u)return c.game[i-5u];
 if(i<13u)return uint(c.piece[i-9u]);
 if(i<17u)return floatBitsToUint(c.flow[i-13u]);
 if(i<21u)return c.bagMeta[i-17u];
 if(i<29u)return c.bag[i-21u];
 if(i<49u)return c.rows[i-29u];
 return c.stats[i-49u];
}
void rsCorePut(uint i,uint v){
 if(i==0u)c.state.x=v;
 else if(i<5u)c.motion[i-1u]=uintBitsToFloat(v);
 else if(i<9u)c.game[i-5u]=v;
 else if(i<13u)c.piece[i-9u]=int(v);
 else if(i<17u)c.flow[i-13u]=uintBitsToFloat(v);
 else if(i<21u)c.bagMeta[i-17u]=v;
 else if(i<29u)c.bag[i-21u]=v;
 else if(i<49u)c.rows[i-29u]=v;
 else c.stats[i-49u]=v;
}
uint rsHash(uint base,uint generation,bool live){
 uint h=2166136261u^generation;
 for(uint i=0u;i<RS_CORE;i++)h=(h^(live?rsCore(i):rs(base+3u+i)))*16777619u;
 return h;
}
bool rsShape(){
 return c.state.x<=min(c.library.x,15u)&&c.piece.x>=0&&c.piece.x<=1&&
 c.piece.y>=0&&c.piece.y<=15&&c.rows[0]<=2u&&c.rows[1]<=1u&&
 c.rows[2]<=16u&&c.rows[7]<40u&&c.rows[10]<=16u&&
 !any(isnan(c.motion))&&!any(isinf(c.motion))&&!any(isnan(c.flow))&&!any(isinf(c.flow));
}
bool rsValid(uint base){return rs(base)==RS_GUARD&&rs(base+3u+RS_CORE)==RS_GUARD&&rs(base+2u)==rsHash(base,rs(base+1u),false);}
void rsCopy(uint base){
 rsPut(base,RS_GUARD);rsPut(base+1u,rs(2u));
 for(uint i=0u;i<RS_CORE;i++)rsPut(base+3u+i,rsCore(i));
 rsPut(base+2u,rsHash(0u,rs(2u),true));rsPut(base+3u+RS_CORE,RS_GUARD);
}
void rsCancel(){
 c.transition=vec4(0);c.reserved.y=0u;c.reserved.z=0u;
 c.piece.w=0;c.bagMeta.y=0u;c.rows[15]=0u;c.rows[16]=0u;
 c.library.w=0u;c.motion.w=0.0;rsPut(10u,0u);
}
void rsHome(){
 rsCancel();c.game=uvec4(0);c.piece=ivec4(0);c.flow=vec4(0);c.stats=uvec4(0);
 for(uint i=0u;i<20u;i++)c.rows[i]=0u;
 c.state.x=min(c.state.x,min(c.library.x,15u));c.motion=vec4(0,float(c.state.x),0,0);
 // Close only the shell's built-in scene, without requesting host work.
 c.atlas[(0x700004u-1024u)/4u]=0u;
}
void rsSeal(){
 rsPut(2u,rs(2u)+1u);rsPut(3u,rsHash(0u,rs(2u),true));
 rsCopy((rs(2u)&1u)==0u?RS_A:RS_B);
 rsPut(16u,RS_GUARD);rsPut(RS_WORDS-1u,RS_GUARD);
}
bool rsBegin(){
 uint epoch=floatBitsToUint(c.refresh.w);bool a=rsValid(RS_A),b=rsValid(RS_B);
 if(rs(0u)!=RS_MAGIC&&!a&&!b){
  for(uint i=0u;i<64u;i++)rsPut(i,0u);
  rsPut(0u,RS_MAGIC);rsPut(1u,epoch);
  if(!rsShape()){rsHome();rsPut(4u,16u);rsPut(14u,1u);}
  rsSeal();rsCopy(RS_A);rsCopy(RS_B);
 }else if(rs(0u)==RS_MAGIC&&rs(1u)!=epoch){
  // The host publishes this epoch only while rendering is paused.
  rsPut(1u,epoch);rsPut(10u,0u);rsPut(13u,0u);
  if(!rsShape()){rsHome();rsPut(4u,rs(4u)|16u);rsPut(14u,1u);}
  rsSeal();rsCopy(RS_A);rsCopy(RS_B);
 }else{
  bool live=rs(0u)==RS_MAGIC&&rsShape()&&rs(3u)==rsHash(0u,rs(2u),true);
  uint fault=(live?0u:1u)|(a?0u:4u)|(b?0u:8u)|((rs(16u)==RS_GUARD&&rs(RS_WORDS-1u)==RS_GUARD)?0u:2u);
  if(fault!=0u){
   rsPut(4u,rs(4u)|fault);
   if(!live){
    if(a||b){uint base=!a?RS_B:!b?RS_A:int(rs(RS_A+1u)-rs(RS_B+1u))>0?RS_A:RS_B;
     for(uint i=0u;i<RS_CORE;i++)rsCorePut(i,rs(base+3u+i));rsPut(2u,rs(base+1u));rsPut(5u,rs(5u)+1u);
    }else{rsHome();rsPut(4u,rs(4u)|16u);}
    if(!rsShape()){rsHome();rsPut(4u,rs(4u)|16u);}
    rsCancel();c.atlas[(0x700004u-1024u)/4u]=0u;rsPut(14u,1u);
   }
   rsPut(0u,RS_MAGIC);rsPut(1u,epoch);rsSeal();rsCopy(RS_A);rsCopy(RS_B);
  }
 }
 rsPut(6u,rs(6u)+1u);c.state.z=rs(6u);
 uint pressed=hbPressed();rsPut(7u,rs(7u)+uint(bitCount(pressed)));
 const uint clk=(0x700c80u+240u-1024u)/4u;
 uint lo=c.atlas[clk],hi=c.atlas[clk+1u];bool clock=c.atlas[clk+2u]==31250000u;
 if(clock&&rs(13u)!=0u){uint delta=lo-rs(11u),upper=hi-rs(12u)-(lo<rs(11u)?1u:0u);
  if(upper!=0u||delta>15625000u){rsPut(9u,rs(9u)+1u);rsCancel();rsPut(14u,1u);}
 }
 rsPut(11u,lo);rsPut(12u,hi);rsPut(13u,clock?1u:0u);
 // Status expands in the menu; normal controls and animations continue.
 if((pressed&RS_TOGGLE)!=0u)rsPut(8u,rsVisible()?0u:1u);
 if(rs(14u)!=0u){if(hbButtons()==0u&&hbTouchCount()==0u)rsPut(14u,0u);hbEndFrame();return false;}
 if(c.transition.y!=0.0||c.bagMeta.y!=0u||c.rows[16]!=0u){
  rsPut(10u,rs(10u)+1u);
  // Frames stop during serviced transitions. An unserviced request is bounded.
  if(rs(10u)>=120u){rsCancel();rsPut(18u,rs(18u)+1u);hbEndFrame();return false;}
 }else rsPut(10u,0u);
 return true;
}
void rsEnd(){
 rsSeal();
 if(rs(13u)==0u)return;
 uint lo=rs(11u),hi=rs(12u),delta=lo-rs(22u),upper=hi-rs(23u)-(lo<rs(22u)?1u:0u);
 if(rs(24u)!=0u&&upper==0u&&delta<31250000u)return;
 uint slot=rs(20u)%RS_SLOTS,o=RS_LOG+slot*8u;
 rsPut(o,lo);rsPut(o+1u,hi);rsPut(o+2u,rs(6u));rsPut(o+3u,rs(7u));
 rsPut(o+4u,rs(4u));rsPut(o+5u,rs(5u));rsPut(o+6u,rs(9u));rsPut(o+7u,rs(18u));
 rsPut(20u,(slot+1u)%RS_SLOTS);rsPut(21u,min(rs(21u)+1u,RS_SLOTS));rsPut(22u,lo);rsPut(23u,hi);rsPut(24u,1u);
}
#endif
