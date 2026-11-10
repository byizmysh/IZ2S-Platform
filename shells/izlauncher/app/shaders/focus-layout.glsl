const uint TOAST=(0x1f0000u)/4u,TOAST_SLOT=(0x1c0000u)/4u;
const float FOCUS_DT=1.0/60.0,TOAST_SECONDS=4.0;
const vec2 ADD_CENTER=vec2(188,430),HERO_CENTER=vec2(580,804),ACTION_CENTER=vec2(1480,804),FOCUS_PREV=vec2(60,430),FOCUS_NEXT=vec2(1860,430);
// Display geometry only. Navigation order and state fields are unchanged.
float focusRailY(float x){return 640.0;}
vec2 focusCardCenter(float index){return vec2(402.0+(index-c.flow.x)*218.0,430.0);}
vec2 focusCardLocal(vec2 p,float index){return p-focusCardCenter(index);}
vec2 focusHeroLocal(vec2 p){return p-HERO_CENTER;}
bool focusCardHit(vec2 p,float index){return all(lessThanEqual(abs(focusCardLocal(p,index)),vec2(109,191)));}
bool focusAddHit(vec2 p){return all(lessThanEqual(abs(p-ADD_CENTER),vec2(91,185)));}
bool focusHeroHit(vec2 p){return false;}

// The orbit, crumbs, and mouse share exactly the same curved path.
const vec2 FOCUS_ORBIT_CENTER=vec2(640,400),FOCUS_ORBIT_RADII=vec2(1170,210),FOCUS_PLANET=vec2(1854,413);
const float FOCUS_ORBIT_ANGLE=.045;
vec2 focusOrbitRotate(vec2 p){float a=FOCUS_ORBIT_ANGLE;return vec2(cos(a)*p.x-sin(a)*p.y,sin(a)*p.x+cos(a)*p.y);}
vec2 focusOrbitPoint(float angle){return FOCUS_ORBIT_CENTER+focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(cos(angle),sin(angle)));}
vec2 focusOrbitTangent(float angle){return normalize(focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(-sin(angle),cos(angle))));}
