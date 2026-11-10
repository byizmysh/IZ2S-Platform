#ifndef IZPSX_SETTINGS_H
#define IZPSX_SETTINGS_H
#include "fs-dir.h"
enum {PS1_VIDEO,PS1_SOUND,PS1_EMULATION,PS1_CONTROLS,PS1_SYSTEM,PS1_ABOUT,PS1_CATEGORIES};
enum {
 PS1_FPS,PS1_ASPECT,PS1_FILTER,PS1_SCANLINES,PS1_BRIGHTNESS,PS1_CROP,PS1_SHOW_FPS,
 PS1_VOLUME,PS1_MUTE,PS1_MONO,PS1_AUDIO_BUFFER,PS1_REVERB,PS1_INTERPOLATION,PS1_CD_AUDIO,
 PS1_CPU,PS1_CPU_CLOCK,PS1_REGION,PS1_BOOT_LOGO,PS1_DITHER,PS1_INTERLACE,
 PS1_SWAP_AB,PS1_STICK_DPAD,PS1_DEADZONE,PS1_TURBO,
 PS1_REMEMBER_GAME,PS1_EXTENSIONS,PS1_ACCENT,PS1_SAVE_DELAY,PS1_OPTION_COUNT
};
enum {PS1_CONTROL_COUNT=4,PS1_PLAYER_COUNT=4};
enum {PS1_MULTITAP_OFF,PS1_MULTITAP_PORT1,PS1_MULTITAP_PORT2,PS1_MULTITAP_COUNT};
/* AUTO follows the original role; explicit slots allow a replacement pad. */
enum {PS1_DEVICE_AUTO,PS1_DEVICE_OFF,PS1_DEVICE_PAD1,PS1_DEVICE_HANDHELD=10,PS1_DEVICE_COUNT};
/* Keep the legacy preferences prefix intact for version-1 journal migration. */
typedef struct {
 uint32_t value[PS1_OPTION_COUNT];char recent[PS1_SD_PATH_MAX];
 uint32_t player2[PS1_CONTROL_COUNT],controller_device[PS1_PLAYER_COUNT];
 uint32_t extra_player[2][PS1_CONTROL_COUNT],multitap;
} Ps1Preferences;
typedef struct {
 const char *label,*help,*values[8];
 uint8_t category,count,initial,next_launch;
} Ps1Option;
extern const Ps1Option ps1_options[PS1_OPTION_COUNT];
extern const char *const ps1_categories[PS1_CATEGORIES];
extern Ps1Preferences *ps1_active_settings;
typedef struct {Ps1Preferences prefs;uint64_t generation;unsigned active,dirty;uint32_t warning;} Ps1Settings;
void ps1_preferences_default(Ps1Preferences*);
int ps1_preferences_valid(const Ps1Preferences*);
int ps1_setting_change(Ps1Settings*,unsigned option,int direction);
void ps1_settings_reset_category(Ps1Settings*,unsigned category);
uint32_t ps1_control_value(const Ps1Preferences*,unsigned player,unsigned option);
int ps1_control_change(Ps1Settings*,unsigned player,unsigned option,int direction);
void ps1_controls_reset(Ps1Settings*,unsigned player);
const char*ps1_controller_device_name(unsigned device);
int ps1_controller_device_change(Ps1Settings*,unsigned player,int direction);
const char*ps1_multitap_name(unsigned mode);
int ps1_multitap_change(Ps1Settings*,int direction);
uint32_t ps1_settings_load(Ps1Settings*,Ps1SdDirectory*);
uint32_t ps1_settings_save(Ps1Settings*,Ps1SdDirectory*);
const char *ps1_core_setting(const char *key);
unsigned ps1_display_limit(const Ps1Preferences*);
uint32_t ps1_input_settings(const Ps1Preferences*,uint32_t buttons,const int32_t axes[2],uint64_t tick,uint64_t hz);
uint32_t ps1_input_player_settings(const Ps1Preferences*,unsigned player,uint32_t buttons,const int32_t axes[2],uint64_t tick,uint64_t hz);
typedef struct {uint64_t next,remainder;uint32_t rate,started;} Ps1FrameClock;
int ps1_frame_clock_due(Ps1FrameClock*,uint64_t now,uint64_t hz,uint32_t rate);
#endif
