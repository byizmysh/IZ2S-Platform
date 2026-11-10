#include "settings.h"
#include "platform.h"
#include "fs-file.h"
#include "sd-layout.h"
#include <string.h>
Ps1Preferences *ps1_active_settings;
const char*const ps1_categories[]={"VIDEO","AUDIO","EMULATION","CONTROLS","SYSTEM","ABOUT"};
#define OPT(label,help,cat,initial,restart,...) {label,help,{__VA_ARGS__},cat,sizeof((const char*[]){__VA_ARGS__})/sizeof(char*),initial,restart}
const Ps1Option ps1_options[]={
 OPT("Display FPS limit","Limits picture updates. game speed and audio stay native.",PS1_VIDEO,0,0,"NATIVE","20 FPS","25 FPS","30 FPS","40 FPS","50 FPS","60 FPS"),
 OPT("Aspect ratio","4:3 is the original TV shape. pixel uses square pixels.",PS1_VIDEO,0,0,"4:3","16:9","PIXEL","INTEGER"),
 OPT("Image filter","Nearest keeps sharp pixels. linear softens the image.",PS1_VIDEO,0,0,"NEAREST","LINEAR"),
 OPT("Scanlines","Add subtle horizontal crt lines to the game image.",PS1_VIDEO,0,0,"OFF","LIGHT","STRONG"),
 OPT("Brightness","Adjust the final image without changing game colours.",PS1_VIDEO,2,0,"80%","90%","100%","110%","120%"),
 OPT("Overscan crop","Hide up to eight pixels around the game image.",PS1_VIDEO,0,0,"OFF","4 PX","8 PX"),
 OPT("Show performance","Show emulation and picture update rates in game.",PS1_VIDEO,0,0,"OFF","ON"),
 OPT("Master volume","Set output volume. the original game audio is unchanged.",PS1_SOUND,4,0,"0%","25%","50%","75%","100%"),
 OPT("Mute","Silence output while keeping audio timing active.",PS1_SOUND,0,0,"OFF","ON"),
 OPT("Mono output","Mix the left and right channels into both speakers.",PS1_SOUND,0,0,"OFF","ON"),
 OPT("Audio buffer","Smooth adds a larger startup cushion. fast reduces delay.",PS1_SOUND,2,0,"FAST","BALANCED","SMOOTH"),
 OPT("Spu reverb","Emulate the playstation reverb effects.",PS1_SOUND,1,1,"OFF","ON"),
 OPT("Interpolation","Gaussian sounds closest to original playstation audio.",PS1_SOUND,0,1,"SIMPLE","GAUSSIAN","CUBIC","OFF"),
 OPT("Cd music","Play CD audio tracks. xa audio remains enabled.",PS1_SOUND,1,1,"OFF","ON"),
 OPT("Cpu engine","Dynarec is fast. interpreter is a slow compatibility option.",PS1_EMULATION,0,1,"DYNAREC","INTERPRETER"),
 OPT("Ps1 CPU clock","57% is the core default. higher values can affect timing.",PS1_EMULATION,0,1,"57%","75%","100%"),
 OPT("Console region","Auto uses the disc region. a wrong region changes speed.",PS1_EMULATION,0,1,"AUTO","NTSC","PAL"),
 OPT("Bios boot logo","The original BIOS logo may reduce game compatibility.",PS1_EMULATION,0,1,"OFF","ON"),
 OPT("Dithering","Keep the original colour dithering used by many PS1 games.",PS1_EMULATION,1,1,"OFF","ON"),
 OPT("Interlaced video","Auto follows the core. off reduces combing in many games.",PS1_EMULATION,0,1,"AUTO","ON","OFF"),
 OPT("Swap A / B","Swap the two face buttons in game. Emulator menus keep their usual controls.",PS1_CONTROLS,0,0,"OFF","ON"),
 OPT("Stick as D-pad","Use the left stick as the digital playstation d-pad.",PS1_CONTROLS,1,0,"OFF","ON"),
 OPT("Stick deadzone","A larger deadzone helps with a drifting left stick.",PS1_CONTROLS,1,0,"SMALL","MEDIUM","LARGE"),
 OPT("Turbo cross","Pulse cross while its button is held. off uses normal input.",PS1_CONTROLS,0,0,"OFF","5 HZ","10 HZ","15 HZ"),
 OPT("Remember last game","Y in the library opens the last game you played.",PS1_SYSTEM,1,0,"OFF","ON"),
 OPT("File extensions","Show or hide .CUE, .BIN and .pbp in the library.",PS1_SYSTEM,0,0,"HIDE","SHOW"),
 OPT("Menu accent","Choose a colour for highlights and menu controls.",PS1_SYSTEM,0,0,"ICE","BLUE","VIOLET","AMBER"),
 OPT("Card save delay","Save dirty cards after this delay. pause always saves.",PS1_SYSTEM,1,0,"IMMEDIATE","0.5 SEC","1 SEC","2 SEC")
};
_Static_assert(sizeof ps1_options/sizeof ps1_options[0]==PS1_OPTION_COUNT,"Option table");
static uint32_t*controls(Ps1Preferences*p,unsigned player){return player==0?p->value+PS1_SWAP_AB:player==1?p->player2:p->extra_player[player-2];}
void ps1_preferences_default(Ps1Preferences*p){
 memset(p,0,sizeof *p);for(unsigned i=0;i<PS1_OPTION_COUNT;i++)p->value[i]=ps1_options[i].initial;
 for(unsigned player=1;player<PS1_PLAYER_COUNT;player++)for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)controls(p,player)[i]=ps1_options[PS1_SWAP_AB+i].initial;
}
int ps1_preferences_valid(const Ps1Preferences*p){
 if(!p||p->multitap>=PS1_MULTITAP_COUNT)return 0;
 for(unsigned i=0;i<PS1_OPTION_COUNT;i++)if(p->value[i]>=ps1_options[i].count)return 0;
 for(unsigned player=0;player<PS1_PLAYER_COUNT;player++){
  for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)if(controls((Ps1Preferences*)p,player)[i]>=ps1_options[PS1_SWAP_AB+i].count)return 0;
  if(p->controller_device[player]>=PS1_DEVICE_COUNT)return 0;
  if(p->controller_device[player]>=PS1_DEVICE_PAD1)for(unsigned other=0;other<player;other++)if(p->controller_device[other]==p->controller_device[player])return 0;
 }
 size_t n=0;while(n<sizeof p->recent&&p->recent[n])n++;return n<sizeof p->recent&&(!n||ps1_sd_path_valid(IZPSX_SD_GAMES,p->recent));
}
int ps1_setting_change(Ps1Settings*s,unsigned id,int direction){if(!s||id>=PS1_OPTION_COUNT||!direction)return 0;unsigned n=ps1_options[id].count,v=s->prefs.value[id];s->prefs.value[id]=direction>0?(v+1)%n:(v+n-1)%n;s->dirty=1;return 1;}
void ps1_settings_reset_category(Ps1Settings*s,unsigned cat){for(unsigned i=0;i<PS1_OPTION_COUNT;i++)if(ps1_options[i].category==cat&&s->prefs.value[i]!=ps1_options[i].initial){s->prefs.value[i]=ps1_options[i].initial;s->dirty=1;}}
uint32_t ps1_control_value(const Ps1Preferences*p,unsigned player,unsigned option){if(!p||player>=PS1_PLAYER_COUNT||option<PS1_SWAP_AB||option>=PS1_SWAP_AB+PS1_CONTROL_COUNT)return 0;return controls((Ps1Preferences*)p,player)[option-PS1_SWAP_AB];}
int ps1_control_change(Ps1Settings*s,unsigned player,unsigned option,int direction){
 if(!s||player>=PS1_PLAYER_COUNT||option<PS1_SWAP_AB||option>=PS1_SWAP_AB+PS1_CONTROL_COUNT||!direction)return 0;
 uint32_t*v=controls(&s->prefs,player)+option-PS1_SWAP_AB;unsigned n=ps1_options[option].count;*v=direction>0?(*v+1)%n:(*v+n-1)%n;s->dirty=1;return 1;
}
void ps1_controls_reset(Ps1Settings*s,unsigned player){if(!s||player>=PS1_PLAYER_COUNT)return;uint32_t*v=controls(&s->prefs,player);for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)if(v[i]!=ps1_options[PS1_SWAP_AB+i].initial){v[i]=ps1_options[PS1_SWAP_AB+i].initial;s->dirty=1;}}
const char*ps1_controller_device_name(unsigned device){static const char*const names[]={"AUTO","OFF","CONTROLLER 1","CONTROLLER 2","CONTROLLER 3","CONTROLLER 4","CONTROLLER 5","CONTROLLER 6","CONTROLLER 7","CONTROLLER 8","HANDHELD"};return device<PS1_DEVICE_COUNT?names[device]:"INVALID";}
int ps1_controller_device_change(Ps1Settings*s,unsigned player,int direction){
 if(!s||player>=PS1_PLAYER_COUNT||!direction)return 0;unsigned value=s->prefs.controller_device[player];int used;
 do{value=direction>0?(value+1)%PS1_DEVICE_COUNT:(value+PS1_DEVICE_COUNT-1)%PS1_DEVICE_COUNT;used=0;
  if(value>=PS1_DEVICE_PAD1)for(unsigned i=0;i<PS1_PLAYER_COUNT;i++)if(i!=player&&s->prefs.controller_device[i]==value)used=1;
 }while(used);s->prefs.controller_device[player]=value;s->dirty=1;return 1;
}
const char*ps1_multitap_name(unsigned mode){static const char*const names[]={"OFF - 2 PLAYERS","PORT 1 - 4 PLAYERS","PORT 2 - 4 PLAYERS"};return mode<PS1_MULTITAP_COUNT?names[mode]:"INVALID";}
int ps1_multitap_change(Ps1Settings*s,int direction){if(!s||!direction)return 0;unsigned v=s->prefs.multitap;s->prefs.multitap=direction>0?(v+1)%PS1_MULTITAP_COUNT:(v+PS1_MULTITAP_COUNT-1)%PS1_MULTITAP_COUNT;s->dirty=1;return 1;}
unsigned ps1_display_limit(const Ps1Preferences*p){static const unsigned rates[]={0,20,25,30,40,50,60};return p&&p->value[PS1_FPS]<7?rates[p->value[PS1_FPS]]:0;}
const char*ps1_core_setting(const char*k){
 Ps1Preferences*p=ps1_active_settings;if(!p)return 0;
 if(!strcmp(k,"pcsx_rearmed_multitap")){static const char*const v[]={"disabled","port 1","port 2"};return v[p->multitap<PS1_MULTITAP_COUNT?p->multitap:0];}
 if(!strcmp(k,"pcsx_rearmed_psxclock")){static const char*const v[]={"57","75","100"};return v[p->value[PS1_CPU_CLOCK]];}
 if(!strcmp(k,"pcsx_rearmed_region")){static const char*const v[]={"auto","NTSC","PAL"};return v[p->value[PS1_REGION]];}
 if(!strcmp(k,"pcsx_rearmed_spu_interpolation")){static const char*const v[]={"simple","gaussian","cubic","off"};return v[p->value[PS1_INTERPOLATION]];}
 if(!strcmp(k,"pcsx_rearmed_neon_interlace_enable_v2")){static const char*const v[]={"auto","enabled","disabled"};return v[p->value[PS1_INTERLACE]];}
 int id=!strcmp(k,"pcsx_rearmed_show_bios_bootlogo")?PS1_BOOT_LOGO:!strcmp(k,"pcsx_rearmed_dithering")?PS1_DITHER:!strcmp(k,"pcsx_rearmed_spu_reverb")?PS1_REVERB:!strcmp(k,"pcsx_rearmed_nocdaudio")?PS1_CD_AUDIO:-1;
 return id<0?0:p->value[id]?"enabled":"disabled";
}
uint32_t ps1_input_settings(const Ps1Preferences*p,uint32_t b,const int32_t a[2],uint64_t now,uint64_t hz){
 return ps1_input_player_settings(p,0,b,a,now,hz);
}
uint32_t ps1_input_player_settings(const Ps1Preferences*p,unsigned player,uint32_t b,const int32_t a[2],uint64_t now,uint64_t hz){
 if(!p)return b;if(player>=PS1_PLAYER_COUNT||p->controller_device[player]==PS1_DEVICE_OFF)return 0;
 unsigned stick=ps1_control_value(p,player,PS1_STICK_DPAD),deadzone=ps1_control_value(p,player,PS1_DEADZONE),swap=ps1_control_value(p,player,PS1_SWAP_AB),turbo=ps1_control_value(p,player,PS1_TURBO);
 if(stick&&a){static const int zone[]={6000,12000,18000};int z=zone[deadzone<3?deadzone:1];if(a[0]<-z)b|=1u<<6;if(a[0]>z)b|=1u<<7;if(a[1]<-z)b|=1u<<5;if(a[1]>z)b|=1u<<4;}
 if(swap){uint32_t x=b&1,y=(b>>8)&1;b=(b&~257u)|(x<<8)|y;}
 if(turbo&&hz){unsigned rate=turbo*5;uint64_t slice=hz/(rate*2);if(slice&&(now/slice&1))b&=~1u;}
 return b;
}
int ps1_frame_clock_due(Ps1FrameClock*c,uint64_t now,uint64_t hz,uint32_t rate){
 if(!c||!hz||!rate||rate>hz)return 0;
 if(!c->started||c->rate!=rate){c->started=1;c->rate=rate;c->next=now;c->remainder=0;}
 if((int64_t)(now-c->next)<0)return 0;
 uint64_t step=hz/rate;if(now-c->next>step*2){c->next=now;c->remainder=0;}
 c->next+=step;c->remainder+=hz%rate;if(c->remainder>=rate){c->next++;c->remainder-=rate;}return 1;
}

/* Small dual-generation settings journal. A previous valid copy survives a
 * failed/torn write. Card data and game images are never touched here. */
#define HEADER 64u
#define BYTES (HEADER+sizeof(Ps1Preferences))
#define LEGACY_PAYLOAD ((28u*4u+PS1_SD_PATH_MAX+3u)&~3u)
#define LEGACY_BYTES (HEADER+LEGACY_PAYLOAD)
#define V2_PAYLOAD (LEGACY_PAYLOAD+6u*4u)
#define V2_BYTES (HEADER+V2_PAYLOAD)
_Static_assert(PS1_OPTION_COUNT==28&&offsetof(Ps1Preferences,player2)==LEGACY_PAYLOAD,"Version-1 settings prefix");
_Static_assert(offsetof(Ps1Preferences,controller_device)+8==V2_PAYLOAD,"Version-2 settings prefix");
#define BAD_SETTINGS 0xf301u
static const char*const files[]={IZPSX_SD_ROOT "/settings.a.izcfg",IZPSX_SD_ROOT "/settings.b.izcfg"};
static uint64_t hash(const void*p,size_t n){const uint8_t*b=p;uint64_t h=1469598103934665603ull;while(n--){h^=*b++;h*=1099511628211ull;}return h;}
static uint64_t get(const uint8_t*b,unsigned at){uint64_t n;memcpy(&n,b+at,8);return n;}
static void put(uint8_t*b,unsigned at,uint64_t n){memcpy(b+at,&n,8);}
static int decode(const uint8_t*b,size_t bytes,Ps1Preferences*p){
 if(bytes!=BYTES&&bytes!=LEGACY_BYTES&&bytes!=V2_BYTES)return 0;
 size_t payload=bytes-HEADER;uint64_t version=get(b,8);
 if(memcmp(b,"IZPSXCFG",8)||!get(b,16)||get(b,24)!=payload||get(b,32)!=hash(b+HEADER,payload)||get(b,40)!=hash(b,40))return 0;
 if(!((version==1&&payload==LEGACY_PAYLOAD)||(version==2&&payload==V2_PAYLOAD)||(version==3&&payload==sizeof *p)))return 0;
 ps1_preferences_default(p);memcpy(p,b+HEADER,payload);
 if(version==1)memcpy(p->player2,p->value+PS1_SWAP_AB,sizeof p->player2);
 if(version<3)for(unsigned player=2;player<PS1_PLAYER_COUNT;player++)memcpy(controls(p,player),p->value+PS1_SWAP_AB,PS1_CONTROL_COUNT*4);
 return ps1_preferences_valid(p);
}
static uint32_t read(Ps1SdDirectory*sd,uint32_t h,uint8_t*b,uint32_t bytes){Ps1FileIO io={.send=(uintptr_t)sd->send,.buffer=(uintptr_t)sd->buffer,.capacity=sd->capacity};ps1_ifile_reset();return ps1_ifile_read(&io,h,0,b,bytes,bytes);}
static uint32_t close(Ps1SdDirectory*sd,uint32_t h,uint32_t rc){uint32_t c=ps1_sd_closefile(sd,h);ps1_ifile_reset();return rc?rc:c;}
uint32_t ps1_settings_load(Ps1Settings*s,Ps1SdDirectory*sd){
 memset(s,0,sizeof *s);ps1_preferences_default(&s->prefs);uint8_t b[BYTES];unsigned bad=0;
 for(unsigned i=0;i<2;i++){
  uint32_t h=0;uint64_t size=0;uint32_t rc=ps1_sd_openfile(sd,IZPSX_SD_ROOT,files[i],&h,&size);if((rc&0x3fffffu)==0x202u)continue;if(rc)return rc;
  if(size!=BYTES&&size!=LEGACY_BYTES&&size!=V2_BYTES){bad++;rc=close(sd,h,0);if(rc)return rc;continue;}
  rc=read(sd,h,b,(uint32_t)size);rc=close(sd,h,rc);if(rc)return rc;
  Ps1Preferences decoded;if(!decode(b,(size_t)size,&decoded)){bad++;continue;}uint64_t gen=get(b,16);
  if(gen==s->generation&&gen&&memcmp(&s->prefs,&decoded,sizeof s->prefs))return BAD_SETTINGS;
  if(gen>s->generation){s->prefs=decoded;s->generation=gen;s->active=i;}
 }
 if(bad)s->warning=BAD_SETTINGS;sd->error=sd->transport=0;return 0;
}
uint32_t ps1_settings_save(Ps1Settings*s,Ps1SdDirectory*sd){
 if(!s->dirty)return 0;if(!ps1_preferences_valid(&s->prefs)||s->generation==UINT64_MAX)return BAD_SETTINGS;
 unsigned target=s->generation?s->active^1u:0;uint32_t h=0;uint64_t size=0;uint8_t b[BYTES];uint32_t rc=ps1_sd_openfile_mode(sd,IZPSX_SD_ROOT,files[target],3,&h,&size);
 if((rc&0x3fffffu)==0x202u){rc=ps1_sd_createfile(sd,IZPSX_SD_ROOT,files[target],BYTES);if(!rc)rc=ps1_sd_openfile_mode(sd,IZPSX_SD_ROOT,files[target],3,&h,&size);}if(rc)return rc;
 if(size!=BYTES)rc=ps1_sd_resizefile(sd,h,BYTES);
 memset(b,0,sizeof b);if(!rc)rc=ps1_sd_writefile(sd,h,0,b,HEADER);if(!rc)rc=ps1_sd_flushfile(sd,h);
 memcpy(b+HEADER,&s->prefs,sizeof s->prefs);if(!rc)rc=ps1_sd_writefile(sd,h,HEADER,b+HEADER,sizeof s->prefs);if(!rc)rc=ps1_sd_flushfile(sd,h);
 memcpy(b,"IZPSXCFG",8);put(b,8,3);put(b,16,s->generation+1);put(b,24,sizeof s->prefs);put(b,32,hash(b+HEADER,sizeof s->prefs));put(b,40,hash(b,40));
 if(!rc)rc=ps1_sd_writefile(sd,h,0,b,HEADER);if(!rc)rc=ps1_sd_flushfile(sd,h);if(!rc)rc=read(sd,h,b,BYTES);
 Ps1Preferences decoded;if(!rc&&(!decode(b,BYTES,&decoded)||get(b,8)!=3||get(b,16)!=s->generation+1||memcmp(&decoded,&s->prefs,sizeof s->prefs)))rc=BAD_SETTINGS;
 rc=close(sd,h,rc);if(!rc)rc=ps1_sd_commit(sd);if(rc)return rc;
 s->generation++;s->active=target;s->dirty=0;s->warning=0;return 0;
}
