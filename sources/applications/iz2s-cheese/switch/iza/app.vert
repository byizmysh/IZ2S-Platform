#version 450
layout(location=0) out vec2 storeUv;
void main(){
    const vec2 corners[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    vec2 position=corners[gl_VertexIndex];
    storeUv=position*0.5+0.5;
    gl_Position=vec4(position,0,1);
}
