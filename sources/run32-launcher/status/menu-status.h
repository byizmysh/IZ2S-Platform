#ifndef IZ2S_MENU_STATUS_H
#define IZ2S_MENU_STATUS_H

#include <switch.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The 16-byte status record sits between activity and tile animation. */
#define IZ2_STATUS_OFFSET (1024u + 0xf0080u)
#define IZ2_CLOCK_MAGIC UINT32_C(0x495a434b)
#define IZ2_BATTERY_MAGIC UINT32_C(0x495a4254)
#define IZ2_BATTERY_CHARGING (UINT32_C(1) << 8)

typedef struct {
    uint32_t clock_magic, minute_of_day, battery_magic, battery;
} IzMenuStatus;

_Static_assert(sizeof(IzMenuStatus) == 16, "IZ2S menu status record size");

static IzMenuStatus iz2_status;
static u64 iz2_clock_tick, iz2_battery_tick;
static bool iz2_clock_sampled, iz2_battery_sampled, iz2_psm_open;
static uint32_t iz2_clock_source, iz2_battery_source;
static Result iz2_clock_result, iz2_battery_result;
static bool iz2_status_started;
static u64 iz2_status_start;
static uint32_t iz2_local_seconds;

static void iz2_menu_status_sample_clock(u64 ticks)
{
    u64 utc = 0;
    TimeCalendarTime local = {0};
    iz2_status.clock_magic = 0;
    iz2_clock_source = 0;
    iz2_clock_result = timeGetCurrentTime(TimeType_UserSystemClock, &utc);
    if (R_FAILED(iz2_clock_result))
        iz2_clock_result = timeGetCurrentTime(TimeType_LocalSystemClock, &utc);
    if (R_SUCCEEDED(iz2_clock_result))
        iz2_clock_result = timeToCalendarTimeWithMyRule(utc, &local, NULL);
    if (R_SUCCEEDED(iz2_clock_result) && local.year >= 2000 &&
        local.hour < 24 && local.minute < 60) {
        iz2_status.minute_of_day = (uint32_t)local.hour * 60u + local.minute;
        iz2_local_seconds = iz2_status.minute_of_day * 60u + local.second;
        iz2_clock_source = 1;
    } else {
        struct tm fallback;
        time_t now = time(NULL);
        bool have_local = now != (time_t)-1 && localtime_r(&now, &fallback) && fallback.tm_year >= 100 &&
                          fallback.tm_hour >= 0 && fallback.tm_hour < 24 &&
                          fallback.tm_min >= 0 && fallback.tm_min < 60;
        if (!have_local) return;
        iz2_status.minute_of_day = (uint32_t)(fallback.tm_hour * 60 + fallback.tm_min);
        iz2_local_seconds = iz2_status.minute_of_day * 60u + (uint32_t)fallback.tm_sec;
        iz2_clock_source = 2;
    }
    iz2_status.clock_magic = IZ2_CLOCK_MAGIC;
}

static void iz2_menu_status_sample_battery(void)
{
    iz2_status.battery_magic = 0;
    iz2_battery_source = 0;
    if (!iz2_psm_open) {
        iz2_battery_result = psmInitialize();
        if (R_FAILED(iz2_battery_result)) return;
        iz2_psm_open = true;
    }

    u32 percentage = 0;
    iz2_battery_result = psmGetBatteryChargePercentage(&percentage);
    if (R_SUCCEEDED(iz2_battery_result) && percentage <= 100) {
        iz2_battery_source = 1;
    } else {
        double raw = 0;
        iz2_battery_result = psmGetRawBatteryChargePercentage(&raw);
        if (R_FAILED(iz2_battery_result) || !(raw >= 0.0 && raw <= 100.0)) return;
        percentage = (u32)(raw + .5);
        iz2_battery_source = 2;
    }

    PsmBatteryChargeInfoFields info = {0};
    bool charging = R_SUCCEEDED(psmGetBatteryChargeInfoFields(&info)) && info.battery_charging;
    iz2_status.battery = percentage | (charging ? IZ2_BATTERY_CHARGING : 0u);
    iz2_status.battery_magic = IZ2_BATTERY_MAGIC;
}

static void iz2_menu_status_report(void)
{
#ifdef IZ2_MENU_STATUS_DIAGNOSTICS
    static uint32_t previous[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    const uint32_t current[] = {iz2_clock_source, iz2_battery_source, iz2_clock_result, iz2_battery_result};
    if (!memcmp(previous, current, sizeof current)) return;
    memcpy(previous, current, sizeof current);
    FILE *log = fopen("sdmc:/iz2s/izlauncher-status.log", "w");
    if (!log) return;
    fprintf(log, "IZ2S 0.32 status publisher\nclock=%s rc=0x%08x\nbattery=%s rc=0x%08x\n",
            iz2_clock_source == 1 ? "system" : iz2_clock_source == 2 ? "localtime" :
            "unavailable",
            (unsigned)iz2_clock_result,
            iz2_battery_source == 1 ? "psm" : iz2_battery_source == 2 ? "psm-raw" : "unavailable",
            (unsigned)iz2_battery_result);
    fclose(log);
#endif
}

void iz2_menu_status_publish(uint8_t *controls, uint8_t *snapshot, bool menu_owned)
{
    if (!menu_owned) return;
    const u64 now = armGetSystemTick();
    if (!iz2_status_started) {
        iz2_status_started = true;
        iz2_status_start = now;
    }
    const u64 age = now - iz2_status_start;
    const u64 clock_interval = iz2_status.clock_magic ? UINT64_C(60000000000) : UINT64_C(2000000000);
    if (age >= armNsToTicks(UINT64_C(300000000)) &&
        (!iz2_clock_sampled || now - iz2_clock_tick >= armNsToTicks(clock_interval))) {
        iz2_menu_status_sample_clock(now);
        iz2_clock_tick = now;
        iz2_clock_sampled = true;
    }
    if (iz2_status.clock_magic) {
        u64 elapsed = (now - iz2_clock_tick) / armNsToTicks(UINT64_C(1000000000));
        iz2_status.minute_of_day = (uint32_t)((iz2_local_seconds + elapsed) / 60u % 1440u);
    }
    if (age >= armNsToTicks(UINT64_C(600000000)) &&
        (!iz2_battery_sampled || now - iz2_battery_tick >= armNsToTicks(UINT64_C(5000000000)))) {
        iz2_menu_status_sample_battery();
        if (iz2_psm_open) psmExit();
        iz2_psm_open = false;
        iz2_battery_tick = now;
        iz2_battery_sampled = true;
    }
    iz2_menu_status_report();
    memcpy(controls + IZ2_STATUS_OFFSET, &iz2_status, sizeof iz2_status);
    memcpy(snapshot + IZ2_STATUS_OFFSET, &iz2_status, sizeof iz2_status);
}

static void iz2_menu_status_close(void)
{
    if (iz2_psm_open) psmExit();
    iz2_psm_open = false;
    iz2_clock_sampled = iz2_battery_sampled = false;
    iz2_status_started = false;
    iz2_status = (IzMenuStatus){0};
}

#endif
