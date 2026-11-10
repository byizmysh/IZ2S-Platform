#version 450
layout(location=0) out vec4 color;
layout(set=0,binding=1,std430) readonly buffer Pixels {uint words[];} frame;
vec3 pixel(ivec2 p,ivec2 size){
 p=clamp(p,ivec2(0),size-1);uint v=frame.words[1024+p.y*size.x+p.x];return vec3(v&255u,(v>>8u)&255u,(v>>16u)&255u)/255.;
}
void main(){
 color=vec4(0,0,0,1);ivec2 size=ivec2(frame.words[0],frame.words[1]);
 if(any(lessThanEqual(size,ivec2(0)))||any(greaterThan(size,ivec2(1024,512))))return;
 int crop=int(min(frame.words[7],8u));ivec2 content=max(size-2*crop,ivec2(1));
 uint aspect=frame.words[3];ivec2 extent=ivec2(1440,1080);
 if(aspect==1u)extent=ivec2(1920,1080);
 else if(aspect>=2u){float fit=min(1920./float(content.x),1080./float(content.y));if(aspect==3u)fit=max(1.,floor(fit));extent=ivec2(vec2(content)*fit);}
 ivec2 target=ivec2(floor(gl_FragCoord.xy))-(ivec2(1920,1080)-extent)/2;
 if(any(lessThan(target,ivec2(0)))||any(greaterThanEqual(target,extent)))return;
 vec2 source=(vec2(target)+0.5)*vec2(content)/vec2(extent)-0.5+float(crop);ivec2 lo=ivec2(floor(source));vec3 value;
 if(frame.words[4]==1u){vec2 f=fract(source);value=mix(mix(pixel(lo,size),pixel(lo+ivec2(1,0),size),f.x),mix(pixel(lo+ivec2(0,1),size),pixel(lo+ivec2(1,1),size),f.x),f.y);}
 else value=pixel(ivec2((target*content)/extent)+crop,size);
 if(frame.words[5]>0u&&(int(floor(source.y))&1)==1)value*=frame.words[5]==1u?0.84:0.65;
 value*=float(frame.words[6]==0u?100u:clamp(frame.words[6],50u,150u))/100.;color=vec4(clamp(value,0.,1.),1);
}
