#version 450

layout(location = 0) out vec4 color;
layout(set = 0, binding = 1, std430) readonly buffer Pixels {
    uint words[];
} frame;

void main()
{
    // The paired presenter uses a fixed 1920x1080 viewport and a 960x540
    // producer. A freshly allocated buffer has no published frame yet.
    if (frame.words[0] != 960u || frame.words[1] != 540u) {
        color = vec4(0, 0, 0, 1);
        return;
    }

    uvec2 pixel = uvec2(gl_FragCoord.xy) >> 1u;
    uint rgb = frame.words[1024u + pixel.y * 960u + pixel.x];
    color = vec4(vec3(rgb & 255u, (rgb >> 8u) & 255u, (rgb >> 16u) & 255u) / 255.0, 1);
}
