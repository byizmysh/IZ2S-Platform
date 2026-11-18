#version 450
#extension GL_GOOGLE_include_directive : require
#define STATE_ACCESS readonly
#define HB_READONLY
#include "state.glsl"
#include "hb.glsl"
layout(location=0) out vec4 color;
void main(){
 vec2 p=(gl_FragCoord.xy-vec2(960,540))/540.0;float phase=float(c.game.x)*.035;
 vec3 status=!hbAvailable()?vec3(.5):hbBusy()?vec3(1,.8,.1):c.game.z==0u?vec3(.25,1,.55):c.game.z==4u?vec3(.15,.6,1):vec3(1,.3,.25);
 float ring=1.-smoothstep(.006,.015,abs(length(p)-.36));float dot=1.-smoothstep(.018,.03,length(p-.36*vec2(cos(phase),sin(phase))));
 float bar=step(abs(p.x),.55)*step(abs(p.y-.65),.025)*step(p.x,float(c.game.w%8u)/8.*1.1-.55);
 color=vec4(mix(vec3(.018,.028,.045),status,max(max(ring*.35,dot),bar)),1);
}
