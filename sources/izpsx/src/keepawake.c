#include "keepawake.h"
void ps1_keepawake_step(Ps1KeepAwake*k,uint64_t tick){
 if(!k||!k->enabled)return;
 if(!k->fn||!k->controller||!k->interval_ticks||k->interval_ticks>INT64_MAX){
  k->last_result=UINT32_MAX;if(k->failures!=UINT64_MAX)k->failures++;k->enabled=0;return;
 }
 uint64_t step=tick-k->last_tick;k->last_tick=tick;
 if(k->calls&&step>INT64_MAX){k->last_call_tick=tick;return;}
 if(k->calls&&tick-k->last_call_tick<k->interval_ticks)return;
 k->last_call_tick=tick;if(k->calls!=UINT64_MAX)k->calls++;
 uint32_t result=((uint32_t(*)(void*))(uintptr_t)k->fn)((void*)(uintptr_t)k->controller);
 k->last_result=result;
 if(result){if(k->failures!=UINT64_MAX)k->failures++;k->enabled=0;}
}
