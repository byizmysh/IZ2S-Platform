// Same bounded activity mailbox as IZPSlauncher, in the existing font gap.
const uint IZ2_ACTIVITY=0xf0000u/4u,IZ2_ACTIVITY_TEXT=0x100000u/4u;
// Visual interpolation lives after its 128-byte header, outside all core state.
const uint IZ2_ANIM=0xf0100u/4u,IZ2_ANIM_MAGIC=0x49325549u;
const float IZ2_CONTENT_TOP=174.0,IZ2_ACTIVITY_TOP=710.0,IZ2_CONTENT_BOTTOM=934.0;
uint iz2ActivityCount(){return c.atlas[IZ2_ACTIVITY]==0x50534143u?min(c.atlas[IZ2_ACTIVITY+1u],6u):0u;}
float iz2ScrollMax(){return max(0.0,IZ2_ACTIVITY_TOP+max(1.0,float(iz2ActivityCount()))*92.0+36.0-IZ2_CONTENT_BOTTOM);}
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
 if(c.atlas[IZ2_ANIM]!=IZ2_ANIM_MAGIC){for(uint i=0u;i<48u;i++)c.atlas[IZ2_ANIM+i]=0u;c.atlas[IZ2_ANIM]=IZ2_ANIM_MAGIC;}
 uint selectedTile=add?32u:actual+(picker?16u:0u),hover=0xffffffffu;
 // Pointer hover is visual only; it never launches or changes an application.
 if(hbHas(HB_MOUSE)){
  vec2 point=hbMouse();uvec2 encoded=floatBitsToUint(point);
  if(any(notEqual(encoded,uvec2(c.atlas[IZ2_ANIM+1u],c.atlas[IZ2_ANIM+2u]))))c.atlas[IZ2_ANIM+3u]=1u;
  c.atlas[IZ2_ANIM+1u]=encoded.x;c.atlas[IZ2_ANIM+2u]=encoded.y;
  if(pressed!=0u||length(hbStick(false))>.4)c.atlas[IZ2_ANIM+3u]=0u;
  if(c.atlas[IZ2_ANIM+3u]>0u){if(iz2ContentHit(point)){
   point.y+=uintBitsToFloat(c.rows[13]);if(focusAddHit(point))hover=32u;
   else for(uint i=0u;i<focusCount(picker);i++)if(focusCardHit(point,float(i)))hover=focusIndex(i)+(picker?16u:0u);
  }}
 }
 for(uint i=0u;i<33u;i++){
  float v=uintBitsToFloat(c.atlas[IZ2_ANIM+8u+i]);if(isnan(v)||isinf(v))v=0.0;
  float target=i==selectedTile||i==hover?1.0:0.0;v=mix(clamp(v,0.0,1.0),target,.20);if(abs(v-target)<.001)v=target;c.atlas[IZ2_ANIM+8u+i]=floatBitsToUint(v);
 }
}
#endif
