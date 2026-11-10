#ifndef PS1_NATIVE_MENU_H
#define PS1_NATIVE_MENU_H
#include "fs-dir.h"
#include "settings.h"
#define PS1_MENU_WIDTH 640u
#define PS1_MENU_HEIGHT 480u
enum {PS1_MENU_A=1,PS1_MENU_B=2,PS1_MENU_UP=4,PS1_MENU_DOWN=8,
      PS1_MENU_LEFT=16,PS1_MENU_RIGHT=32,PS1_MENU_EXIT_BUTTON=64};
enum {PS1_MENU_RUNNING=0,PS1_MENU_SELECTED=1,PS1_MENU_CANCELLED=2,
      PS1_MENU_EXIT=3,PS1_MENU_ERROR=4};
enum {PS1_PAUSE_MENU_BUTTON=128}; /* Canonical emulator-menu action (Minus). */
enum {PS1_MENU_L=256,PS1_MENU_R=512,PS1_MENU_X=1024,PS1_MENU_Y=2048};
enum {PS1_MENU_ZL=4096,PS1_MENU_ZR=8192};
enum {PS1_PAUSE_RUNNING=0,PS1_PAUSE_CONTINUE=1,PS1_PAUSE_CHOOSE_GAME=2,
      PS1_PAUSE_RETURN_LAUNCHER=3,PS1_PAUSE_ERROR=4,PS1_PAUSE_SETTINGS=5,PS1_PAUSE_RESTART=6};
typedef struct {
 Ps1SdDirectory *sd;
 const char *root; /* SD-relative game directory, without /sd mount prefix */
 Ps1SdEntry *entries;
 size_t capacity;
 uint32_t *rgba; /* 640x480; packed RGBA little-endian (alpha high byte). */
 void *user;
 uint32_t (*buttons)(void *); /* Canonical PS1_MENU_* bits, no PS1 Start action. */
 int (*present)(void *, const uint32_t *, unsigned, unsigned);
 void (*sleep)(void *, uint64_t nanoseconds);
 const char *save_status; /* Set only after the caller's verified card flush. */
 const Ps1Preferences *preferences;
 const char *launch_source;
 const char *controller_status; /* Optional live P1/P2 status for native inputs. */
 uint32_t controller_mask,controller_error,single_joycon;
 const char*controller_style[PS1_PLAYER_COUNT];
} Ps1MenuConfig;
typedef struct {
 char directory[PS1_SD_PATH_MAX], selected[PS1_SD_PATH_MAX];
 size_t count,index,top;
 uint32_t previous,held_ticks,error,status,error_kind;
} Ps1Menu;
/* Config and workspaces remain owned by the caller. No allocation, host RPC,
 * file writes or background threads. init may report a filesystem error in
 * state.error; that error is shown with A retry / B back. */
int ps1_menu_init(Ps1Menu *,const Ps1MenuConfig *);
int ps1_menu_step(Ps1Menu *,const Ps1MenuConfig *,uint32_t buttons);
void ps1_menu_draw(const Ps1Menu *,const Ps1MenuConfig *);
void ps1_loading_draw(uint32_t *rgba,unsigned progress,const char *message);
void ps1_ui_rect(uint32_t*,unsigned,unsigned,unsigned,unsigned,uint32_t);
void ps1_ui_text(uint32_t*,unsigned,unsigned,const char*,unsigned,uint32_t,unsigned);
void ps1_ui_chrome(uint32_t*,const char*,const Ps1Preferences*);
void ps1_ui_logo(uint32_t*,unsigned,unsigned,unsigned);
void ps1_ui_github(uint32_t*,unsigned,unsigned,unsigned,uint32_t);
uint32_t ps1_ui_accent(const Ps1Preferences*);
void ps1_performance_draw(uint32_t*,unsigned,unsigned,unsigned,unsigned);
int ps1_menu_run(Ps1Menu *,const Ps1MenuConfig *);
/* Pause UI reuses only rgba/user/buttons/present/sleep from config. SD, root
 * and entries may be null. No core, filesystem, audio or lifecycle operations
 * occur here; the caller owns pause/resume and game-frame preservation. */
typedef struct {
 uint32_t selection,previous,held_ticks,input_armed,pending_continue,status;
} Ps1Pause;
int ps1_pause_init(Ps1Pause *,const Ps1MenuConfig *);
int ps1_pause_step(Ps1Pause *,const Ps1MenuConfig *,uint32_t buttons);
void ps1_pause_draw(const Ps1Pause *,const Ps1MenuConfig *);
/* Stack-owned state. Continue waits for release of A/B/Minus; an owner stop
 * remains active throughout that wait. A present error returns immediately. */
int ps1_pause_run(const Ps1MenuConfig *);
#endif
