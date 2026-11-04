#include "ps-layout.glsl"
const uint TOAST=(0x1f0000u)/4u,TOAST_SLOT=(0x1c0000u)/4u;
const float FOCUS_DT=1.0/60.0,TOAST_SECONDS=4.0;
const vec2 ADD_CENTER=PS_EXCHANGE_CENTER,HERO_CENTER=vec2(384,368),ACTION_CENTER=PS_ACTION;
float focusRailY(float x){return 530.0;}
vec2 focusCardCenter(float index){return vec2(624.0+(index-c.flow.x)*232.0,368.0);}
vec2 focusCardLocal(vec2 p,float index){return p-focusCardCenter(index);}
vec2 focusHeroLocal(vec2 p){return p-HERO_CENTER;}
bool focusCardHit(vec2 p,float index){return all(lessThanEqual(abs(focusCardLocal(p,index)),PS_CARD_HALF));}
bool focusHeroHit(vec2 p){return all(lessThanEqual(abs(p-HERO_CENTER),PS_CARD_HALF))||all(lessThanEqual(abs(p-ACTION_CENTER),vec2(128,28)));}

// The orbit, crumbs, and mouse share exactly the same curved path.
const vec2 FOCUS_ORBIT_CENTER=vec2(640,400),FOCUS_ORBIT_RADII=vec2(1170,210),FOCUS_PLANET=vec2(1854,413);
const float FOCUS_ORBIT_ANGLE=.045;
vec2 focusOrbitRotate(vec2 p){float a=FOCUS_ORBIT_ANGLE;return vec2(cos(a)*p.x-sin(a)*p.y,sin(a)*p.x+cos(a)*p.y);}
vec2 focusOrbitPoint(float angle){return FOCUS_ORBIT_CENTER+focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(cos(angle),sin(angle)));}
vec2 focusOrbitTangent(float angle){return normalize(focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(-sin(angle),cos(angle))));}
