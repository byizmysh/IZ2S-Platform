#version 450
layout(location = 0) out vec2 uv;
layout(set = 0, binding = 1, std140) uniform Status {
    uvec4 windows;
    vec4 frame[2];
    vec4 content[2];
    uvec4 controls;
    uvec4 titles[2];
} status;
layout(push_constant) uniform Draw { uint pane; } draw;
void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec4 region = draw.pane == 3u ? vec4(0, 934, 1920, 146) :
        status.frame[draw.pane - 1u] + vec4(-3, -3, 6, 6);
    vec2 position = (region.xy + uv * region.zw) / vec2(1920, 1080);
    gl_Position = vec4(2.0 * position - 1.0, 0.0, 1.0);
}
