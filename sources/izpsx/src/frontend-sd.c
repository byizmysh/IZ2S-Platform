/* Cooperative SD entry shares the production frontend's conversion, controls,
 * options and platform adapters. A current longjmp frame exists for each call. */
#include "frontend.c"
/* Qualify executable cache maintenance in this actual process before enabling
 * page publication. Exactly one private RX page; 64 changed warm executions. */
uint32_t ps1_sd_jit_probe(Ps1Context*c){
 ps1_context=c;uint64_t bytes=c->jit_bytes;
 if(__builtin_setjmp(panic_frame)){c->jit_bytes=bytes;return c->base.error;}
 c->jit_bytes=4096;ps1_ro_map();uint32_t*code=(void*)(uintptr_t)c->jit_rw;
 for(unsigned i=1;i<=64;i++){
  code[0]=0xd503245f;code[1]=0x52800000u|((0x5a00u+i)<<5);code[2]=0xd65f03c0;code[3]=0;
  if(ps1_ro_publish(c,0,16))ps1_fatal(153);
  if(((uint32_t(*)(void))(uintptr_t)c->jit_rx)()!=0x5a00u+i)ps1_fatal(154);
 }
 ps1_ro_unmap();memset((void*)(uintptr_t)c->jit_source,0,4096);memset((void*)(uintptr_t)c->jit_rw,0,4096);
 c->jit_bytes=bytes;c->jit_flushes=c->jit_patches=c->jit_bytes_published=0;return 0;
}
uint32_t ps1_sd_core_begin(Ps1Context*c){
 ps1_context=c;HeavyContext*b=&c->base;
 if(__builtin_setjmp(panic_frame))return b->error;
 b->status=1;b->error=0;c->audio_hash=1469598103934665603ull;completed_frames=0;
 variable_count=0;previous_hash=0;memset(live_buttons,0,sizeof live_buttons);
 ps1_ro_map();ps1_platform_init();if(ps1_audio_open())ps1_fatal(140);
 retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample_batch(audio);retro_set_audio_sample(sample);retro_set_input_poll(poll);retro_set_input_state(input);
 b->boot_start=tick();retro_init();struct retro_game_info game={.path=(const char*)(uintptr_t)c->game_path};
 if(!retro_load_game(&game)){
  Ps1FileIO*io=(void*)(uintptr_t)b->wad;Ps1SdDirectory*sd=io?(void*)(uintptr_t)io->directory:0;
  /* A rejected image is recoverable only at a known file/audio/JIT boundary.
   * Transport failures keep their capabilities for diagnosis. Cards have
   * not been opened yet, so this path never publishes a blank save card. */
  if((io&&io->errors)||(sd&&(sd->transport||sd->leaked_handle))||c->mapping_unknown)ps1_fatal(63);
  retro_unload_game();retro_deinit();for(unsigned i=0;i<variable_count;i++){free(variables[i].key);free(variables[i].value);}variable_count=0;
  if(ps1_audio_close())ps1_fatal(142);ps1_ro_unmap();b->error=63;b->status=2;return 63;
 }
 if(Config.HLE)ps1_fatal(66);b->flags|=PS1_FLAG_BIOS_LOADED;
 retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD);
 retro_set_controller_port_device(1,RETRO_DEVICE_JOYPAD);
 Ps1FileIO*io=(void*)(uintptr_t)b->wad;
 if(ps1_cards_open((void*)(uintptr_t)io->directory,Mcd1Data,Mcd2Data))ps1_fatal(151);
 struct retro_system_av_info av={0};retro_get_system_av_info(&av);if(av.timing.fps<49||av.timing.fps>61)ps1_fatal(64);
 c->reserved=(uint64_t)(av.timing.fps*1000000);b->boot_end=tick();b->run_start=b->boot_end;b->phase=2;return 0;
}
extern int in_type[8];
void ps1_sd_core_ports(uint32_t connected_mask){
 for(unsigned port=0;port<8;port++){
  unsigned enabled=port<PS1_PLAYER_COUNT&&(port<2||(ps1_active_settings&&ps1_active_settings->multitap));
  unsigned device=(enabled&&(connected_mask&(1u<<port)))?RETRO_DEVICE_JOYPAD:RETRO_DEVICE_NONE;
  int expected=device==RETRO_DEVICE_NONE?PSE_PAD_TYPE_NONE:PSE_PAD_TYPE_STANDARD;
  if(in_type[port]!=expected)retro_set_controller_port_device(port,device);
 }
}
uint32_t ps1_sd_core_frame4(uint32_t buttons,uint32_t player2,uint32_t player3,uint32_t player4){
 Ps1Context*c=ps1_context;HeavyContext*b=&c->base;if(__builtin_setjmp(panic_frame))return b->error;
 live_buttons[0]=buttons;live_buttons[1]=player2;live_buttons[2]=player3;live_buttons[3]=player4;uint64_t begin=tick();retro_run();uint64_t end=tick();
 if(ps1_cards_poll(end,b->tick_hz,0))ps1_fatal(152);
 if(ps1_audio_commit_frame())ps1_fatal(143);
 uint64_t delta=end-begin;b->step_ticks+=delta;if(delta>b->max_step_ticks)b->max_step_ticks=delta;
 if(completed_frames<b->trace_capacity/sizeof(HeavyTrace))((HeavyTrace*)(uintptr_t)b->trace)[completed_frames]=(HeavyTrace){(uint32_t)(completed_frames+1),buttons,delta,b->image_hash,((uint64_t)c->width<<32)|c->height};
 if(completed_frames!=UINT64_MAX)completed_frames++;b->frames_done=completed_frames>UINT32_MAX?UINT32_MAX:(uint32_t)completed_frames;
 return 0;
}
/* Preserve the existing finite two-player qualification entry. */
uint32_t ps1_sd_core_frame(uint32_t p1,uint32_t p2){return ps1_sd_core_frame4(p1,p2,0,0);}
uint32_t ps1_sd_core_reset(void){
 Ps1Context*c=ps1_context;HeavyContext*b=&c->base;if(__builtin_setjmp(panic_frame))return b->error;
 if(ps1_cards_poll(tick(),b->tick_hz,1))ps1_fatal(152);
 retro_reset();completed_frames=0;b->frames_done=0;b->run_start=tick();return 0;
}
uint32_t ps1_sd_core_end(void){
 Ps1Context*c=ps1_context;HeavyContext*b=&c->base;if(__builtin_setjmp(panic_frame))return b->error;
 if(ps1_cards_poll(tick(),b->tick_hz,1))ps1_fatal(152);
 retro_unload_game();retro_deinit();for(unsigned i=0;i<variable_count;i++){free(variables[i].key);free(variables[i].value);}
 if(ps1_audio_close())ps1_fatal(142);ps1_ro_unmap();b->run_end=tick();b->status=2;return 0;
}
