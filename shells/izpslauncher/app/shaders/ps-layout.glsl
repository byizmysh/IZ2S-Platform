// One layout for rendering and hit testing. Main columns use 24/32/48px spacing.
const float PS_LEFT=280.0,PS_MAIN_RIGHT=1200.0,PS_SIDE_LEFT=1248.0,PS_RIGHT=1856.0;
const float PS_RAIL_TOP=208.0,PS_RAIL_BOTTOM=528.0,PS_BODY_TOP=720.0,PS_BODY_BOTTOM=944.0;
const vec2 PS_CARD_HALF=vec2(104,160),PS_ACTION=vec2(1072,596);
const float PS_CONTENT_TOP=174.0,PS_CONTENT_BOTTOM=1016.0;
bool psStatusVisible(){return rsInlineVisible()&&!loadProgressVisible();}
float psContentBottom(){return psStatusVisible()?896.0:PS_CONTENT_BOTTOM;}
const vec2 PS_EXCHANGE_CENTER=vec2(156,368),PS_EXCHANGE_HALF=vec2(92,160),PS_COLLECTION_HALF=vec2(304,49);
bool psContentHit(vec2 p){return p.y>=PS_CONTENT_TOP&&p.y<psContentBottom();}
bool psExchangeHit(vec2 p){return all(lessThanEqual(abs(p-PS_EXCHANGE_CENTER),PS_EXCHANGE_HALF));}
vec2 psCollectionCenter(uint i){return vec2(1552,769+float(i)*126.0);}
int psCollectionAt(vec2 p){for(uint i=0u;i<2u;i++)if(all(lessThanEqual(abs(p-psCollectionCenter(i)),PS_COLLECTION_HALF)))return int(i);return -1;}
// A single top row: brand, mouse lane, category group, search, clock.
const float PS_HEADER_Y=88.0,PS_EDGE=32.0,PS_HEADER_RIGHT=1920.0-PS_EDGE;
// Allow 80px for the whole mouse in either facing direction at both turns.
const float PS_MOUSE_LEFT=465.0,PS_MOUSE_RIGHT=1096.0;
const vec2 PS_SEARCH_CENTER=vec2(1688,PS_HEADER_Y),PS_CLOCK_ORIGIN=vec2(1780,76);
vec2 psCategoryCenter(uint i){return vec2(i==0u?1240.0:i==1u?1348.0:1508.0,PS_HEADER_Y);}
vec2 psCategoryHalf(uint i){return vec2(i==0u?40.0:i==1u?56.0:92.0,20);}
int psCategoryAt(vec2 p){for(uint i=0u;i<3u;i++)if(all(lessThanEqual(abs(p-psCategoryCenter(i)),psCategoryHalf(i))))return int(i);return -1;}
bool psSearchHit(vec2 p){return all(lessThanEqual(abs(p-PS_SEARCH_CENTER),vec2(68,20)));}
// A reserved atlas mailbox is shared by graphics and input, outside package slots.
const uint PS_ACTIVITY_BASE=0xf0000u/4u,PS_ACTIVITY_TEXT=0x100000u/4u;
const vec2 PS_PROGRESS_ORIGIN=vec2(PS_LEFT+16.0,PS_BODY_TOP+16.0),PS_PROGRESS_SIZE=vec2(PS_MAIN_RIGHT-PS_LEFT-32.0,162);
uint psActivityCount(){return c.atlas[PS_ACTIVITY_BASE]==0x50534143u?min(c.atlas[PS_ACTIVITY_BASE+1u],6u):0u;}
float psActivityHeight(){if(loadProgressVisible())return PS_PROGRESS_SIZE.y+32.0;uint count=psActivityCount();return count==0u?80.0:32.0+float(count)*96.0;}
float psScrollMax(){return max(0.0,max(PS_BODY_TOP+psActivityHeight(),PS_BODY_BOTTOM)+32.0-psContentBottom());}
uint psScrollSteps(){return uint(ceil(psScrollMax()/140.0));}
