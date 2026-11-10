const uint TOAST=(0x1f0000u)/4u,TOAST_SLOT=(0x1c0000u)/4u;
const float FOCUS_DT=1.0/60.0,TOAST_SECONDS=4.0;
const vec2 ADD_CENTER=vec2(188,690),HERO_CENTER=vec2(535,400),ACTION_CENTER=vec2(903,515),FOCUS_PREV=vec2(60,690),FOCUS_NEXT=vec2(1852,664);
float focusRailY(float x){float dx=x-850.0,u=dx/1200.0;return 560.0+300.0*sqrt(max(.02,1.0-u*u))+.018*dx;}
vec2 focusCardCenter(float index){float x=402.0+(index-c.flow.x)*218.0;return vec2(x,focusRailY(x)-116.0);}
vec2 focusCardLocal(vec2 p,float index){vec2 q=p-focusCardCenter(index);q.y+=.07*q.x;q.y/=1.0-.00015*q.x;return q;}
vec2 focusHeroLocal(vec2 p){vec2 q=p-HERO_CENTER;q.y/=max(.75,1.0-.00035*q.x);return q;}
bool focusCardHit(vec2 p,float index){return all(lessThanEqual(abs(focusCardLocal(p,index)),vec2(105,109)));}
bool focusHeroHit(vec2 p){return all(lessThanEqual(abs(focusHeroLocal(p)),vec2(180,184)))||all(lessThanEqual(abs(p-ACTION_CENTER),vec2(123,42)));}

// The orbit, crumbs, and mouse share exactly the same curved path.
const vec2 FOCUS_ORBIT_CENTER=vec2(640,400),FOCUS_ORBIT_RADII=vec2(1170,210),FOCUS_PLANET=vec2(1854,413);
const float FOCUS_ORBIT_ANGLE=.045;
vec2 focusOrbitRotate(vec2 p){float a=FOCUS_ORBIT_ANGLE;return vec2(cos(a)*p.x-sin(a)*p.y,sin(a)*p.x+cos(a)*p.y);}
vec2 focusOrbitPoint(float angle){return FOCUS_ORBIT_CENTER+focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(cos(angle),sin(angle)));}
vec2 focusOrbitTangent(float angle){return normalize(focusOrbitRotate(FOCUS_ORBIT_RADII*vec2(-sin(angle),cos(angle))));}
