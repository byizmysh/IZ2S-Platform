// This buffer contains bounded indices, counters and Vulkan indirect arguments.
// It never contains callable addresses, handles or executable memory.
const uint RMAGIC=0x49525231u,RVERSION=2u,RTABLE=512u,RSTRIDE=64u,RMAX=16u;
const uint RCLK=(0x700c80u+240u)/4u,RHZ=31250000u;
layout(set=0,binding=0,std430) buffer Resident {uint w[];} r;
uint row(uint slot){return RTABLE+slot*RSTRIDE;}
bool residentValid(){return uint(r.w.length())>=RTABLE+RMAX*RSTRIDE&&r.w[0]==RMAGIC&&r.w[1]==RVERSION&&r.w[2]>=2u&&r.w[2]<=RMAX;}
