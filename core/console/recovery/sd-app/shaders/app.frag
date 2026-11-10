#version 450
#extension GL_GOOGLE_include_directive : require
#include "abi.glsl"
layout(location=0) out vec4 color;void main(){vec2 p=(gl_FragCoord.xy-vec2(960,540))/540.0;float angle=atan(p.y,p.x);float ring=(1.-smoothstep(.025,.055,abs(length(p)-.32)))*step(-2.3,angle);color=vec4(mix(vec3(.025,.055,.08),vec3(.2,.75,1.),ring),1);}
