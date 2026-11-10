/* Dedicated pause UI model: any accidental SD/filesystem use fails the test. */
#include "../src/native-menu.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t pixels[640*480];
int ps1_sd_path_valid(const char*a,const char*b){(void)a;(void)b;assert(0&&"pause queried SD paths");return 0;}
int ps1_sd_join(const char*a,const char*b,const char*c,char d[PS1_SD_PATH_MAX]){(void)a;(void)b;(void)c;(void)d;assert(0&&"pause joined SD path");return 0;}
int ps1_sd_parent(const char*a,const char*b,char c[PS1_SD_PATH_MAX]){(void)a;(void)b;(void)c;assert(0&&"pause queried SD parent");return 0;}
uint32_t ps1_sd_list(Ps1SdDirectory*a,const char*b,const char*c,Ps1SdEntry*d,size_t e,size_t*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;assert(0&&"pause performed filesystem IPC");return 1;}
size_t ps1_sd_filter_images(Ps1SdEntry*a,size_t b){(void)a;(void)b;assert(0&&"pause filtered SD images");return 0;}
typedef struct {const uint32_t*input;unsigned count,cursor,frames,sleeps,error_at;} Model;
static uint32_t buttons(void*user){Model*m=user;assert(m->cursor<m->count);return m->input[m->cursor++];}
static int present(void*user,const uint32_t*rgba,unsigned w,unsigned h){Model*m=user;assert(rgba==pixels&&w==640&&h==480);assert(++m->frames<100);return m->error_at&&m->frames==m->error_at;}
static void sleep_frame(void*user,uint64_t ns){Model*m=user;assert(ns==16666667);m->sleeps++;}
static Ps1MenuConfig config(Model*m){Ps1MenuConfig c={.rgba=pixels,.user=m,.buttons=buttons,.present=present,.sleep=sleep_frame};return c;}
static Model sequence(const uint32_t*input,unsigned count){Model m={.input=input,.count=count};return m;}
#define SCRIPT(name,...) const uint32_t name[]={__VA_ARGS__}
#define RUN(name,expected) do{Model m=sequence(name,sizeof name/sizeof name[0]);Ps1MenuConfig c=config(&m);assert(ps1_pause_run(&c)==expected);assert(m.cursor==m.count&&m.sleeps+1==m.frames);}while(0)
int main(int argc,char**argv){
 SCRIPT(a_resume,128,128,0,1,1,0);RUN(a_resume,PS1_PAUSE_CONTINUE);
 SCRIPT(b_resume,0,2,2,0);RUN(b_resume,PS1_PAUSE_CONTINUE);
 SCRIPT(minus_resume,0,128,128,0);RUN(minus_resume,PS1_PAUSE_CONTINUE);
 SCRIPT(settings,0,8,0,1);RUN(settings,PS1_PAUSE_SETTINGS);
 SCRIPT(restart,0,8,0,8,0,1,0,1);RUN(restart,PS1_PAUSE_RESTART);
 SCRIPT(cancel_restart,0,8,0,8,0,1,0,2,0,2,0);RUN(cancel_restart,PS1_PAUSE_CONTINUE);
 SCRIPT(choose,0,8,0,8,0,8,0,1);RUN(choose,PS1_PAUSE_CHOOSE_GAME);
 SCRIPT(launcher,0,4,0,1);RUN(launcher,PS1_PAUSE_RETURN_LAUNCHER);
 SCRIPT(held_a,1,1,0,8,0,8,0,8,0,1);RUN(held_a,PS1_PAUSE_CHOOSE_GAME);
 SCRIPT(plus_ignored,0,0x400,0x400,0,128,0);RUN(plus_ignored,PS1_PAUSE_CONTINUE);
 SCRIPT(exit_pending,0,1,1,64);RUN(exit_pending,PS1_PAUSE_RETURN_LAUNCHER);
 SCRIPT(exit_before_armed,1,65);RUN(exit_before_armed,PS1_PAUSE_RETURN_LAUNCHER);
 SCRIPT(exit_immediate,64);Model exit=sequence(exit_immediate,1);Ps1MenuConfig ce=config(&exit);assert(ps1_pause_run(&ce)==PS1_PAUSE_RETURN_LAUNCHER&&exit.cursor==1&&!exit.frames&&!exit.sleeps);
 SCRIPT(error_pending,0,1);Model error=sequence(error_pending,2);error.error_at=2;Ps1MenuConfig cf=config(&error);assert(ps1_pause_run(&cf)==PS1_PAUSE_ERROR&&error.cursor==2&&error.frames==2&&error.sleeps==1);
 SCRIPT(neutral,0);Model model=sequence(neutral,1);Ps1MenuConfig c=config(&model);Ps1Pause state;assert(ps1_pause_init(&state,&c)==0&&state.input_armed&&!state.selection&&!state.pending_continue);
 for(unsigned i=0;i<30;i++)assert(ps1_pause_step(&state,&c,PS1_MENU_DOWN)==0);assert(state.selection==4);ps1_pause_step(&state,&c,0);ps1_pause_step(&state,&c,PS1_MENU_UP|PS1_MENU_DOWN);assert(state.selection==4);ps1_pause_step(&state,&c,0);ps1_pause_step(&state,&c,PS1_MENU_A);assert(state.status==PS1_PAUSE_RETURN_LAUNCHER);
 model.cursor=0;assert(ps1_pause_init(&state,&c)==0);assert(ps1_pause_step(&state,&c,PS1_MENU_A)==0&&state.pending_continue);assert(ps1_pause_step(&state,&c,PS1_MENU_A|PS1_MENU_B|PS1_PAUSE_MENU_BUTTON)==0);assert(ps1_pause_step(&state,&c,PS1_MENU_B)==0);assert(ps1_pause_step(&state,&c,0)==PS1_PAUSE_CONTINUE);
 /* SD/root/entries are deliberately null in every valid config. */
 assert(!c.sd&&!c.root&&!c.entries&&!c.capacity);model.cursor=0;assert(ps1_pause_init(&state,&c)==0);ps1_pause_step(&state,&c,PS1_MENU_DOWN);ps1_pause_draw(&state,&c);assert(pixels[0]==0xffeac45c&&pixels[479*640]!=0);
 if(argc>1){FILE*f=fopen(argv[1],"wb");assert(f&&fwrite(pixels,1,sizeof pixels,f)==sizeof pixels&&!fclose(f));}
 Ps1MenuConfig bad=c;bad.buttons=0;assert(ps1_pause_run(&bad)==PS1_PAUSE_ERROR);bad=c;bad.present=0;assert(ps1_pause_init(&state,&bad)==PS1_PAUSE_ERROR);bad=c;bad.sleep=0;assert(ps1_pause_run(&bad)==PS1_PAUSE_ERROR);bad=c;bad.rgba=0;assert(ps1_pause_run(&bad)==PS1_PAUSE_ERROR);assert(ps1_pause_run(0)==PS1_PAUSE_ERROR);
 puts("PASS: pause A/B/Minus entry and Continue release gates, 5 actions with restart confirmation and settings, D-pad repeat/wrap, game Start omitted, owner stop priority during held/release waits, present-error propagation, 640x480 drawing, null SD/no filesystem calls");return 0;
}
