#ifndef TEST_SWITCH_H
#define TEST_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t Result;
typedef uint32_t u32;
typedef uint64_t u64;
typedef enum {TimeType_UserSystemClock, TimeType_LocalSystemClock} TimeType;
typedef struct { uint16_t year; uint8_t hour, minute, second; } TimeCalendarTime;
typedef struct { bool battery_charging; } PsmBatteryChargeInfoFields;
#define R_FAILED(rc) ((rc) != 0)
#define R_SUCCEEDED(rc) ((rc) == 0)
Result timeGetCurrentTime(TimeType source, u64 *out);
Result timeToCalendarTimeWithMyRule(u64 utc, TimeCalendarTime *out, void *details);
Result psmInitialize(void);
Result psmGetBatteryChargePercentage(u32 *out);
Result psmGetRawBatteryChargePercentage(double *out);
Result psmGetBatteryChargeInfoFields(PsmBatteryChargeInfoFields *out);
void psmExit(void);
typedef struct { int unused; } FsFileSystem;
typedef struct { u64 modified; bool is_valid; } FsTimeStampRaw;
FsFileSystem *fsdevGetDeviceFileSystem(const char *name);
Result fsFsGetFileTimeStampRaw(FsFileSystem *fs, const char *path, FsTimeStampRaw *out);
u64 armGetSystemTick(void);
u64 armNsToTicks(u64 ns);
#endif
