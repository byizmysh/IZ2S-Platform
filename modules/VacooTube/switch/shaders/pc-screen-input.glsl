// Native monotonic ticks are published before each render, independent of FPS.
// The existing ACK flags carry classification, so host jitter cannot turn a
// double tap into a game press. The sampled HID masks remain input-owned.
uint streamMenuPressed(uint pressed){
 uint phase=c.atlas[SB+46u]&255u,sequence=c.atlas[SB+47u]&255u,now=c.atlas[SB+60u];bool down=(hbButtons()&HB_MINUS)!=0u,toggle=false,blocked=(c.atlas[SB+46u]&256u)!=0u;
 if(c.atlas[SB+62u]!=31250000u){c.atlas[SB+46u]=0u;c.atlas[SB+24u]&=0x1f00ffffu;return pressed&~HB_MINUS;}
 // Compare the full monotonic counter, including long HOME pauses and wrap.
 uint first=c.atlas[SB+45u],elapsed=now-first,highElapsed=c.atlas[SB+61u]-c.atlas[SB+51u]-(now<first?1u:0u);
 if((phase==1u||phase==2u)&&(highElapsed!=0u||elapsed>=7812500u)){if(!blocked&&c.atlas[SB+25u]==0u&&c.atlas[SB+38u]==0u)sequence=(sequence+1u)&255u;phase=phase==1u&&down?3u:0u;}
 if((pressed&HB_MINUS)!=0u){
  if(phase==2u){phase=4u;toggle=true;}
  else if(phase==0u){c.atlas[SB+45u]=now;c.atlas[SB+51u]=c.atlas[SB+61u];phase=1u;blocked=c.atlas[SB+25u]!=0u||c.atlas[SB+38u]!=0u;}
 }
 if(!down){if(phase==1u)phase=2u;else if(phase==3u||phase==4u)phase=0u;}
 c.atlas[SB+46u]=phase|(blocked?256u:0u);c.atlas[SB+47u]=sequence;
 c.atlas[SB+24u]=(c.atlas[SB+24u]&0x1f00ffffu)|0x80000000u|(phase==1u||phase==2u?0x40000000u:0u)|(phase==3u&&!blocked&&c.atlas[SB+25u]==0u?0x20000000u:0u)|(sequence<<16u);
 return (pressed&~HB_MINUS)|(toggle?HB_MINUS:0u);
}
// Read the independent sensor publication through a coherent alias; retain the
// last complete sampleValid during the sampler's bounded invalidate/commit window.
layout(set=0,binding=0,std430) coherent volatile readonly buffer StreamSensors{uint words[];}streamSensors;
uint streamSensorWord(uint offset){return streamSensors.words[(0x7f0000u+544u+offset)/4u];}
bool streamExitPressed(uint pressed){
 uint now=c.atlas[SB+60u],hi=c.atlas[SB+61u];
 if((pressed&32u)==0u)return false;
 uint lo=c.atlas[SB+104u],beforeHi=c.atlas[SB+105u],elapsed=now-lo,high=hi-beforeHi-(now<lo?1u:0u);
 bool exit=c.atlas[SB+106u]==1u&&high==0u&&elapsed<18750000u;
 c.atlas[SB+104u]=now;c.atlas[SB+105u]=hi;c.atlas[SB+106u]=exit?0u:1u;
 if(exit){c.atlas[CB+1u]=0u;c.atlas[SB+25u]=0u;c.atlas[SB+69u]=0u;c.state.y=hbButtons();}
 return exit;
}
bool streamWithin(uint lo,uint hi,uint beforeLo,uint beforeHi,uint limit){uint delta=lo-beforeLo,high=hi-beforeHi-(lo<beforeLo?1u:0u);return high==0u&&delta<=limit;}
// Read passive mode telemetry already sampled by the platform. Transport ACKs
// and explicit source transitions own visibility; diagnostic age never resets
// the stream, pauses audio or starts a GPU/host generation handshake.
void streamFreshness(bool acceptedFrame){
 uint now=c.atlas[SB+60u],hi=c.atlas[SB+61u],magic=streamSensorWord(0u),lo=streamSensorWord(32u),beforeHi=streamSensorWord(36u);
 uint mode=streamSensorWord(8u),result=streamSensorWord(12u),type=streamSensorWord(16u),status=streamSensorWord(24u);
 memoryBarrierBuffer();
 bool sampleValid=magic==0x495a534eu&&streamSensorWord(4u)==1u&&streamSensorWord(0u)==magic&&streamSensorWord(32u)==lo&&streamSensorWord(36u)==beforeHi&&streamWithin(now,hi,lo,beforeHi,93750000u);
 bool route=sampleValid&&mode<=1u&&result==0u&&status==4u;
 bool host=c.atlas[SB+41u]==1u&&c.atlas[SB+63u]==0u&&c.atlas[CB+1u]==2u&&c.atlas[SB+38u]==0u;
 c.atlas[SB+69u]=host&&c.atlas[SB+3u]!=0u?1u:0u;c.atlas[SB+70u]=1u;
 uint telemetry=sampleValid?((mode&1u)<<4u)|((type&3u)<<5u)|(status==4u?128u:0u):0u;
 c.atlas[SB+24u]=(c.atlas[SB+24u]&0xe0ff000fu)|telemetry|256u|0x10000000u|(c.atlas[SB+69u]==1u?0x01000000u:0u)|(sampleValid?0x02000000u:0u)|(route?0x04000000u:0u)|(host?0x08000000u:0u);
}
void streamLatch(){
 uint pub=c.atlas[SB],w=c.atlas[SB+1u],h=c.atlas[SB+2u],format=c.atlas[SB+6u];
 bool valid=format==0u?w>=2u&&w<=640u&&h>=2u&&h<=360u&&(w&1u)==0u:format==2u?w>=4u&&w<=1280u&&h>=4u&&h<=720u&&(w&3u)==0u&&(h&3u)==0u&&1024u+w*h<=0x78000u:format==1u&&w>=4u&&w<=1920u&&h>=4u&&h<=1080u&&(w&3u)==0u&&(h&3u)==0u&&w*h/2u<=VIDEO_STRIDE*4u;
 if(pub!=0u&&pub!=c.atlas[SB+3u]&&valid){
  if(c.atlas[SB+19u]!=0u&&(c.atlas[SB+16u]!=c.atlas[SB+18u]||c.atlas[SB+17u]!=0u)){
   c.atlas[SB+20u]=1u;c.atlas[SB+8u]=c.atlas[SB+14u];streamFreshness(false);return;
  }
  c.atlas[SB+4u]=w;c.atlas[SB+5u]=h;c.atlas[SB+7u]=format;c.atlas[SB+3u]=pub;
  if(c.atlas[SB+19u]!=0u)c.atlas[SB+8u]=c.atlas[SB+10u+((pub&1u)^1u)*2u];
  streamFreshness(true);
 }else streamFreshness(false);
}
void streamOpen(){c.atlas[SB+69u]=0u;c.atlas[SB+83u]=0u;c.atlas[SB+51u]=0u;c.atlas[SB+45u]=0u;c.atlas[SB+46u]=0u;c.atlas[SB+47u]=0u;c.atlas[SB+24u]&=65535u;c.atlas[SB+40u]=0u;
 c.atlas[CB+1u]=2u;c.atlas[SB+25u]=0u;c.atlas[SB+38u]=1u;c.atlas[SB+39u]=0u;c.atlas[CB+2u]=c.touch[2]>0u?1u:0u;c.transition.xy=vec2(0);
}
void streamCommand(uint action){c.atlas[SB+40u]=action;c.transition.z=float(action);c.transition.w=c.transition.w>=1000000.0?1.0:c.transition.w+1.0;c.atlas[SB+37u]=1u;c.atlas[SB+25u]=0u;c.atlas[SB+38u]=0u;}
void streamInput(uint pressed){
 if(streamExitPressed(pressed))return;
 pressed=streamMenuPressed(pressed);
 bool held=c.touch[2]==1u&&(c.touch[6]&2u)==0u,tap=held&&c.atlas[CB+2u]==0u;vec2 p=vec2(c.touch[8],c.touch[9])*1.5;
 uint menu=c.atlas[SB+25u],sel=c.atlas[SB+39u];bool start=c.atlas[SB+38u]!=0u,fixedSource=c.atlas[SB+40u]==2u||c.atlas[SB+40u]==4u;
 if(start&&menu==0u)menu=1u;
 bool sizeMenu=!start&&(menu==2u||menu==4u||menu==5u),profiles=!start&&menu==3u;uint previousMenu=menu;
 int action=uiMenuInput(pressed,held,tap,p,menu,sel,sizeMenu?7u:(!start&&fixedSource?2u:3u));
 if(!start&&(pressed&HB_B)!=0u){if(sizeMenu){menu=3u;sel=previousMenu==2u?0u:previousMenu==4u?1u:2u;}else if(profiles){menu=1u;sel=1u;}action=-1;}
 if(start&&(pressed&HB_B)!=0u){c.atlas[CB+1u]=0u;c.library.w=held?1u:0u;menu=0u;}
 if(action>=0){
  if(sizeMenu){
   const uint widths[7]=uint[7](0u,640u,832u,960u,1280u,1600u,1920u);uint width=widths[action],command=16u+uint(action);
   if(previousMenu==2u)c.atlas[SB+42u]=width;
   else{bool docked=previousMenu==4u;command=(docked?24u:32u)+uint(action);c.atlas[SB+42u]=0u;c.atlas[SB+(docked?43u:44u)]=action==0?(docked?1920u:1280u):width;}
   c.transition.z=float(command);c.transition.w=c.transition.w>=1000000.0?1.0:c.transition.w+1.0;menu=3u;sel=previousMenu==2u?0u:previousMenu==4u?1u:2u;
  }
  else if(profiles){
   menu=action==0?2u:action==1?4u:5u;sel=0u;const uint widths[7]=uint[7](0u,640u,832u,960u,1280u,1600u,1920u);uint width=c.atlas[SB+(action==0?42u:action==1?43u:44u)],defaultWidth=action==0?0u:action==1?1920u:1280u;
   if(width!=defaultWidth)for(uint i=1u;i<7u;i++)if(widths[i]==width)sel=i;
  }
  else if(start&&action<2){streamCommand(uint(action)+1u);menu=0u;}
  else if(action==2||(!start&&fixedSource&&action==1)){c.atlas[CB+1u]=0u;c.library.w=held?1u:0u;menu=0u;}
  else if(!start&&action==1){menu=3u;sel=0u;}
  else menu=0u;
 }
 c.atlas[SB+25u]=menu;c.atlas[SB+39u]=sel;c.atlas[CB+2u]=held?1u:0u;c.state.y=hbButtons();
}
