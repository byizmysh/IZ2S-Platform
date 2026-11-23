#include "common/HostSys.h"
#include "common/Threading.h"
#include "native-system.h"
#include <cstdio>
#include <cstdlib>
u64 GetPhysicalMemory(){return iz_process_memory(0);}
u64 GetAvailablePhysicalMemory(){return iz_process_memory(1);}
u64 GetTickFrequency(){return iz_tick_frequency();}
u64 GetCPUTicks(){return iz_system_ticks();}
std::string GetOSVersionString(){return "Nintendo Switch 2 / libounce";}
bool Common::InhibitScreensaver(bool){return false;}
bool Common::PlaySoundAsync(const char*){return false;}
void Common::SetMousePosition(int,int){}
bool Common::AttachMousePositionCb(std::function<void(int,int)>){return false;}
void Common::DetachMousePositionCb(){}
void Threading::Sleep(int ms){iz_sleep_ns(ms>0?(u64)ms*1000000:0);}
void Threading::SleepUntil(u64 ticks){const u64 now=iz_system_ticks();if(ticks>now)iz_sleep_ns(((ticks-now)*1000000000)/iz_tick_frequency());}
