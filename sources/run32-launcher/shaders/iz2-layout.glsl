// Same bounded activity mailbox as IZPSlauncher, in the existing font gap.
const uint IZ2_ACTIVITY=0xf0000u/4u,IZ2_ACTIVITY_TEXT=0x100000u/4u;
const uint IZ2_CLOCK=0xf0080u/4u,IZ2_CLOCK_MAGIC=0x495a434bu;
const uint IZ2_BATTERY=IZ2_CLOCK+2u,IZ2_BATTERY_MAGIC=0x495a4254u;
const uint IZ2_MENU_VERSIONS=0xf0200u/4u;
bool iz2BatteryAvailable(){
 return c.atlas[IZ2_BATTERY]==IZ2_BATTERY_MAGIC&&(c.atlas[IZ2_BATTERY+1u]&255u)<=100u;
}
float iz2HeaderShift(){
 return FOCUS_HEADER_CLOCK_SPACE+FOCUS_HEADER_BATTERY_SPACE;
}
// Visual interpolation lives after its 128-byte header, outside all core state.
const uint IZ2_ANIM=0xf0100u/4u,IZ2_ANIM_MAGIC=0x49325549u;
const float IZ2_CONTENT_TOP=110.0,IZ2_ACTIVITY_TOP=815.0,IZ2_ACTIVITY_ROW=56.0,IZ2_CONTENT_BOTTOM=934.0;
const vec3 IZ2_DEFAULT_TINT=vec3(.63,.55,.99);
const uint IZ2_TINT=IZ2_ANIM+4u, IZ2_TINT_ORIGIN=IZ2_ANIM+7u, IZ2_NETWORK_OPEN=IZ2_ANIM+41u;
const uint IZ2_FOCUS_TAG=IZ2_ANIM+42u,IZ2_FOCUS_AGE=IZ2_ANIM+44u;
#ifdef IZ_CORE_LOCAL_ADD
const uint CORE_LOCAL_ADD=(0x704000u-1024u)/4u;
const uint CORE_GRID=CORE_LOCAL_ADD+24577u;
bool iz2NetworkAvailable(){
 return c.atlas[CORE_GRID]==0x495a4752u&&c.atlas[CORE_LOCAL_ADD+37289u]==1u;
}
#endif
uint iz2CoverKind(uint base,uint index){
 uint prefix=c.atlas[base+25608u];
 if(prefix==0x73327a69u)return 0u; // iz2s
 if(prefix==0x73707a69u)return (c.atlas[base+25609u]&255u)==50u?2u:1u; // izps
 if(prefix==0x646c6f66u)return 3u; // fold
 if(prefix==0x6b6f6f62u)return 4u; // book
 if(prefix==0x6f636176u)return 5u; // vaco
 if(prefix==0x74696c62u)return 6u; // blit
 return index%7u;
}
vec3 iz2CoverAccent(uint kind){
 const vec3 colors[7]=vec3[7](vec3(.71,.91,.32),vec3(.45,.76,1),IZ2_DEFAULT_TINT,
   vec3(.40,.87,.82),vec3(.94,.87,.67),vec3(1,.60,.47),vec3(1,.82,.51));
 return colors[kind];
}
uint iz2ActivityCount(){return c.atlas[IZ2_ACTIVITY]==0x50534143u?min(c.atlas[IZ2_ACTIVITY+1u],6u):0u;}
float iz2ScrollMax(){return max(0.0,IZ2_ACTIVITY_TOP+float(iz2ActivityCount())*IZ2_ACTIVITY_ROW+6.0-IZ2_CONTENT_BOTTOM);}
bool iz2ContentHit(vec2 p){return p.y>=IZ2_CONTENT_TOP&&p.y<IZ2_CONTENT_BOTTOM;}
float iz2Grow(uint slot){return c.atlas[IZ2_ANIM]==IZ2_ANIM_MAGIC?clamp(uintBitsToFloat(c.atlas[IZ2_ANIM+8u+min(slot,32u)]),0.0,1.0):0.0;}
#ifndef HB_READONLY
float iz2ScrollInput(uint pressed){
 float limit=iz2ScrollMax(),target=clamp(uintBitsToFloat(c.rows[12]),0.0,limit);
 if(c.rows[1]==0u){
  uint held=hbButtons();float direction=float((held&HB_DOWN)!=0u)-float((held&HB_UP)!=0u);
  float y=hbStick(false).y;if(abs(y)>.2)direction=-y;
  target+=direction*460.0*FOCUS_DT;
  if((pressed&HB_DOWN)!=0u)target+=52.0;if((pressed&HB_UP)!=0u)target-=52.0;
  if((pressed&(HB_LEFT|HB_RIGHT|HB_B))!=0u||abs(hbStick(false).x)>.55)target=0.0;
 }
 target=clamp(target,0.0,limit);c.rows[12]=floatBitsToUint(target);
 float scroll=mix(clamp(uintBitsToFloat(c.rows[13]),0.0,limit),target,.18);if(abs(target-scroll)<.1)scroll=target;c.rows[13]=floatBitsToUint(scroll);return scroll;
}
void iz2Animate(bool picker,uint actual,bool add,uint pressed){
 bool first=c.atlas[IZ2_ANIM]!=IZ2_ANIM_MAGIC;
 if(first){for(uint i=0u;i<48u;i++)c.atlas[IZ2_ANIM+i]=0u;c.atlas[IZ2_ANIM]=IZ2_ANIM_MAGIC;}
 uint selectedTile=add?32u:actual+(picker?16u:0u),hover=0xffffffffu;
 uint tag=c.atlas[IZ2_FOCUS_TAG];
 float age=tag==selectedTile+1u?min(1.0,uintBitsToFloat(c.atlas[IZ2_FOCUS_AGE])+FOCUS_DT):0.0;
 c.atlas[IZ2_FOCUS_TAG]=selectedTile+1u;
 c.atlas[IZ2_FOCUS_AGE]=floatBitsToUint(age);
 vec3 tint=add||actual>=16u?IZ2_DEFAULT_TINT:iz2CoverAccent(iz2CoverKind(524288u+selectedTile*32768u,actual));
 float origin=add?.37:clamp(focusCardCenter(float(c.rows[11])).x/1920.0,.10,.90);
 // Time constant of 0.19s, with the selected cover's color nearly settled after 0.7s.
 float blend=first?1.0:1.0-exp(-FOCUS_DT/.19);
 for(uint i=0u;i<3u;i++)c.atlas[IZ2_TINT+i]=floatBitsToUint(mix(uintBitsToFloat(c.atlas[IZ2_TINT+i]),tint[i],blend));
 c.atlas[IZ2_TINT_ORIGIN]=floatBitsToUint(mix(uintBitsToFloat(c.atlas[IZ2_TINT_ORIGIN]),origin,blend));
 float drawer=uintBitsToFloat(c.atlas[IZ2_NETWORK_OPEN]);
 c.atlas[IZ2_NETWORK_OPEN]=floatBitsToUint(mix(drawer,rsVisible()?1.0:0.0,.22));
 // Pointer hover is visual only; it never launches or changes an application.
 if(hbHas(HB_MOUSE)){
  vec2 point=hbMouse();uvec2 encoded=floatBitsToUint(point);
  if(any(notEqual(encoded,uvec2(c.atlas[IZ2_ANIM+1u],c.atlas[IZ2_ANIM+2u]))))c.atlas[IZ2_ANIM+3u]=1u;
  c.atlas[IZ2_ANIM+1u]=encoded.x;c.atlas[IZ2_ANIM+2u]=encoded.y;
  if(pressed!=0u||length(hbStick(false))>.4)c.atlas[IZ2_ANIM+3u]=0u;
  if(c.atlas[IZ2_ANIM+3u]>0u){if(iz2ContentHit(point)){
   point.y+=uintBitsToFloat(c.rows[13]);
   for(uint i=0u;i<focusCount(picker);i++)if(focusCardHit(point,float(i)))hover=focusIndex(i)+(picker?16u:0u);
  }}
 }
 for(uint i=0u;i<33u;i++){
  float v=uintBitsToFloat(c.atlas[IZ2_ANIM+8u+i]);if(isnan(v)||isinf(v))v=0.0;
  float target=i==selectedTile?1.0:i==hover?.28:0.0;
  v=mix(clamp(v,0.0,1.0),target,.17);if(abs(v-target)<.001)v=target;c.atlas[IZ2_ANIM+8u+i]=floatBitsToUint(v);
 }
}
#endif
