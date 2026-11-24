// The cover tint is interpolated once per frame in iz2Animate.
vec3 focusBackground(vec2 p){
 vec2 uv=p/vec2(1920,1080);
 bool ready=c.atlas[IZ2_ANIM]==IZ2_ANIM_MAGIC;
 vec3 tint=ready?uintBitsToFloat(uvec3(c.atlas[IZ2_TINT],c.atlas[IZ2_TINT+1u],c.atlas[IZ2_TINT+2u])):IZ2_DEFAULT_TINT;
 float origin=ready?uintBitsToFloat(c.atlas[IZ2_TINT_ORIGIN]):.37;
 vec2 sky=(uv-vec2(.79,.14))/vec2(.78,1.14);
 vec3 col=mix(vec3(.035,.047,.10),mix(vec3(.16,.23,.49),tint*.40,.40),1.0-smoothstep(0.0,1.0,length(sky)));
 vec2 wash=(uv-vec2(origin,.34))/vec2(.52,.64);
 col=mix(col,tint,.19*(1.0-smoothstep(0.0,.87,length(wash))));

 // Parallel blue ribbons cross the color field; their hue stays tied to IZ2S.
 float t=c.motion.x;
 float path=.87-.83*uv.x+.14*sin(uv.x*5.0+t*.65);
 float spread=.14+.095*sin(uv.x*6.0-t*.76);
 float band=(uv.y-path)/spread+.5;
 float lane=band*24.0;
 float strand=abs(fract(lane+.5)-.5);
 float aa=max(fwidth(lane),.025);
 float ribbon=(1.0-smoothstep(.04,.04+aa,strand))*smoothstep(-.01,.04,band)*(1.0-smoothstep(.96,1.01,band));
 float strength=mix(.28,.65,smoothstep(.08,.84,uv.x));
 col=mix(col,vec3(.39,.55,1),ribbon*strength);
 col=mix(col,vec3(.025,.034,.078),smoothstep(.72,1.0,uv.y)*.65);
 float grain=fract(52.9829189*fract(dot(floor(p),vec2(.06711056,.00583715))));
 return col+(grain-.5)/255.0;
}
