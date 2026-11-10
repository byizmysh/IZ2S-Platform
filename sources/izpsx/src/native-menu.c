#include "native-menu.h"
#include <string.h>
#include "logo-data.h"
#include "ui-font.h"
#include "brand.h"

/* Original compact 5x7 bitmap lettering. Lowercase filenames render uppercase;
 * their exact UTF-8 bytes are preserved for filesystem access. */
typedef struct{char c;uint8_t r[7];} Glyph;
static const Glyph glyphs[]={
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},{'C',{14,17,16,16,16,17,14}},
 {'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},
 {'J',{7,2,2,2,18,18,12}},{'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,25,25,21,19,19,17}},{'O',{14,17,17,17,17,17,14}},
 {'P',{30,17,17,30,16,16,16}},{'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},
 {'V',{17,17,17,17,17,10,4}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}},{'.',{0,0,0,0,0,12,12}},{'-',{0,0,0,31,0,0,0}},
 {'_',{0,0,0,0,0,0,31}},{'/',{1,2,2,4,8,8,16}},{':',{0,12,12,0,12,12,0}},
 {'[',{14,8,8,8,8,8,14}},{']',{14,2,2,2,2,2,14}},{'(',{2,4,8,8,8,4,2}},
 {')',{8,4,2,2,2,4,8}},{'+',{0,4,4,31,4,4,0}},{'?',{14,17,1,2,4,0,4}},
 {'@',{14,17,23,21,23,16,14}},
 {'%',{25,25,2,4,8,19,19}},
 {'b',{16,16,22,25,17,17,30}},{'y',{0,0,17,17,15,1,14}},{'i',{4,0,12,4,4,4,14}},{'z',{0,0,31,2,4,8,31}},{'m',{0,0,26,21,21,21,21}},{'s',{0,0,15,16,14,1,30}},{'h',{16,16,22,25,17,17,17}},
 {'>',{16,8,4,2,4,8,16}},{'!',{4,4,4,4,4,0,4}},{'=',{0,31,0,31,0,0,0}},{',',{0,0,0,0,0,4,8}}
};
void ps1_ui_rect(uint32_t *p,unsigned x,unsigned y,unsigned w,unsigned h,uint32_t color){if(x>=640||y>=480)return;if(w>640-x)w=640-x;if(h>480-y)h=480-y;for(unsigned yy=y;yy<y+h;yy++)for(unsigned xx=x;xx<x+w;xx++)p[yy*640+xx]=color;}
static void text_case(uint32_t *p,unsigned x,unsigned y,const char *s,unsigned scale,uint32_t color,unsigned limit,int upper){if(scale==1){for(unsigned n=0;s[n]&&n<limit&&x+n*7+6<640;n++){unsigned c=(unsigned char)s[n];if(c<32||c>126)c='?';for(unsigned yy=0;yy<12&&y+yy<480;yy++)for(unsigned xx=0;xx<7;xx++){unsigned a=((izpsx_ui_font[c-32][yy]>>(xx*2))&3)*85;if(!a)continue;uint32_t old=p[(y+yy)*640+x+n*7+xx],out=0xff000000;for(unsigned ch=0;ch<3;ch++){unsigned shift=ch*8;out|=((((color>>shift)&255)*a+((old>>shift)&255)*(255-a))/255)<<shift;}p[(y+yy)*640+x+n*7+xx]=out;}}return;}for(unsigned n=0;s[n]&&n<limit;n++){unsigned c=(unsigned char)s[n];if(upper&&c>='a'&&c<='z')c-=32;if(c==' ')continue;const uint8_t *g=0;for(size_t j=0;j<sizeof glyphs/sizeof glyphs[0];j++)if((unsigned char)glyphs[j].c==c){g=glyphs[j].r;break;}if(!g){static const uint8_t unknown[]={14,17,1,2,4,0,4};g=unknown;}for(unsigned yy=0;yy<7;yy++)for(unsigned xx=0;xx<5;xx++)if(g[yy]&(1u<<(4-xx)))ps1_ui_rect(p,x+n*6*scale+xx*scale,y+yy*scale,scale,scale,color);}}
void ps1_ui_text(uint32_t*p,unsigned x,unsigned y,const char*s,unsigned scale,uint32_t color,unsigned limit){text_case(p,x,y,s,scale,color,limit,1);}
#define rect ps1_ui_rect
#define text ps1_ui_text
static void hex(char out[9],uint32_t v){static const char h[]="0123456789ABCDEF";for(unsigned i=0;i<8;i++)out[i]=h[(v>>(28-i*4))&15];out[8]=0;}
static void decimal(char out[24],size_t n){char reverse[24];size_t i=0,j=0;do{reverse[i++]=(char)('0'+n%10);n/=10;}while(n&&i<sizeof reverse);while(i)out[j++]=reverse[--i];out[j]=0;}
static int config_valid(const Ps1MenuConfig *c){return c&&c->sd&&c->root&&c->entries&&c->capacity&&c->rgba&&ps1_sd_path_valid(c->root,c->root);}
static void load(Ps1Menu *m,const Ps1MenuConfig *c){m->count=m->index=m->top=0;m->error_kind=0;m->error=ps1_sd_list(c->sd,c->root,m->directory,c->entries,c->capacity,&m->count);if(!m->error){if(!strcmp(m->directory,c->root)){size_t out=0;for(size_t i=0;i<m->count;i++)if(strcmp(c->entries[i].name,"bios"))c->entries[out++]=c->entries[i];m->count=out;}m->count=ps1_sd_filter_images(c->entries,m->count);}}
int ps1_menu_init(Ps1Menu *m,const Ps1MenuConfig *c){if(!m||!config_valid(c))return PS1_MENU_ERROR;memset(m,0,sizeof *m);memcpy(m->directory,c->root,strlen(c->root)+1);load(m,c);if(c->buttons)m->previous=c->buttons(c->user);return PS1_MENU_RUNNING;}
int ps1_menu_step(Ps1Menu *m,const Ps1MenuConfig *c,uint32_t buttons){
 if(!m||!config_valid(c))return PS1_MENU_ERROR;
 if(m->status)return (int)m->status;
 uint32_t pressed=buttons&~m->previous,direction=buttons&(PS1_MENU_UP|PS1_MENU_DOWN|PS1_MENU_LEFT|PS1_MENU_RIGHT);
 if(direction&&direction==(m->previous&(PS1_MENU_UP|PS1_MENU_DOWN|PS1_MENU_LEFT|PS1_MENU_RIGHT))){m->held_ticks++;if(m->held_ticks>=20&&(m->held_ticks-20)%4==0)pressed|=direction;}else m->held_ticks=0;m->previous=buttons;
 if(pressed&PS1_MENU_EXIT_BUTTON)return m->status=PS1_MENU_EXIT;
 if(pressed&PS1_MENU_B){char p[PS1_SD_PATH_MAX];if(!ps1_sd_parent(c->root,m->directory,p))return m->status=PS1_MENU_CANCELLED;memcpy(m->directory,p,strlen(p)+1);load(m,c);return PS1_MENU_RUNNING;}
 if(pressed&PS1_MENU_A){if(m->error){load(m,c);return PS1_MENU_RUNNING;}if(m->count){Ps1SdEntry *e=&c->entries[m->index];char p[PS1_SD_PATH_MAX];if(!ps1_sd_join(c->root,m->directory,e->name,p)){m->error=PS1_SD_INVALID;return PS1_MENU_RUNNING;}if(e->directory){memcpy(m->directory,p,strlen(p)+1);load(m,c);}else{memcpy(m->selected,p,strlen(p)+1);return m->status=PS1_MENU_SELECTED;}}}
 if(m->count&&!m->error){if(pressed&PS1_MENU_UP)m->index=m->index?m->index-1:m->count-1;if(pressed&PS1_MENU_DOWN)m->index=(m->index+1)%m->count;if(pressed&PS1_MENU_LEFT)m->index=m->index>=12?m->index-12:0;if(pressed&PS1_MENU_RIGHT)m->index=m->index+12<m->count?m->index+12:m->count-1;if(m->index<m->top)m->top=m->index;if(m->index>=m->top+12)m->top=m->index-11;}
 return PS1_MENU_RUNNING;
}
/* A small, entirely native UI. It needs no fonts or assets from a host. */
void ps1_ui_chrome(uint32_t*p,const char*section,const Ps1Preferences*prefs){
 uint32_t accent=ps1_ui_accent(prefs);
 for(unsigned y=0;y<480;y++){unsigned r=12+y/80,g=16+y/48,b=28+y/24;ps1_ui_rect(p,0,y,640,1,0xff000000u|(b<<16)|(g<<8)|r);}
 ps1_ui_rect(p,0,0,640,3,accent);ps1_ui_rect(p,22,18,3,52,accent);
 ps1_ui_logo(p,17,4,1);text(p,110,19,IZPSX_NAME,4,0xfff5f2ec,5);text(p,112,58,section,1,0xffccbcb0,60);
 text_case(p,490,27,"@byizmysh",1,accent,10,0);text(p,490,46,"PLAYSTATION",1,0xffccbcb0,11);
 ps1_ui_rect(p,22,78,596,1,0xff3c3026);ps1_ui_rect(p,22,416,596,1,0xff3c3026);
}
void ps1_loading_draw(uint32_t*p,unsigned progress,const char*message){
 if(!p)return;if(progress>1000)progress=1000;ps1_ui_chrome(p,"PLAYSTATION ON SWITCH 2",0);uint32_t accent=ps1_ui_accent(0);
 text(p,38,174,"GETTING READY",3,0xfff5f2ec,30);
 text(p,40,224,message?message:"LOADING FROM SD",1,0xffccbcb0,88);
 ps1_ui_rect(p,40,269,560,8,0xff493623);ps1_ui_rect(p,40,269,560*progress/1000,8,accent);
 text(p,40,315,"YOUR GAMES. YOUR SAVE FILES.",1,0xffb3a79b,80);
 text(p,24,441,"- IZPSX MENU / EXIT FROM MENU",1,0xffb3a79b,70);
}
void ps1_menu_draw(const Ps1Menu *m,const Ps1MenuConfig *c){
 if(!m||!config_valid(c))return;uint32_t*p=c->rgba;ps1_ui_chrome(p,"YOUR PLAYSTATION LIBRARY",c->preferences);uint32_t accent=ps1_ui_accent(c->preferences);
 text(p,24,90,"SD",1,accent,2);const char*path=m->directory;size_t n=strlen(path);if(n>79)path+=n-79;text(p,48,90,path,1,0xffb3a79b,94);
 if(m->error){char code[9];hex(code,m->error);text(p,30,145,m->error_kind==1?"BIOS NOT READY":m->error_kind==2?"GAME NOT READY":"FOLDER UNAVAILABLE",3,0xffb4b8ff,25);text(p,30,190,m->error_kind==1?"Add your own 512 KB scph101.bin in the BIOS folder.":m->error_kind==2?"Check the game image and its CUE/BIN track files.":"Check the SD card and try again.",1,0xffd3e1e9,70);text(p,30,218,"ERROR",1,0xffb3a79b,5);text(p,72,218,code,1,0xffb3a79b,8);text(p,30,254,m->error_kind?"A LIBRARY    B BACK":"A RETRY    B BACK",2,accent,40);}
 else if(!m->count){text(p,30,155,"YOUR LIBRARY STARTS HERE",2,0xfff5f2ec,45);text(p,30,199,"ADD BIN/CUE OR PBP TO THE GAMES FOLDER",1,0xffb3a79b,80);text(p,30,225,"SD:/IZ2S/HOMEBREW/MODULES/IZPSX/GAMES",1,accent,80);}
 else for(size_t i=m->top;i<m->count&&i<m->top+12;i++){
  unsigned y=114+(unsigned)(i-m->top)*24;int selected=i==m->index;
  if(selected){ps1_ui_rect(p,22,y-5,596,23,0xff493623);ps1_ui_rect(p,22,y-5,3,23,accent);}
  text(p,34,y,c->entries[i].directory?"+":">",2,selected?accent:0xff786451,1);
  char label[PS1_SD_ENTRY_NAME_MAX];memcpy(label,c->entries[i].name,strlen(c->entries[i].name)+1);if(c->preferences&&!c->preferences->value[PS1_EXTENSIONS]&&!c->entries[i].directory){char*dot=strrchr(label,'.');if(dot)*dot=0;}text(p,62,y,label,2,selected?0xfff5f2ec:0xffc5b8ab,45);
 }
 text(p,24,432,"A OPEN    B BACK    D-PAD SELECT / PAGE",1,0xffe6d5c4,85);
 if(!m->error&&m->count){char value[24];decimal(value,m->index+1);text(p,24,455,value,1,accent,23);text(p,90,455,"OF",1,0xffb3a79b,2);decimal(value,m->count);text(p,114,455,value,1,0xffb3a79b,23);}
 text(p,278,455,c->single_joycon?"Y LAST / SL+SR+/- SETTINGS":"Y LAST PLAYED / - SETTINGS",1,0xffb3a79b,50);
}
int ps1_menu_run(Ps1Menu *m,const Ps1MenuConfig *c){if(!m||!config_valid(c)||!c->buttons||!c->present)return PS1_MENU_ERROR;while(!m->status){ps1_menu_draw(m,c);if(c->present(c->user,c->rgba,640,480)){m->error=PS1_SD_PACKET;return m->status=PS1_MENU_ERROR;}ps1_menu_step(m,c,c->buttons(c->user));if(c->sleep)c->sleep(c->user,16666667);}return (int)m->status;}

static int pause_valid(const Ps1MenuConfig*c){return c&&c->rgba&&c->buttons&&c->present&&c->sleep;}
static uint32_t pause_controls(void){return PS1_MENU_A|PS1_MENU_B|PS1_MENU_UP|PS1_MENU_DOWN|PS1_MENU_LEFT|PS1_MENU_RIGHT|PS1_PAUSE_MENU_BUTTON;}
int ps1_pause_init(Ps1Pause*m,const Ps1MenuConfig*c){
 if(!m||!pause_valid(c))return PS1_PAUSE_ERROR;
 memset(m,0,sizeof*m);m->previous=c->buttons(c->user);m->input_armed=!(m->previous&pause_controls());
 if(m->previous&PS1_MENU_EXIT_BUTTON)m->status=PS1_PAUSE_RETURN_LAUNCHER;
 return PS1_PAUSE_RUNNING;
}
int ps1_pause_step(Ps1Pause*m,const Ps1MenuConfig*c,uint32_t buttons){
 if(!m||!pause_valid(c))return PS1_PAUSE_ERROR;
 if(m->status)return (int)m->status;
 /* An owner stop request takes priority while entry/Continue waits for
  * held buttons, so shutdown never depends on another release event. */
 if(buttons&PS1_MENU_EXIT_BUTTON)return m->status=PS1_PAUSE_RETURN_LAUNCHER;
 if(m->pending_continue==2){if(buttons&PS1_MENU_B){m->pending_continue=0;m->previous=buttons;return PS1_PAUSE_RUNNING;}if((buttons&PS1_MENU_A)&&!(m->previous&PS1_MENU_A))return m->status=PS1_PAUSE_RESTART;m->previous=buttons;return PS1_PAUSE_RUNNING;}
 if(m->pending_continue){if(!(buttons&(PS1_MENU_A|PS1_MENU_B|PS1_PAUSE_MENU_BUTTON)))return m->status=PS1_PAUSE_CONTINUE;m->previous=buttons;return PS1_PAUSE_RUNNING;}
 if(!m->input_armed){m->previous=buttons;m->held_ticks=0;if(!(buttons&pause_controls()))m->input_armed=1;return PS1_PAUSE_RUNNING;}
 uint32_t pressed=buttons&~m->previous,direction=buttons&(PS1_MENU_UP|PS1_MENU_DOWN);
 if(direction&&direction==(m->previous&(PS1_MENU_UP|PS1_MENU_DOWN))){m->held_ticks++;if(m->held_ticks>=20&&(m->held_ticks-20)%4==0)pressed|=direction;}else m->held_ticks=0;
 m->previous=buttons;
 if(pressed&(PS1_MENU_B|PS1_PAUSE_MENU_BUTTON)){m->pending_continue=1;return PS1_PAUSE_RUNNING;}
 if(pressed&PS1_MENU_A){if(m->selection==0){m->pending_continue=1;return PS1_PAUSE_RUNNING;}if(m->selection==1)return m->status=PS1_PAUSE_SETTINGS;if(m->selection==2){m->pending_continue=2;return PS1_PAUSE_RUNNING;}return m->status=m->selection==3?PS1_PAUSE_CHOOSE_GAME:PS1_PAUSE_RETURN_LAUNCHER;}
 if(direction!=(PS1_MENU_UP|PS1_MENU_DOWN)){if(pressed&PS1_MENU_UP)m->selection=m->selection?m->selection-1:4;if(pressed&PS1_MENU_DOWN)m->selection=(m->selection+1)%5;}
 return PS1_PAUSE_RUNNING;
}
void ps1_pause_draw(const Ps1Pause*m,const Ps1MenuConfig*c){
 if(!m||!pause_valid(c))return;uint32_t*p=c->rgba;ps1_ui_chrome(p,"IZPSX EMULATOR MENU",c->preferences);uint32_t accent=ps1_ui_accent(c->preferences);
 static const char*labels[]={"CONTINUE","SETTINGS","RESTART GAME","CHOOSE GAME","EXIT IZPSX EMULATOR"};
 static const char*notes[]={"PICK UP WHERE YOU LEFT OFF","VIDEO, SOUND, EMULATION AND CONTROLS","RESTART THE CURRENT DISC","BROWSE YOUR SD LIBRARY","BACK TO YOUR APPLICATIONS"};
 for(unsigned i=0;i<5;i++){unsigned y=114+i*51;if(i==m->selection){rect(p,22,y-7,596,47,0xff493623);rect(p,22,y-7,3,47,accent);}text(p,38,y,i==m->selection?">":" ",2,accent,1);text(p,70,y,labels[i],2,i==m->selection?0xfff5f2ec:0xffc5b8ab,40);text(p,70,y+22,notes[i],1,0xffb3a79b,70);}
 if(m->pending_continue)text(p,24,380,m->pending_continue==2?"RESTART THIS GAME? A CONFIRM / B CANCEL":"RELEASE BUTTONS TO CONTINUE",1,accent,80);
 else if(c->save_status)text(p,24,380,c->save_status,1,0xffb2db8d,80);
 text(p,24,432,"A SELECT    B CONTINUE    D-PAD SELECT",1,0xffe6d5c4,90);
 text(p,24,455,c->single_joycon?"+/- START / SL+SR+/- IZPSX MENU / STICK SELECT":"+ GAME MENU / - IZPSX MENU / L3 SELECT",1,0xffb3a79b,95);
}
int ps1_pause_run(const Ps1MenuConfig*c){
 Ps1Pause menu;if(ps1_pause_init(&menu,c)!=PS1_PAUSE_RUNNING)return PS1_PAUSE_ERROR;
 while(!menu.status){ps1_pause_draw(&menu,c);if(c->present(c->user,c->rgba,640,480))return PS1_PAUSE_ERROR;ps1_pause_step(&menu,c,c->buttons(c->user));if(!menu.status)c->sleep(c->user,16666667);}
 return (int)menu.status;
}

uint32_t ps1_ui_accent(const Ps1Preferences*p){static const uint32_t colors[]={0xffeac45c,0xffff9b60,0xffef8ab8,0xff55c8f3};return colors[p&&p->value[PS1_ACCENT]<4?p->value[PS1_ACCENT]:0];}
void ps1_ui_logo(uint32_t*p,unsigned x,unsigned y,unsigned scale){
 unsigned pixel=0;for(unsigned i=0;i<sizeof izpsx_logo/sizeof izpsx_logo[0];i++)for(unsigned n=0;n<izpsx_logo[i].count;n++,pixel++){
  uint32_t color=izpsx_logo[i].color;if(!(color>>24))continue;
  unsigned xx=x+(pixel%IZPSX_LOGO_W)*scale,yy=y+(pixel/IZPSX_LOGO_W)*scale;
  for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++)if(xx+dx<640&&yy+dy<480){uint32_t old=p[(yy+dy)*640+xx+dx],a=color>>24,out=0xff000000;for(unsigned ch=0;ch<3;ch++){unsigned shift=ch*8;out|=((((color>>shift)&255)*a+((old>>shift)&255)*(255-a))/255)<<shift;}p[(yy+dy)*640+xx+dx]=out;}
 }
}
void ps1_performance_draw(uint32_t*p,unsigned w,unsigned h,unsigned emu,unsigned video){
 if(!p||w<100||h<20)return;char a[24],b[24],label[64]="EMU ";decimal(a,emu);decimal(b,video);strcat(label,a);strcat(label," / VIDEO ");strcat(label,b);
 /* The text primitive is for 640-wide menus. Draw a tiny scratch strip and
  * copy only its valid extent into the variable-width game framebuffer. */
 uint32_t strip[640*14];for(unsigned i=0;i<640*14;i++)strip[i]=0xff18100b;text(strip,4,1,label,1,0xfff5f2ec,42);
 unsigned width=(unsigned)strlen(label)*7+8;if(width>w)width=w;for(unsigned y=0;y<14;y++)memcpy(p+y*w,strip+y*640,width*4);
}

void ps1_ui_github(uint32_t*p,unsigned x,unsigned y,unsigned scale,uint32_t color){
 static const unsigned short mark[]={0x03c0,0x0ff0,0x1ff8,0x381c,0x73ce,0x67e6,0x4ff2,0x4ff2,0x4ff2,0x67e6,0x73ce,0x39dc,0x1bd8,0x0bd0,0x03c0,0};
 for(unsigned yy=0;yy<16;yy++)for(unsigned xx=0;xx<16;xx++)if(mark[yy]&(1u<<(15-xx)))rect(p,x+xx*scale,y+yy*scale,scale,scale,color);
}
