#define IZPSX_SETTINGS_MODEL
#define main unused_card_model_main
#include "memory-card-model.c"
#undef main
#include "settings-menu.h"
static uint32_t image[640*480];
static uint64_t config_hash(const uint8_t*b,size_t bytes){uint64_t h=1469598103934665603ull;for(size_t i=0;i<bytes;i++){h^=b[i];h*=1099511628211ull;}return h;}
static void config_put(uint8_t*b,unsigned at,uint64_t value){memcpy(b+at,&value,8);}
static void legacy_journal(const Ps1Preferences*p){
 for(unsigned i=0;i<count;i++)free(files[i].data);memset(files,0,sizeof files);count=1;
 strcpy(files[0].path,IZPSX_SD_ROOT "/settings.a.izcfg");size_t payload=offsetof(Ps1Preferences,player2);files[0].size=64+payload;files[0].data=calloc(1,files[0].size);assert(files[0].data);
 uint8_t*b=files[0].data;memcpy(b+64,p,payload);memcpy(b,"IZPSXCFG",8);config_put(b,8,1);config_put(b,16,10);config_put(b,24,payload);config_put(b,32,config_hash(b+64,payload));config_put(b,40,config_hash(b,40));
}
int main(int argc,char**argv){
 scratch=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(scratch!=MAP_FAILED);
 Ps1SdDirectory sd={.send=send,.close_handle=close_handle,.filesystem=42,.buffer=scratch,.capacity=65536};Ps1Settings s,t;
 check(!ps1_settings_load(&s,&sd)&&!s.generation&&!s.dirty&&ps1_preferences_valid(&s.prefs));
 check(ps1_setting_change(&s,PS1_FPS,1)&&ps1_display_limit(&s.prefs)==20);s.prefs.value[PS1_FPS]=3;check(!ps1_settings_save(&s,&sd)&&s.generation==1&&!s.dirty&&!open_count);
 s.prefs.value[PS1_VOLUME]=1;s.dirty=1;check(!ps1_settings_save(&s,&sd)&&s.generation==2&&count==2);
 check(!ps1_settings_load(&t,&sd)&&t.prefs.value[PS1_VOLUME]==1&&ps1_display_limit(&t.prefs)==30);
 for(unsigned phase=1;phase<=3;phase++){s.prefs.value[PS1_FPS]=5;s.dirty=1;fail_write=writes+phase;check(ps1_settings_save(&s,&sd)!=0&&!open_count);fail_write=0;check(!ps1_settings_load(&t,&sd)&&ps1_display_limit(&t.prefs)==30&&t.prefs.value[PS1_VOLUME]==1);}
 check(!ps1_settings_save(&s,&sd)&&s.generation==3);files[0].data[64]^=1;check(!ps1_settings_load(&t,&sd)&&t.generation==2&&t.warning&&ps1_display_limit(&t.prefs)==30);
 files[1].data[0]^=1;unsigned before=writes;check(!ps1_settings_load(&t,&sd)&&!t.generation&&t.warning&&!t.dirty&&writes==before&&ps1_display_limit(&t.prefs)==0);
 t.prefs.value[PS1_FPS]=99;check(!ps1_preferences_valid(&t.prefs));ps1_preferences_default(&t.prefs);strcpy(t.prefs.recent,IZPSX_SD_GAMES "/../outside.cue");check(!ps1_preferences_valid(&t.prefs));t.prefs.recent[0]=0;
 Ps1FrameClock clock={0};unsigned shown=0;for(unsigned f=0;f<2400;f++)shown+=ps1_frame_clock_due(&clock,1000000ull+f*625000ull,31250000,30);check(shown==1440);
 memset(&clock,0,sizeof clock);shown=0;for(unsigned f=0;f<2400;f++)shown+=ps1_frame_clock_due(&clock,1000000ull+f*625000ull,31250000,50);check(shown==2400);
 check(!ps1_frame_clock_due(&clock,clock.next-1,31250000,50));check(ps1_frame_clock_due(&clock,clock.next+312500000,31250000,50));check(!ps1_frame_clock_due(&clock,clock.next-1,31250000,50));
 int32_t axes[2]={15000,-15000};check(ps1_input_settings(&t.prefs,0,axes,0,1000)==((1u<<7)|(1u<<5)));t.prefs.value[PS1_DEADZONE]=2;check(!ps1_input_settings(&t.prefs,0,axes,0,1000));t.prefs.value[PS1_SWAP_AB]=1;check(ps1_input_settings(&t.prefs,1,0,0,1000)==256);t.prefs.value[PS1_SWAP_AB]=0;t.prefs.value[PS1_TURBO]=1;check(ps1_input_settings(&t.prefs,1,0,0,1000)==1&&!ps1_input_settings(&t.prefs,1,0,100,1000));
 ps1_preferences_default(&s.prefs);Ps1Preferences launched=s.prefs;ps1_active_settings=&launched;s.prefs.value[PS1_CPU_CLOCK]=2;check(!strcmp(ps1_core_setting("pcsx_rearmed_psxclock"),"57"));ps1_active_settings=&s.prefs;check(!strcmp(ps1_core_setting("pcsx_rearmed_psxclock"),"100"));
 Ps1SettingsMenu m;ps1_settings_menu_init(&m,128);ps1_settings_menu_step(&m,&s,128);check(!m.armed);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,32);check(s.prefs.value[PS1_FPS]==1);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_X);check(!s.prefs.value[PS1_FPS]);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_R);check(m.category==PS1_SOUND);
 ps1_preferences_default(&s.prefs);s.dirty=0;ps1_settings_menu_init(&m,0);m.category=PS1_CONTROLS;
 ps1_settings_menu_step(&m,&s,PS1_MENU_ZR);check(m.player==1&&!s.dirty);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_DOWN);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_DOWN);check(m.row==2);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_DOWN);check(m.row==3);
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_A);check(s.prefs.player2[0]==1&&s.prefs.value[PS1_SWAP_AB]==0);
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_X);check(s.prefs.player2[0]==0);
 s.prefs.value[PS1_SWAP_AB]=1;s.prefs.player2[0]=1;ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_Y);check(m.confirm_reset);
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_A);check(!m.confirm_reset&&!s.prefs.player2[0]&&s.prefs.value[PS1_SWAP_AB]==1);
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_UP);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_UP);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_UP);check(m.row==0);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_RIGHT);check(m.player==2);
 for(unsigned cat=0;cat<PS1_CATEGORIES;cat++){m.category=cat;m.row=0;Ps1MenuConfig c={.rgba=image,.preferences=&s.prefs,.controller_status="LIVE",.controller_mask=3};ps1_settings_menu_draw(&m,&s,&c);if(argc>1){char out[1024];snprintf(out,sizeof out,"%s/settings-%u.rgba",argv[1],cat);FILE*f=fopen(out,"wb");assert(f&&fwrite(image,1,sizeof image,f)==sizeof image&&!fclose(f));}}
 if(argc>1){m.category=PS1_CONTROLS;m.player=1;m.row=4;s.prefs.player2[2]=2;Ps1MenuConfig c={.rgba=image,.preferences=&s.prefs,.controller_status="LIVE",.controller_mask=3};ps1_settings_menu_draw(&m,&s,&c);char out[1024];snprintf(out,sizeof out,"%s/settings-player2.rgba",argv[1]);FILE*f=fopen(out,"wb");assert(f&&fwrite(image,1,sizeof image,f)==sizeof image&&!fclose(f));}
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_B);check(m.pending_close&&!m.closed);ps1_settings_menu_step(&m,&s,0);check(m.closed);
 ps1_performance_draw(image,320,240,50,30);check(image[0]!=0);
 ps1_settings_menu_init(&m,0);m.category=PS1_CONTROLS;m.player=1;m.row=7;
 ps1_settings_menu_step(&m,&s,PS1_MENU_A);check(m.confirm_disconnect&&!m.disconnect_request);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_B);check(!m.confirm_disconnect&&!m.disconnect_request);
 ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_A);ps1_settings_menu_step(&m,&s,0);ps1_settings_menu_step(&m,&s,PS1_MENU_A);check(m.disconnect_request==2);m.disconnect_request=0;ps1_settings_menu_step(&m,&s,PS1_MENU_A);check(!m.disconnect_request);
 Ps1Preferences old;ps1_preferences_default(&old);old.value[PS1_SWAP_AB]=1;old.value[PS1_TURBO]=2;old.value[PS1_FPS]=3;strcpy(old.recent,IZPSX_SD_GAMES "/Tekken 3/Tekken 3.cue");legacy_journal(&old);
 before=writes;check(!ps1_settings_load(&s,&sd)&&s.generation==10&&!s.dirty&&writes==before);
 for(unsigned i=0;i<PS1_CONTROL_COUNT;i++)check(s.prefs.player2[i]==old.value[PS1_SWAP_AB+i]);check(!strcmp(s.prefs.recent,old.recent)&&s.prefs.value[PS1_FPS]==3);
 check(ps1_control_change(&s,1,PS1_SWAP_AB,1)&&!s.prefs.player2[0]&&s.prefs.value[PS1_SWAP_AB]==1);
 s.prefs.controller_device[1]=PS1_DEVICE_OFF;
 for(unsigned phase=1;phase<=3;phase++){fail_write=writes+phase;check(ps1_settings_save(&s,&sd)!=0&&!open_count);fail_write=0;check(!ps1_settings_load(&t,&sd)&&t.generation==10&&t.prefs.player2[0]==1&&t.prefs.value[PS1_SWAP_AB]==1);}
 check(!ps1_settings_save(&s,&sd)&&s.generation==11);check(!ps1_settings_load(&t,&sd)&&t.generation==11&&!t.prefs.player2[0]&&t.prefs.value[PS1_SWAP_AB]==1&&!strcmp(t.prefs.recent,old.recent)&&t.prefs.controller_device[1]==PS1_DEVICE_OFF);
 check(!ps1_input_player_settings(&t.prefs,1,0xffff,axes,0,1000));t.prefs.controller_device[1]=PS1_DEVICE_AUTO;
 t.prefs.player2[1]=0;t.prefs.player2[3]=0;check(ps1_input_player_settings(&t.prefs,0,1,axes,0,1000)!=(ps1_input_player_settings(&t.prefs,1,1,axes,0,1000)));check(ps1_input_player_settings(&t.prefs,1,0,axes,0,1000)==0);
 t.prefs.player2[2]=99;check(!ps1_preferences_valid(&t.prefs));check(!ps1_control_change(&s,4,PS1_SWAP_AB,1)&&!ps1_control_change(&s,0,PS1_FPS,1));
 ps1_preferences_default(&s.prefs);s.dirty=0;
 check(ps1_control_change(&s,2,PS1_SWAP_AB,1)&&ps1_control_change(&s,3,PS1_DEADZONE,1));
 check(ps1_control_value(&s.prefs,2,PS1_SWAP_AB)==1&&ps1_control_value(&s.prefs,0,PS1_SWAP_AB)==0&&ps1_control_value(&s.prefs,3,PS1_DEADZONE)==2);
 s.prefs.controller_device[0]=PS1_DEVICE_PAD1;s.prefs.controller_device[2]=PS1_DEVICE_PAD1;check(!ps1_preferences_valid(&s.prefs));s.prefs.controller_device[2]=PS1_DEVICE_PAD1+2;check(ps1_preferences_valid(&s.prefs));
 s.prefs.multitap=PS1_MULTITAP_PORT1;ps1_active_settings=&s.prefs;check(!strcmp(ps1_core_setting("pcsx_rearmed_multitap"),"port 1"));
 check(!ps1_settings_save(&s,&sd)&&!ps1_settings_load(&t,&sd)&&t.prefs.multitap==PS1_MULTITAP_PORT1&&ps1_control_value(&t.prefs,3,PS1_DEADZONE)==2);
 ps1_settings_menu_init(&m,0);m.category=PS1_CONTROLS;for(unsigned i=0;i<4;i++){ps1_settings_menu_step(&m,&s,PS1_MENU_ZR);check(m.player==(i+1)%4);ps1_settings_menu_step(&m,&s,0);}
 m.row=1;ps1_settings_menu_step(&m,&s,PS1_MENU_X);check(s.prefs.multitap==PS1_MULTITAP_OFF);
 /* Version-2 prefix preserves both original profiles and their devices. */
 for(unsigned i=0;i<count;i++)free(files[i].data);memset(files,0,sizeof files);count=1;strcpy(files[0].path,IZPSX_SD_ROOT "/settings.a.izcfg");size_t payload=offsetof(Ps1Preferences,controller_device)+8;files[0].size=64+payload;files[0].data=calloc(1,files[0].size);uint8_t*b=files[0].data;s.prefs.player2[0]=1;memcpy(b+64,&s.prefs,payload);memcpy(b,"IZPSXCFG",8);config_put(b,8,2);config_put(b,16,20);config_put(b,24,payload);config_put(b,32,config_hash(b+64,payload));config_put(b,40,config_hash(b,40));
 check(!ps1_settings_load(&t,&sd)&&t.generation==20&&t.prefs.player2[0]==1&&t.prefs.controller_device[0]==PS1_DEVICE_PAD1&&t.prefs.multitap==0);
 check(ps1_control_value(&t.prefs,2,PS1_SWAP_AB)==s.prefs.value[PS1_SWAP_AB]&&t.prefs.controller_device[2]==0);
 if(argc>1){m.category=PS1_CONTROLS;m.player=3;m.row=1;Ps1MenuConfig c={.rgba=image,.preferences=&s.prefs,.controller_status="LIVE",.controller_mask=15,.controller_style={"FULL CONTROLLER","JOY-CON PAIR","JOY-CON L / SIDEWAYS","JOY-CON R / SIDEWAYS"}};ps1_settings_menu_draw(&m,&s,&c);char out[1024];snprintf(out,sizeof out,"%s/settings-four-players.rgba",argv[1]);FILE*f=fopen(out,"wb");assert(f&&fwrite(image,1,sizeof image,f)==sizeof image&&!fclose(f));}
 for(unsigned i=0;i<count;i++)free(files[i].data);munmap(scratch,65536);printf("PASS: %u settings checks; torn-write recovery, validation, next-launch isolation, 30 FPS pacing at native 50 Hz, input and all menu tabs\n",checks);return 0;
}
