// Slowly moving analytic colour fields. No images, blur passes or new buffers.
const vec3 WARM=vec3(.99,.76,.94);
vec3 focusBackground(vec2 p){
 vec2 uv=p/vec2(1920,1080);float t=c.motion.x*.085;
 float a=sin(t),b=cos(t*.73);
 vec2 pink=(uv-vec2(.18+.13*a,.20+.10*b))*vec2(.90,1.14);
 vec2 blue=(uv-vec2(.88-.10*b,.66+.12*a))*vec2(.85,1.04);
 vec2 violet=(uv-vec2(.52-.11*a,.49-.09*b))*vec2(1.10,.92);
 float m=1.0-smoothstep(.02,.80,dot(pink,pink));
 float n=1.0-smoothstep(.01,.76,dot(blue,blue));
 float v=1.0-smoothstep(.01,.65,dot(violet,violet));
 vec3 col=vec3(.025,.055,.19);
 col=mix(col,vec3(.53,.13,.52),m*.76);
 col=mix(col,vec3(.075,.21,.63),n*.84);
 col=mix(col,vec3(.37,.13,.64),v*.58);
 float edge=dot((uv-.5)*vec2(.9,1.1),(uv-.5)*vec2(.9,1.1));
 col*=1.0-.28*edge;
 // Sub-byte dither keeps large gradients smooth at 8-bit output.
 float grain=fract(52.9829189*fract(dot(floor(p),vec2(.06711056,.00583715))));
 return col+(grain-.5)/255.0;
}
