#version 450
layout(set = 0, binding = 0) uniform sampler2D pane;
layout(set = 0, binding = 1, std140) uniform Status {
    uvec4 windows;
    vec4 frame[2];
    vec4 content[2];
    uvec4 controls;
    uvec4 titles[2];
    uvec4 font[960]; // 64 printable ASCII glyphs, 12 x 20 coverage bytes each.
} status;
layout(push_constant) uniform Draw { uint pane; } draw;
layout(location = 0) out vec4 color;

const uvec2 labelRange[12] = uvec2[](
    uvec2(0u, 5u) , // FOCUS
    uvec2(5u, 5u) , // AUDIO
    uvec2(10u, 4u) , // SWAP
    uvec2(14u, 5u) , // FLOAT
    uvec2(19u, 7u) , // SMALLER
    uvec2(26u, 6u) , // LARGER
    uvec2(32u, 6u) , // CORNER
    uvec2(38u, 9u) , // END SPLIT
    uvec2(47u, 12u) , // SIDE BY SIDE
    uvec2(59u, 15u) , // WINDOW CONTROLS
    uvec2(74u, 39u) , // D-PAD SELECT    A APPLY    + AND - HIDE
    uvec2(113u, 8u) // FLOATING
);
const uint labelText[121] = uint[](
    70u, 79u, 67u, 85u, 83u,
    65u, 85u, 68u, 73u, 79u,
    83u, 87u, 65u, 80u,
    70u, 76u, 79u, 65u, 84u,
    83u, 77u, 65u, 76u, 76u, 69u, 82u,
    76u, 65u, 82u, 71u, 69u, 82u,
    67u, 79u, 82u, 78u, 69u, 82u,
    69u, 78u, 68u, 32u, 83u, 80u, 76u, 73u, 84u,
    83u, 73u, 68u, 69u, 32u, 66u, 89u, 32u, 83u, 73u, 68u, 69u,
    87u, 73u, 78u, 68u, 79u, 87u, 32u, 67u, 79u, 78u, 84u, 82u, 79u, 76u, 83u,
    68u, 45u, 80u, 65u, 68u, 32u, 83u, 69u, 76u, 69u, 67u, 84u, 32u, 32u, 32u, 32u, 65u, 32u, 65u, 80u, 80u, 76u, 89u, 32u, 32u, 32u, 32u, 43u, 32u, 65u, 78u, 68u, 32u, 45u, 32u, 72u, 73u, 68u, 69u,
    70u, 76u, 79u, 65u, 84u, 73u, 78u, 71u
);

const vec3 azure = vec3(0.42, 0.75, 0.99);
const vec3 ink = vec3(0.94, 0.96, 1.0);
const vec3 secondary = vec3(0.60, 0.67, 0.76);
const vec3 mint = vec3(0.53, 0.93, 0.76);

bool inside(vec2 p, vec4 r) {
    return all(greaterThanEqual(p, r.xy)) && all(lessThan(p, r.xy + r.zw));
}
float rounded(vec2 p, vec2 center, vec2 halfSize, float radius) {
    vec2 q = abs(p - center) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}
float mesh(float distance) { return 1.0 - smoothstep(-0.65, 0.65, distance); }
float lineTo(vec2 p, vec2 a, vec2 b) {
    vec2 ab = b - a;
    vec2 closest = a + ab * clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    return mesh(length(p - closest) - 1.25);
}
float glyph(uint ch, vec2 point) {
    if (ch < 32u || ch > 95u || any(lessThan(point, vec2(0))) ||
        any(greaterThanEqual(point, vec2(12, 20)))) return 0.;
    uvec2 p = uvec2(point);
    uint index = (ch - 32u) * 240u + p.y * 12u + p.x;
    uint word = status.font[index / 16u][(index / 4u) % 4u];
    return float((word >> ((index % 4u) * 8u)) & 255u) / 255.;
}
float text(uint label, vec2 point, float scale) {
    point /= scale;
    if (point.x < 0. || point.y < 0. || point.y >= 20.) return 0.;
    uvec2 range = labelRange[label];
    uint column = uint(point.x / 12.);
    if (column >= range.y) return 0.;
    return glyph(labelText[range.x + column], vec2(point.x - float(column) * 12., point.y));
}
float titleText(uint slot, vec2 point) {
    if (point.x < 0. || point.y < 0. || point.y >= 20.) return 0.;
    uint column = uint(point.x / 12.);
    if (column >= 16u) return 0.;
    uint word = status.titles[slot][column / 4u];
    return glyph((word >> ((column % 4u) * 8u)) & 255u,
                 vec2(point.x - float(column) * 12., point.y));
}
float speaker(vec2 p, bool audible) {
    float body = max(mesh(rounded(p, vec2(-6, 0), vec2(3.5, 4), 0.8)),
                  mesh(max(max(-4.0 - p.x, p.x - 2.5), abs(p.y) - (5.0 + p.x * 0.6))));
    if (!audible) return max(body, lineTo(p, vec2(-10, 9), vec2(9, -9)));
    float ring = min(abs(length(p) - 7.), abs(length(p) - 12.));
    float waves = mesh(max(ring - 1., max(3. - p.x, abs(p.y) - 9.)));
    return max(body, waves);
}
float icon(uint id, vec2 p) {
    if (id == 0u) {
        float rim = mesh(abs(length(p) - 10.) - 1.3);
        return max(rim, mesh(length(p) - 3.));
    }
    if (id == 1u) return speaker(p, status.windows.y != 0u);
    if (id == 2u) {
        float top = max(lineTo(p,vec2(-11,-4),vec2(11,-4)), lineTo(p,vec2(11,-4),vec2(6,-9)));
        float bottom = max(lineTo(p,vec2(11,4),vec2(-11,4)), lineTo(p,vec2(-11,4),vec2(-6,9)));
        return max(top, bottom);
    }
    if (id == 3u) {
        float outer = mesh(abs(rounded(p,vec2(0),vec2(12,9),2.5)) - 1.15);
        float middle = status.windows.w == 0u ?
            mesh(rounded(p,vec2(5,3),vec2(4,3),0.6)) : lineTo(p,vec2(-2,-8),vec2(-2,8));
        return max(outer, middle);
    }
    if (id == 4u || id == 5u) {
        float h = lineTo(p,vec2(-8,0),vec2(8,0));
        return id == 4u ? h : max(h, lineTo(p,vec2(0,-8),vec2(0,8)));
    }
    if (id == 6u) {
        float outline = mesh(abs(rounded(p,vec2(0),vec2(11,9),2.5)) - 1.15);
        return max(outline, mesh(rounded(p,vec2(6,-4),vec2(3,2),0.7)));
    }
    return max(lineTo(p,vec2(-7,-7),vec2(7,7)),lineTo(p,vec2(-7,7),vec2(7,-7)));
}

vec3 menu(vec2 p) {
    float panel = rounded(p, vec2(960,1005), vec2(888,68), 22.0);
    vec3 c = mix(vec3(0.029,0.035,0.050), vec3(0.072,0.087,0.121),
                 clamp((1073. - p.y) / 136., 0., 1.));
    c *= mesh(panel);
    c = mix(c, vec3(0.20,0.24,0.32), mesh(abs(panel) - 0.65));
    c = mix(c, azure, mesh(rounded(p, vec2(101,966), vec2(3,11), 3.)));
    c = mix(c, ink, text(9u, p - vec2(116,951), 1.15));
    uint layoutLabel = status.windows.w == 0u ? 8u : 11u;
    float tagWidth = 12. * float(labelRange[layoutLabel].y) + 20.;
    c = mix(c, vec3(0.111,0.18,0.25), mesh(rounded(p, vec2(358. + tagWidth / 2., 965), vec2(tagWidth / 2.,13), 10.)));
    c = mix(c, azure, text(layoutLabel, p - vec2(368,955), 1.));
    float hintWidth = 12. * float(labelRange[10].y);
    c = mix(c, secondary, text(10u, p - vec2(1824. - hintWidth,955), 1.));

    int index = int(floor((p.x - 97.) / 217.));
    if (index < 0 || index >= 8 || p.y < 994. || p.y >= 1058.) return c;
    uint id = uint(index);
    vec2 local = p - vec2(97. + 217. * float(index), 995.);
    float d = rounded(local, vec2(103.5,31), vec2(103.5,31), 12.);
    bool disabled = (status.windows.z != 3u && id >= 2u && id <= 6u) ||
                    (status.windows.w == 0u && id >= 4u && id <= 6u);
    bool selected = status.controls.x == id;
    vec3 accent = id == 1u && status.windows.y != 0u ? mint : id == 7u ? vec3(1.,0.62,0.62) : azure;
    vec3 tile = selected ? vec3(0.12,0.20,0.29) : vec3(0.081,0.098,0.132);
    c = mix(c, tile, mesh(d));
    c = mix(c, selected ? accent : vec3(0.19,0.23,0.30), mesh(abs(d) - (selected ? 1.1 : 0.55)));
    c = mix(c, vec3(0.032,0.042,0.062), mesh(rounded(local,vec2(29,31),vec2(17,17),7.)));
    vec3 foreground = disabled ? vec3(0.33,0.39,0.47) : ink;
    c = mix(c, disabled ? foreground : accent, icon(id, local - vec2(29,31)));
    uint buttonLabel = id == 3u && status.windows.w != 0u ? 8u : id;
    c = mix(c, foreground, text(buttonLabel, local - vec2(57,20), 1.));
    return c;
}

void main() {
    vec2 p = gl_FragCoord.xy;
    bool controlsOpen = status.controls.y != 0u;
    if (draw.pane == 3u) {
        if (!controlsOpen || p.y < 934.) discard;
        color = vec4(menu(p), 1.);
        return;
    }
    uint slot = draw.pane - 1u;
    vec4 f = status.frame[slot];
    vec4 inner = status.content[slot];
    if (status.windows.w != 0u && status.windows.z == 3u && slot != status.controls.z) {
        vec4 foreground = controlsOpen ? status.frame[status.controls.z] : status.content[status.controls.z];
        if (inside(p, foreground)) discard;
    }
    if (!controlsOpen) {
        if (!inside(p, inner)) discard;
        color = vec4(texture(pane, (p - inner.xy) / inner.zw).rgb, 1.);
        return;
    }
    if (!inside(p, f + vec4(-3,-3,6,6))) discard;
    bool focused = status.windows.x == draw.pane;
    float boundary = rounded(p, f.xy + f.zw / 2., f.zw / 2., 5.);
    if (boundary > 0. && !focused) discard;
    vec3 c = vec3(0.029,0.040,0.058);
    if (inside(p,inner)) c = texture(pane, (p - inner.xy) / inner.zw).rgb;
    if (p.y < f.y + 32.) {
        c = focused ? vec3(0.09,0.15,0.225) : vec3(0.06,0.08,0.114);
        c = mix(c, focused ? ink : secondary, titleText(slot, p - f.xy - vec2(12,6)));
        if (status.windows.w != 0u && slot == status.controls.z) {
            vec2 grip = p - vec2(f.x + f.z / 2., f.y + 16.);
            float handle = max(mesh(rounded(grip,vec2(0,-3),vec2(17,0.8),0.8)),
                               mesh(rounded(grip,vec2(0,3),vec2(17,0.8),0.8)));
            c = mix(c, secondary, handle);
        }
        bool audible = status.windows.y == draw.pane;
        c = mix(c, audible ? mint : secondary,
                speaker(p - vec2(f.x + f.z - 22., f.y + 16.), audible));
    }
    vec3 edge = focused ? mix(azure * 0.60, azure, clamp(1. - (p.y - f.y) / f.w, 0., 1.)) : vec3(0.18,0.22,0.29);
    c = mix(c, edge, mesh(abs(boundary + 1.) - (focused ? 1.3 : 0.65)));
    if (boundary > 0.) c *= 1.0 - smoothstep(0., 3., boundary);
    color = vec4(c,1.);
}
