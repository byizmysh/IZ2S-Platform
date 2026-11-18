#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) out vec4 outColor;
#define STATE_ACCESS readonly
#define HB_READONLY
#define VACOO_THEME
#include "abi.glsl"
#include "hb.glsl"
#include "common.glsl"
#include "menu-draw.glsl"
#include "pc-screen.glsl"
#include "pc-screen-draw.glsl"
#include "standalone.glsl"
void main(){outColor=vec4(mediaScreen(gl_FragCoord.xy),1);}
