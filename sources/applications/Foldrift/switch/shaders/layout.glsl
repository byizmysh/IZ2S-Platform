#ifndef STATE_ACCESS
#define STATE_ACCESS
#endif
layout(set=0,binding=0,std430) STATE_ACCESS buffer Controls {
    uvec4 raw[4];
    vec4 motion;
    uvec4 state;
    vec4 refresh;
    uvec4 library;
    uint touch[164];
    uvec4 game;
    ivec4 piece;
    vec4 flow;
    uvec4 bagMeta;
    uint bag[8];
    uint rows[20];
    vec4 transition;
    uvec4 events;
    uvec4 stats;
    uvec4 reserved;
    uint atlas[];
} c;

const uint Deck=(0x7f8000u-1024u)/4u;
const uint DeckReady=0x495a4644u;
const uint PaneStart=1664u,RowStart=144u,RowBytes=80u;
const uint ViewBrowse=0u,ViewActions=1u,ViewConfirm=2u,ViewPreview=3u,ViewKeyboard=4u;
const uint ViewCopying=5u;
const vec2 FilePanelTop=vec2(54,88), FilePanelSize=vec2(1812,846);
const vec2 FileRowsTop=FilePanelTop+vec2(12,132);
const float FileRowPitch=58.0;
const uint FileRowsPerPage=12u;
uint deck(uint offset){return c.atlas[Deck+offset/4u];}
