#version 450
layout(location=0) out vec4 outColor;
layout(location=0) in vec2 storeUv;
layout(set=0,binding=0,std430) readonly buffer AppControls { uint words[]; } ui;
const ivec2 extent = ivec2(1280,720);

uint readPixel(ivec2 position) {
    ivec2 px = clamp(position, ivec2(0), extent-1);
    return ui.words[0x80000u+uint(px.y*extent.x+px.x)];
}

vec3 rgb(uint pixel) {
    return unpackUnorm4x8(pixel).rgb;
}

void main() {
    vec2 position = clamp(storeUv,0.0,1.0)*vec2(extent);
    uint nearest = readPixel(ivec2(position));
    vec3 color = rgb(nearest);
    vec2 center = position-0.5;
    ivec2 base = ivec2(floor(center));
    uint a = readPixel(base);
    uint b = readPixel(base+ivec2(1,0));
    uint c = readPixel(base+ivec2(0,1));
    uint d = readPixel(base+ivec2(1,1));
    // All Store colors are opaque except the text marker (254).
    if (((a & b & c & d) >> 24) == 254u) {
        vec2 fraction = fract(center);
        color = mix(mix(rgb(a),rgb(b),fraction.x),mix(rgb(c),rgb(d),fraction.x),fraction.y);
    }
    outColor = vec4(color,1.0);
}
