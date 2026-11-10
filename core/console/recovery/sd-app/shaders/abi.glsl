// std430 ABI 1. Data starts at byte 1024; bytes 784..959 are app-owned scratch.
#ifndef STATE_ACCESS
#define STATE_ACCESS
#endif
layout(set=0,binding=0,std430) STATE_ACCESS buffer Controls {
    uvec4 raw[4]; vec4 motion; uvec4 state;
    vec4 refresh; uvec4 library; uint touch[164];
    // App-owned scratch. Field names are retained for ABI-1 source compatibility.
    uvec4 game;       // 784
    ivec4 piece;      // 800
    vec4 flow;        // 816
    uvec4 bagMeta;    // 832
    uint bag[8];      // 848
    uint rows[20];    // 880
    vec4 transition;  // 960: fade opacity, requested mode, spare, spare
    uvec4 events;     // 976: sequence, sound kind, frame, cleared-row mask
    uvec4 stats;      // 992: best, direction, lock resets, ghost y
    uvec4 reserved;   // 1008: keep the type atlas aligned at byte 1024
    uint atlas[];
} c;
