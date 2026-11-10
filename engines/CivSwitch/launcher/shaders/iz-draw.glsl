const vec3 INK=vec3(242,241,232)/255.0;
const vec3 BLUE=vec3(213,255,0)/255.0;
const vec3 MUTED=vec3(167,170,165)/255.0;
const vec3 BG=vec3(16,17,18)/255.0;
const vec3 PANEL=vec3(25,27,29)/255.0;
float box(vec2 p,vec2 center,vec2 halfSize,float radius){vec2 q=abs(p-center)-halfSize+radius;return min(max(q.x,q.y),0.0)+length(max(q,0.0))-radius;}
float fill(float d){return 1.0-smoothstep(-.8,.8,d);}
float line(float d,float w){return 1.0-smoothstep(w-.8,w+.8,abs(d));}
float stroke(vec2 p,vec2 a,vec2 b){vec2 v=b-a;return length(p-a-v*clamp(dot(p-a,v)/dot(v,v),0.0,1.0));}
float text(vec2 p,vec2 origin,ivec4 rect){
 ivec2 q=ivec2(floor(p-origin));
 if(any(lessThan(p-origin,vec2(0)))||any(greaterThanEqual(q,rect.zw)))return 0.0;
 uint index=uint((q.y+rect.y)*IZ_ATLAS_WIDTH+q.x+rect.x);
 return float((c.atlas[index>>4u]>>((index&15u)*2u))&3u)/3.0;
}
