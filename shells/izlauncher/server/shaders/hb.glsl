#ifndef HB_SDK_GLSL
#define HB_SDK_GLSL
// Include state.glsl first. Compute owns writes; define STATE_ACCESS readonly in fragment.
const uint HB_A=1u,HB_B=2u,HB_X=4u,HB_Y=8u,HB_L=64u,HB_R=128u,HB_PLUS=1024u,HB_MINUS=2048u;
const uint HB_LEFT=4096u,HB_UP=8192u,HB_RIGHT=16384u,HB_DOWN=32768u;
const uint HB_PAD=1u,HB_MOTION=2u,HB_KEYBOARD=4u,HB_MOUSE=8u,HB_NETWORK=16u,HB_JOY_MOUSE=32u,HB_PAD_SOURCES=64u;
const uint HB_EXT=(0x7f0000u-1024u)/4u;
uint hbWord(uint offset){return c.atlas[HB_EXT+offset/4u];}
float hbFloat(uint offset){return uintBitsToFloat(hbWord(offset));}
bool hbAvailable(){return hbWord(0u)==0x48425344u&&hbWord(4u)==1u;}
bool hbHas(uint flags){return hbAvailable()&&(hbWord(12u)&flags)==flags;}
// A detached Joy-Con can report outside the selected handheld state.
uint hbButtons(){
 uint buttons=c.raw[0].z;
 if(hbHas(HB_PAD_SOURCES))for(uint i=0u;i<5u;i++){
  uint offset=640u+i*64u;
  if((hbWord(offset+32u)&1u)!=0u)buttons|=hbWord(offset+8u);
 }
 return buttons;
}
#ifndef HB_READONLY
uint izPadRead(uint i){return i<16u?c.raw[i/4u][i%4u]:c.atlas[i-256u];}
void izPadWrite(uint i,uint value){if(i<16u)c.raw[i/4u][i%4u]=value;else c.atlas[i-256u]=value;}
#include "pad-merge.glsl"
#endif
uint hbPressed(){return hbButtons()&~c.state.y;}
bool hbConnected(){return (c.raw[2].x&1u)!=0u;}
vec2 hbStick(bool right){ivec2 raw=right?ivec2(c.raw[1].zw):ivec2(c.raw[1].xy);vec2 s=clamp(vec2(raw)/32767.0,-1.0,1.0);float n=length(s);return n<=.15?vec2(0):s/n*min(1.0,(n-.15)/.85);}
uint hbTouchCount(){return min(c.touch[2],16u);}
vec2 hbTouch(uint index){if(index>=hbTouchCount())return vec2(-1);return vec2(c.touch[8u+index*10u],c.touch[9u+index*10u]);}
uint hbMotionCount(){return hbHas(HB_MOTION)?min(hbWord(28u),2u):0u;}
vec3 hbAcceleration(uint sensor){if(sensor>=hbMotionCount())return vec3(0);uint o=128u+sensor*96u+16u;return vec3(hbFloat(o),hbFloat(o+4u),hbFloat(o+8u));}
vec3 hbGyroscope(uint sensor){if(sensor>=hbMotionCount())return vec3(0);uint o=128u+sensor*96u+28u;return vec3(hbFloat(o),hbFloat(o+4u),hbFloat(o+8u));}
mat3 hbDirection(uint sensor){if(sensor>=hbMotionCount())return mat3(1);uint o=128u+sensor*96u+52u;return mat3(vec3(hbFloat(o),hbFloat(o+4u),hbFloat(o+8u)),vec3(hbFloat(o+12u),hbFloat(o+16u),hbFloat(o+20u)),vec3(hbFloat(o+24u),hbFloat(o+28u),hbFloat(o+32u)));}
bool hbKey(uint usage){return hbHas(HB_KEYBOARD)&&usage<256u&&(hbWord(336u+(usage/32u)*4u)&(1u<<(usage%32u)))!=0u;}
vec2 hbMouse(){return hbHas(HB_MOUSE)?vec2(int(hbWord(392u)),int(hbWord(396u))):vec2(0);}
uint hbMouseButtons(){return hbHas(HB_MOUSE)?hbWord(416u):0u;}
bool hbBusy(){return hbWord(2048u)!=hbWord(4096u);}
uint hbResult(){return hbWord(4100u);}
uint hbResponseBytes(){return min(hbWord(4104u),1024u);}
uint hbResponseByte(uint index){return index<hbResponseBytes()?(hbWord(4128u+(index/4u)*4u)>>((index%4u)*8u))&255u:0u;}
// Native optical ring: 8 players x 2 sides. Side 0/1 matches Jcms handles.
// Keep a separate previous sequence for each player/side in your app state.
vec2 hbJoyMouseDelta(uint player,uint side,inout uint previous){
 if(!hbHas(HB_JOY_MOUSE)||player>=8u||side>=2u)return vec2(0);
 uint base=16384u+(player*2u+side)*712u,tail=hbWord(base+16u),count=hbWord(base+24u);
 if(tail>=17u||count>16u)return vec2(0);
 vec2 delta=vec2(0);uint newest=previous;
 for(uint i=0u;i<count;i++){
  uint o=base+32u+((tail+17u-i)%17u)*40u,seq=hbWord(o+16u),high=hbWord(o+20u);
  if(hbWord(o)!=seq*2u||hbWord(o+4u)!=high*2u+(seq>>31u))continue;
  if(int(seq-previous)>0){delta+=vec2(int(hbWord(o+24u)),int(hbWord(o+28u)));if(int(seq-newest)>0)newest=seq;}
 }
 previous=newest;return delta;
}
uint hbJoyMouseAttributes(uint player,uint side){
 if(!hbHas(HB_JOY_MOUSE)||player>=8u||side>=2u)return 0u;
 uint base=16384u+(player*2u+side)*712u,tail=hbWord(base+16u);if(tail>=17u||hbWord(base+24u)==0u)return 0u;
 uint o=base+32u+tail*40u,seq=hbWord(o+16u),high=hbWord(o+20u);
 return hbWord(o)==seq*2u&&hbWord(o+4u)==high*2u+(seq>>31u)?hbWord(o+32u):0u;
}
// Background FTP service owns bytes 8192..8447; app command is at 8448.
bool hbFtpAvailable(){return hbWord(8192u)==0x46545031u;}
uint hbFtpState(){return hbFtpAvailable()?hbWord(8196u):0u;}
uint hbFtpAddress(){return hbWord(8200u);}
uint hbFtpPort(){return hbWord(8204u);}
uint hbFtpClients(){return hbWord(8208u);}
uint hbFtpSent(){return hbWord(8216u);}
uint hbFtpReceived(){return hbWord(8224u);}
uint hbFtpFiles(){return hbWord(8232u);}
bool hbFtpBusy(){return hbWord(8448u)!=hbWord(8236u);}
#ifndef HB_READONLY
bool hbFtpCommand(uint action){
 if(!hbFtpAvailable()||hbFtpBusy()||(action!=1u&&action!=2u))return false;
 uint o=HB_EXT+8448u/4u;c.atlas[o+1u]=action;
 uint sequence=c.atlas[o]+1u;c.atlas[o]=sequence==0u?1u:sequence;return true;
}
void hbEndFrame(){c.state.y=hbButtons();}
void hbExit(){c.reserved.z=1u;}
void hbSound(uint id){c.events.x++;c.events.y=id;}
bool hbRequest(uint op,uint a,uint b,uint d){
 if(!hbAvailable()||hbBusy())return false;
 uint o=HB_EXT+2048u/4u;c.atlas[o+1u]=op;c.atlas[o+2u]=a;c.atlas[o+3u]=b;c.atlas[o+4u]=d;
 uint sequence=c.atlas[o]+1u;c.atlas[o]=sequence==0u?1u:sequence;return true;
}
bool hbVibrate(uint side,uint milliseconds,uint amplitudePermille){return hbRequest(1u,side,milliseconds,amplitudePermille);}
void hbHapticStep(uint index,uint milliseconds,vec4 value){
 if(index>=32u||hbBusy())return;uint o=HB_EXT+2080u/4u+index*5u;c.atlas[o]=milliseconds;
 c.atlas[o+1u]=floatBitsToUint(value.x);c.atlas[o+2u]=floatBitsToUint(value.y);c.atlas[o+3u]=floatBitsToUint(value.z);c.atlas[o+4u]=floatBitsToUint(value.w);
}
bool hbPlayHaptics(uint side,uint steps){return steps>0u&&steps<=32u&&hbRequest(4u,side,steps,0u);}
bool hbScanNfc(){return hbRequest(2u,0u,0u,0u);}
// Fill request payload before hbSendUdp. Call only when !hbBusy().
void hbRequestByte(uint index,uint value){if(index>=1024u||hbBusy())return;uint o=HB_EXT+(2080u/4u)+index/4u,shift=(index%4u)*8u;c.atlas[o]=(c.atlas[o]&~(255u<<shift))|((value&255u)<<shift);}
bool hbSendUdp(uint endpoint,uint bytes){return bytes>0u&&bytes<=1024u&&hbRequest(3u,endpoint,bytes,0u);}
#endif
#endif
