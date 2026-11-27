#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <time.h>
static unsigned status_copies;
static void *counted_memcpy(void *dst,const void *src,size_t bytes){
 status_copies++;return memcpy(dst,src,bytes);
}
#define memcpy counted_memcpy
static time_t mock_time(time_t *v){if(v)*v=-1;return -1;}
#define time mock_time
#include "menu-status.h"
#undef memcpy
#include <assert.h>
static u64 ticks;
static unsigned clock_calls,battery_calls,psm_opens,psm_closes,tick_conversions;
static bool deny_clock,deny_battery,deny_percentage;
u64 armGetSystemTick(void){return ticks;}
u64 armNsToTicks(u64 ns){tick_conversions++;return ns/1000000;}
Result timeGetCurrentTime(TimeType type,u64 *out){(void)type;clock_calls++;*out=1;return deny_clock;}
Result timeToCalendarTimeWithMyRule(u64 utc,TimeCalendarTime *out,void *details){assert(utc==1&&!details);*out=(TimeCalendarTime){2026,23,59,59};return 0;}
Result psmInitialize(void){if(deny_battery)return 1;psm_opens++;return 0;}
Result psmGetBatteryChargePercentage(u32 *out){battery_calls++;*out=71;return deny_percentage;}
Result psmGetRawBatteryChargePercentage(double *out){*out=71;return deny_percentage;}
Result psmGetBatteryChargeInfoFields(PsmBatteryChargeInfoFields *out){out->battery_charging=true;return 0;}
void psmExit(void){psm_closes++;assert(psm_closes==psm_opens);}
static uint8_t a[IZ2_STATUS_OFFSET+20],b[IZ2_STATUS_OFFSET+20];
static bool sample(u64 t){ticks=t;bool changed=iz2_menu_status_publish(a,b,true);assert(!memcmp(a+IZ2_STATUS_OFFSET,b+IZ2_STATUS_OFFSET,16));assert(a[IZ2_STATUS_OFFSET-1]==0xab&&a[IZ2_STATUS_OFFSET+16]==0xab);assert(psm_opens==psm_closes);return changed;}
int main(void){
 memset(a,0xab,sizeof a);memset(b,0xab,sizeof b);
 assert(sample(0));assert(!sample(299));assert(!clock_calls&&!battery_calls);
 assert(sample(300));assert(clock_calls==1&&!battery_calls&&iz2_status.minute_of_day==1439);
 assert(sample(600));assert(battery_calls==1&&iz2_status.battery==(71|256));
 assert(sample(1300));assert(iz2_status.minute_of_day==0&&clock_calls==1);
 const unsigned copies=status_copies,conversions=tick_conversions;
 for(u64 tick=1301;tick<5600;tick+=16)assert(!sample(tick));
 assert(status_copies==copies&&tick_conversions==conversions);
 // Repair either copy when a transition restores older menu contents.
 b[IZ2_STATUS_OFFSET]^=1;
 assert(!sample(5598));assert(status_copies==copies+1);
 a[IZ2_STATUS_OFFSET]^=1;
 assert(sample(5599));assert(status_copies==copies+2);
 // Simulate a host hold with pointers unavailable to the menu. Must not touch them or services.
 unsigned cc=clock_calls,bc=battery_calls; ticks=65000;
 assert(!iz2_menu_status_publish(NULL,NULL,false));assert(clock_calls==cc&&battery_calls==bc);
 sample(65001);assert(clock_calls==2);iz2_menu_status_close();assert(psm_closes==2);
 // A failed battery query still relinquishes its service before application ownership.
 deny_percentage=true;sample(70000);sample(70600);assert(!iz2_status.battery_magic&&psm_closes==3);
 iz2_menu_status_close();deny_clock=deny_battery=true;sample(80000);sample(80600);
 assert(!iz2_status.clock_magic&&!iz2_status.battery_magic&&psm_closes==3);
 cc=clock_calls;
 assert(!sample(82599)&&clock_calls==cc);
 assert(!sample(82600)&&clock_calls==cc+2);
 puts("PASS: visible status changes request redraw; no writes/services during host hold; no retained battery session; failed queries close; deferred first-frame sampling; bounded memory; rollover");
 puts("PASS: 269 idle polls with zero status copies and zero tick conversions; stale buffers repaired independently");
}
