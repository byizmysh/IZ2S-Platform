#define _GNU_SOURCE
#include "../src/native-menu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t *buffer;
static unsigned position,total,opens,closes,reads,mode;
static char current[769];
static void reply(void *message,uint32_t code,uint64_t count,int handle){uint8_t *m=message;memset(m,0,4096);uint32_t *p=message;if(handle){p[1]=0x80000000u|8;p[2]=1u<<5;p[3]=77;}p[4]=0x4f434653;p[6]=code;memcpy(m+32,&count,8);}
static uint32_t send(void *message,uint64_t bytes,uint32_t handle){
 assert(bytes==4096&&((uintptr_t)message&4095)==0);uint32_t *p=message;
 if(handle==42){assert(p[0]==(4u|(1u<<16))&&p[1]==9&&p[6]==9&&p[8]==3);uintptr_t a=p[3]|(uint64_t)((p[2]>>12)&15)<<32|(uint64_t)((p[2]>>6)&7)<<36;assert((p[2]>>16)==769&&a==(uintptr_t)buffer);strcpy(current,(char*)a);position=0;opens++;reply(message,mode==7?0xe007:0,0,mode!=7);return 0;}
 assert(handle==77&&p[0]==(4u|(1u<<24))&&p[1]==8&&p[8]==0x49434653&&p[10]==0);reads++;uintptr_t a=p[3]|(uint64_t)((p[4]>>28)&15)<<32|(uint64_t)((p[4]>>2)&7)<<36;assert(a==(uintptr_t)buffer&&(p[4]&3)==0);size_t capacity=p[2]/0x310;unsigned count=total-position;if(count>capacity)count=capacity;if(mode==1){reply(message,0,capacity+1,0);return 0;}if(mode==2)return 0xe001;if(mode==3){reply(message,0xe002,0,0);return 0;}
 memset(buffer,0,p[2]);for(unsigned i=0;i<count;i++){uint8_t *row=buffer+i*0x310;unsigned index=position+i;if(mode==4)strcpy((char*)row,"../escape.cue");else if(mode==5&&strcmp(current,"/izmysh/ps1")==0){strcpy((char*)row,index?"zGame.cue":"games");row[0x304]=index?1:0;}else{snprintf((char*)row,769,"Game-%03u.cue",index);row[0x304]=1;}uint64_t size=10000+index;memcpy(row+0x308,&size,8);}position+=count;reply(message,0,count,0);return 0;
}
static uint32_t close_handle(uint32_t handle){assert(handle==77);closes++;return mode==6?1:0;}
static uint32_t rgba[640*480];static Ps1SdEntry entries[128];
int main(int argc,char **argv){
 buffer=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(buffer!=MAP_FAILED&&((uintptr_t)buffer&4095)==0);
 Ps1SdDirectory sd={.send=send,.close_handle=close_handle,.filesystem=42,.buffer=buffer,.capacity=16*0x310};size_t count=999;total=75;
 assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,128,&count)==0&&count==75&&reads==6&&opens==1&&closes==1);assert(sd.entries_read==75&&strcmp(entries[74].name,"Game-074.cue")==0);
 assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,20,&count)==PS1_SD_LIMIT&&count==0&&closes==2);
 for(unsigned fault=1;fault<=4;fault++){mode=fault;unsigned before=closes;assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,128,&count)!=0&&count==0&&closes==before+1);}
 mode=6;total=0;assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,128,&count)==PS1_SD_CLOSE&&sd.leaked_handle==77);assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,128,&count)==PS1_SD_CLOSE);mode=0;assert(sd.close_handle(sd.leaked_handle)==0);sd.leaked_handle=0;
 mode=7;unsigned before_close=closes;assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1",entries,128,&count)==0xe007&&closes==before_close);mode=0;unsigned before_open=opens;assert(ps1_sd_list(&sd,"/izmysh/ps1","/izmysh/ps1/../escape",entries,128,&count)==PS1_SD_INVALID&&opens==before_open);
 assert(ps1_sd_path_valid("/izmysh/ps1","/izmysh/ps1/Tekken 3/game.cue"));
 const char *bad[]={"/izmysh/ps1x/game.cue","/izmysh/ps1/../escape","/izmysh/ps1/./escape","/izmysh//ps1","/izmysh/ps1/","/izmysh/ps1/a\\b","/a"};for(size_t i=0;i<sizeof bad/sizeof bad[0];i++)assert(!ps1_sd_path_valid("/izmysh/ps1",bad[i]));
 char path[769];assert(ps1_sd_join("/izmysh/ps1","/izmysh/ps1","Tekken 3",path)&&strcmp(path,"/izmysh/ps1/Tekken 3")==0);assert(!ps1_sd_join("/izmysh/ps1","/izmysh/ps1","../game",path));assert(ps1_sd_parent("/izmysh/ps1","/izmysh/ps1/Tekken 3",path)&&strcmp(path,"/izmysh/ps1")==0);assert(!ps1_sd_parent("/izmysh/ps1","/izmysh/ps1",path));
 memset(entries,0,sizeof entries);strcpy(entries[0].name,"disc.bin");strcpy(entries[1].name,"disc.CUE");strcpy(entries[2].name,"another.PBP");strcpy(entries[3].name,"folder");entries[3].directory=1;strcpy(entries[4].name,"bios.bin");strcpy(entries[5].name,"notes.txt");assert(ps1_sd_filter_images(entries,6)==3&&entries[0].directory&&strcmp(entries[1].name,"another.PBP")==0);
 mode=5;total=2;Ps1MenuConfig config={.sd=&sd,.root="/izmysh/ps1",.entries=entries,.capacity=128,.rgba=rgba};Ps1Menu menu;assert(ps1_menu_init(&menu,&config)==0&&menu.count==2&&menu.error==0);assert(ps1_menu_step(&menu,&config,PS1_MENU_A)==0&&strcmp(menu.directory,"/izmysh/ps1/games")==0);ps1_menu_step(&menu,&config,0);assert(ps1_menu_step(&menu,&config,PS1_MENU_B)==0&&strcmp(menu.directory,"/izmysh/ps1")==0);ps1_menu_step(&menu,&config,0);ps1_menu_step(&menu,&config,PS1_MENU_DOWN);ps1_menu_step(&menu,&config,0);assert(ps1_menu_step(&menu,&config,PS1_MENU_A)==PS1_MENU_SELECTED&&strcmp(menu.selected,"/izmysh/ps1/zGame.cue")==0);
 assert(ps1_menu_init(&menu,&config)==0);assert(ps1_menu_step(&menu,&config,PS1_MENU_B)==PS1_MENU_CANCELLED);assert(ps1_menu_init(&menu,&config)==0);assert(ps1_menu_step(&menu,&config,PS1_MENU_EXIT_BUTTON)==PS1_MENU_EXIT);
 mode=0;total=75;assert(ps1_menu_init(&menu,&config)==0);for(unsigned i=0;i<30;i++)ps1_menu_step(&menu,&config,PS1_MENU_DOWN);assert(menu.index==4);ps1_menu_step(&menu,&config,0);ps1_menu_step(&menu,&config,PS1_MENU_RIGHT);assert(menu.index==16&&menu.top==5);
 ps1_menu_draw(&menu,&config);assert(rgba[0]==0xffeac45c&&rgba[479*640]!=0);if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(rgba,1,sizeof rgba,f)==sizeof rgba);fclose(f);}assert(munmap(buffer,65536)==0);puts("PASS: SD directory IPC pagination, bounds, errors/close, paths, filtering, menu navigation/repeat/cancel/exit and drawing");return 0;
}
