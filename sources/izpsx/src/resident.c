/* One resident CPU thread owns menu, PS1, SD and graphics for the session.
 * No host transport or background loader is used here. */
#include "resident.h"
#include "display.h"
#include "audio.h"
#include "keepawake.h"
#include "memory-card.h"
#include "sd-layout.h"
#include <string.h>
#include <stdlib.h>
static void imports(Ps1Context*c){
 ps1_context=c;for(unsigned i=0;i<PS1_IMPORT_COUNT;i++)ps1_imports[i]=((uintptr_t*)(uintptr_t)c->imports)[i];
}
/* Volatile loops deliberately avoid libc, imports or globals while resetting
 * the very data region containing those globals. Config and stack are outside. */
__attribute__((noinline)) static void reset_image(Ps1Resident*r){
 volatile uint8_t*d=(void*)(uintptr_t)r->data_base;const volatile uint8_t*s=(void*)(uintptr_t)r->pristine;
 for(uint64_t i=0;i<r->data_bytes;i++)d[i]=s[i];
 for(uint64_t i=r->data_bytes;i<r->data_region;i++)d[i]=0;
}
static void sleep_native(void*argument,uint64_t ns){Ps1Context*c=argument;Ps1Display*d=(void*)(uintptr_t)c->base.display;((void(*)(uint64_t))(uintptr_t)d->fn_sleep)(ns);}
static int picker_present(void*,const uint32_t*,unsigned,unsigned);
static uint32_t pause_buttons(void*argument){(void)argument;return ps1_display_menu_buttons();}
static void pause_poll(void*argument){(void)pause_buttons(argument);}
static uint32_t buttons(void*argument){
 Ps1Context*c=argument;uint32_t out=ps1_display_menu_buttons();
 if(ps1_display_take_menu_request()){
  Ps1MenuConfig config={.rgba=(void*)(uintptr_t)c->base.frame,.user=c,.buttons=pause_buttons,.present=picker_present,.sleep=sleep_native};
  int result=ps1_pause_run(&config);ps1_display_take_menu_request();
  if(result==PS1_PAUSE_ERROR){((Ps1Resident*)(uintptr_t)c->autonomy)->error=206;c->base.stop=1;}
  if(result==PS1_PAUSE_RETURN_LAUNCHER)c->base.stop=1;
  return c->base.stop?PS1_MENU_EXIT_BUTTON:0;
 }
 return out;
}
static int picker_present(void*argument,const uint32_t*rgba,unsigned w,unsigned h){
 Ps1Context*c=argument;(void)rgba;c->width=w;c->height=h;c->video_calls++;
 Ps1Resident*r=(void*)(uintptr_t)c->autonomy;r->menu_frames++;return ps1_display_present();
}
int ps1_resident_pause(Ps1Context*c){
 if(!c||!c->autonomy||!c->base.display||!c->width||!c->height||c->width>1024||c->height>512)return 1;
 Ps1Resident*r=(void*)(uintptr_t)c->autonomy;uint64_t begin=((uint64_t(*)(void))(uintptr_t)c->base.tick)();
 unsigned width=c->width,height=c->height,calls=c->video_calls;size_t bytes=(size_t)width*height*4;
 if(bytes>c->base.frame_capacity)return 1;
 void*saved=malloc(bytes);if(!saved)return 2;memcpy(saved,(void*)(uintptr_t)c->base.frame,bytes);
 if(ps1_audio_pause_poll(pause_poll,c)){free(saved);return 3;}
 if((c->base.flags&PS1_FLAG_MEMORY_CARDS)&&ps1_cards_poll(begin,c->base.tick_hz,1)){free(saved);return 7;}
 r->state=3;
 Ps1MenuConfig config={.rgba=(void*)(uintptr_t)c->base.frame,.user=c,.buttons=pause_buttons,.present=picker_present,.sleep=sleep_native,.save_status=(c->base.flags&PS1_FLAG_MEMORY_CARDS)?"MEMORY CARDS SAVED TO SD":NULL};
 int result=ps1_pause_run(&config);ps1_display_take_menu_request();
 if(result==PS1_PAUSE_ERROR){free(saved);return 4;}
 if(result==PS1_PAUSE_CONTINUE&&!c->base.stop){
  memcpy((void*)(uintptr_t)c->base.frame,saved,bytes);c->width=width;c->height=height;c->video_calls=calls;
  if(ps1_display_present()||ps1_audio_resume()){free(saved);return 5;}
 }else{if(result==PS1_PAUSE_CHOOSE_GAME)c->base.flags|=PS1_FLAG_RETURN_PICKER;c->base.stop=1;}
 free(saved);r->state=2;
 uint64_t end=((uint64_t(*)(void))(uintptr_t)c->base.tick)();c->base.run_start+=end-begin;return 0;
}
static uint32_t launcher_present(void*argument){(void)argument;return (uint32_t)ps1_display_present();}
static void restore_game_commands(Ps1Context*c,Ps1Resident*r){Ps1Display*d=(void*)(uintptr_t)c->base.display;memcpy(d->commands,r->game_commands,sizeof d->commands);}
static int outside(Ps1Resident*r,uint64_t pointer,uint64_t bytes){return pointer&&bytes&&pointer+bytes>=pointer&&(pointer+bytes<=r->data_base||pointer>=r->data_base+r->data_region);}
static int optional_outside(Ps1Resident*r,uint64_t pointer,uint64_t bytes){return !pointer&&!bytes?1:outside(r,pointer,bytes);}
static int layout_valid(Ps1Context*c,Ps1Resident*r){
 if(!r||r->magic!=PS1_RESIDENT_MAGIC||r->version!=1||!r->data_base||!r->data_bytes||r->data_bytes>r->data_region||r->data_region>16*1024*1024||r->data_base+r->data_region<r->data_base)return 0;
 HeavyContext*b=&c->base;
 if(!outside(r,(uintptr_t)c,sizeof *c)||!outside(r,(uintptr_t)r,sizeof *r)||!outside(r,(uintptr_t)&r,sizeof r)||
    !outside(r,r->pristine,r->data_bytes)||!outside(r,c->imports,PS1_IMPORT_COUNT*sizeof(uintptr_t))||
    !outside(r,b->display,sizeof(Ps1Display))||!outside(r,b->wad,sizeof(Ps1FileIO))||!outside(r,b->reserved,sizeof(Ps1Audio))||
    !outside(r,r->sd_directory,sizeof(Ps1SdDirectory))||!r->entry_capacity||r->entry_capacity>4096||!outside(r,r->entries,r->entry_capacity*sizeof(Ps1SdEntry))||
    !outside(r,b->stack,b->stack_bytes)||b->stack_bytes<4096||!outside(r,b->heap,b->heap_bytes)||
    b->frame<4096||b->frame_capacity<640*480*4||b->frame_capacity>UINT64_MAX-4096||!outside(r,b->frame-4096,b->frame_capacity+4096)||
    !optional_outside(r,c->audio,c->audio_capacity)||!optional_outside(r,b->trace,b->trace_capacity)||!optional_outside(r,b->error_text,b->error_capacity)||
    !optional_outside(r,c->performance,c->performance_capacity)||!outside(r,c->jit_source,c->jit_bytes)||!outside(r,c->jit_rw,c->jit_bytes)||!outside(r,c->jit_rx,c->jit_bytes)||
    (c->input&&(!b->frames_target||!outside(r,c->input,(uint64_t)b->frames_target*4)))||c->file_count>64||
    (c->file_count&&!outside(r,c->files,c->file_count*sizeof(Ps1File)))||
    (c->game_path&&!outside(r,c->game_path,1)))return 0;
 Ps1FileIO*io=(void*)(uintptr_t)b->wad;Ps1Audio*a=(void*)(uintptr_t)b->reserved;Ps1SdDirectory*sd=(void*)(uintptr_t)r->sd_directory;
 Ps1Display*d=(void*)(uintptr_t)b->display;
 if(d->reserved&&!outside(r,d->reserved,sizeof(Ps1KeepAwake)))return 0;
 if(io->directory!=r->sd_directory||!outside(r,io->buffer,io->capacity)||io->capacity>65536||!outside(r,(uintptr_t)sd->buffer,sd->capacity)||sd->capacity>65536||
    !outside(r,a->arena,a->arena_bytes)||!outside(r,r->launcher.snapshot,r->launcher.storage_bytes)||r->launcher.storage_bytes!=RESIDENT_MENU_BYTES||
    r->launcher.storage_capacity<r->launcher.storage_bytes||!outside(r,r->launcher.storage,r->launcher.storage_capacity))return 0;
 for(unsigned i=0;i<PS1_IMPORT_COUNT;i++)if(!outside(r,((uintptr_t*)(uintptr_t)c->imports)[i],4))return 0;
 return 1;
}
static int resources_pending(Ps1Context*c,Ps1Resident*r){
 Ps1Audio*a=(void*)(uintptr_t)c->base.reserved;Ps1FileIO*io=(void*)(uintptr_t)c->base.wad;Ps1SdDirectory*sd=(void*)(uintptr_t)r->sd_directory;
 return c->pages_mapped||c->mapping_unknown||a->opened||a->pending||a->uncertain||io->opened||io->errors||sd->leaked_handle;
}
static uint32_t resident_failure(Ps1Context*c,Ps1Resident*r,uint32_t error){r->error=error;r->state=5;c->base.error=error;return error;}
static void clear_game_counters(Ps1Context*c){
 HeavyContext*b=&c->base;b->status=1;b->error=0;b->frames_done=b->stop=0;b->flags=(b->flags&(PS1_FLAG_PAGE_PUBLICATION|PS1_FLAG_CONTINUOUS))|PS1_FLAG_BIOS_REQUIRED|PS1_FLAG_MEMORY_CARDS;b->phase=80;
 b->heap_peak=b->step_ticks=b->min_step_ticks=b->max_step_ticks=0;b->changed_frames=b->last_frame=0;
 c->audio_bytes=c->audio_hash=c->audio_frames=0;c->width=640;c->height=480;c->video_calls=0;
 c->jit_flushes=c->jit_patches=c->jit_bytes_published=0;c->log_bytes=0;c->pages_mapped=c->mapping_unknown=c->transport=0;c->performance_count=0;
 Ps1FileIO*io=(void*)(uintptr_t)b->wad;if(io){io->reads=io->bytes=io->opened=io->errors=0;}
 Ps1Audio*a=(void*)(uintptr_t)b->reserved;if(a)memset((uint8_t*)a+128,0,sizeof(*a)-128);
 Ps1Display*d=(void*)(uintptr_t)b->display;d->submissions=d->error=d->observed_buttons=0;d->input_samples=0;
}
uint32_t ps1_resident_main(Ps1Context*c){
 if(!c)return 201;
 Ps1Resident*r=(void*)(uintptr_t)c->autonomy;
 if(!layout_valid(c,r))return 201;
 if(resources_pending(c,r))return resident_failure(c,r,204);
 /* Must be outside .data/.bss: reset_image preserves this C stack and config. */
 imports(c);Ps1Display*d=(void*)(uintptr_t)c->base.display;memcpy(r->game_commands,d->commands,sizeof d->commands);
 r->launcher.render=(uintptr_t)launcher_present;r->launcher.render_argument=(uintptr_t)c;
 int show_launcher=0;
 for(;;){
  reset_image(r);imports(c);clear_game_counters(c);ps1_platform_init();restore_game_commands(c,r);
  if(show_launcher){
   r->state=4;c->base.phase=83;
   uint64_t saved[8];memcpy(saved,d->commands,sizeof saved);
   memcpy(d->commands,r->menu_commands,sizeof d->commands);
   uint32_t result=resident_launcher_run(&r->launcher);memcpy(d->commands,saved,sizeof saved);
   if(result==RESIDENT_STOP){r->state=6;c->base.status=2;c->base.phase=84;c->base.error=0;return 0;}
   if(result!=RESIDENT_SELECT_PS1){r->error=202;r->state=5;c->base.error=202;return 202;}
   c->base.stop=0;show_launcher=0;
  }
  r->state=1;c->base.phase=80;
  Ps1Menu menu;Ps1MenuConfig config={.sd=(void*)(uintptr_t)r->sd_directory,.root=IZPSX_SD_GAMES,.entries=(void*)(uintptr_t)r->entries,.capacity=r->entry_capacity,.rgba=(void*)(uintptr_t)c->base.frame,.user=c,.buttons=buttons,.present=picker_present,.sleep=sleep_native};
  if(ps1_menu_init(&menu,&config)!=PS1_MENU_RUNNING)return resident_failure(c,r,203);
  if(((Ps1SdDirectory*)(uintptr_t)r->sd_directory)->leaked_handle)return resident_failure(c,r,204);
  int selection=ps1_menu_run(&menu,&config);
  if(r->error)return resident_failure(c,r,r->error);
  if(((Ps1SdDirectory*)(uintptr_t)r->sd_directory)->leaked_handle)return resident_failure(c,r,204);
  if(selection==PS1_MENU_ERROR)return resident_failure(c,r,205);
  if(selection!=PS1_MENU_SELECTED){r->returns++;show_launcher=1;continue;}
  memcpy(r->selected,menu.selected,strlen(menu.selected)+1);
  char virtual_path[PS1_SD_PATH_MAX];strcpy(virtual_path,"/ps1");strcat(virtual_path,menu.selected+strlen(IZPSX_SD_GAMES));
  c->game_path=(uintptr_t)virtual_path;c->file_count=0;d->submissions=0;c->base.stop=0;
  r->state=2;c->base.phase=81;uint32_t result=ps1_game_main(&c->base);
  r->last_game_frames=ps1_game_frames_done();r->last_game_seconds_ticks=c->base.run_end-c->base.run_start;r->games++;
  if(result||resources_pending(c,r)){
   /* The failed core's mappings/buffers are retained. Never reset or run a
    * second core over memory whose ownership has become uncertain. */
   r->error=result?result:204;r->state=5;c->base.error=r->error;return r->error;
  }
  r->returns++;show_launcher=(c->base.flags&PS1_FLAG_RETURN_PICKER)?0:1;
 }
}
