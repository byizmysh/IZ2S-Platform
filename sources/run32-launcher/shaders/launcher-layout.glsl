// Shared drawing and pointer geometry for the horizontal app tiles.
const uint IZ_PAGE_SIZE=5u;
const float IZ_CONTENT_LEFT=160.0,IZ_CONTENT_RIGHT=1760.0;
const float IZ_FOOTER_Y=1016.0,IZ_CAPTION_Y=848.0;
vec2 izTileCenter(uint slot){return vec2(288.0+336.0*float(slot),600.0);}
float izTileDistance(vec2 p,vec2 center){
 vec2 q=abs(p-center)-vec2(128,224)+7.0;
 return min(max(q.x,q.y),0.0)+length(max(q,0.0))-7.0;
}
