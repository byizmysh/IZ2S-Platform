#ifndef IZ_PS2_APPLICATION_H
#define IZ_PS2_APPLICATION_H
#include "launch.h"
#define IZ_PS2_APPLICATION_CONFIG UINT32_C(0x41325350)
#define IZ_PS2_APPLICATION_UI UINT32_C(0x55325350)
typedef struct IzPs2Application IzPs2Application;
typedef struct {
 uint32_t magic,version,bytes,state_offset;
 uint32_t root_count,vertex_offset,vertex_bytes,fragment_offset,fragment_bytes,budget,reserved[6];
 char image[80],roots[3][128];
} IzPs2ApplicationConfig;
enum {IZ_PS2_APP_PREPARING=1,IZ_PS2_APP_CATALOG,IZ_PS2_APP_STARTING,
 IZ_PS2_APP_RUNNING,IZ_PS2_APP_CLOSING,IZ_PS2_APP_CLOSED,IZ_PS2_APP_RETAINED};
typedef struct {
 uint32_t version,bytes,state,phase,error,ui_error,game_count,cursor,stop_requested,reserved;
 uint64_t units,image_read_bytes,image_total_bytes,jit_rx;
 IzPs2LaunchProgress launch;
} IzPs2ApplicationProgress;
typedef struct {uint32_t rank,root;uint64_t bytes;char name[256];} IzPs2UiGame;
typedef struct {
 uint32_t magic,version,bytes,state,error,ui_error,count,cursor,first,rows,phase,reserved;
 uint64_t image_read_bytes,image_total_bytes;
 IzPs2UiGame games[12];uint8_t padding[768];
} IzPs2ApplicationUi;
_Static_assert(sizeof(IzPs2ApplicationUi)==4096,"Native PS2 picker/status payload");
/* The controller, four resource owners, SDK bindings, FS IPC scratch and the
 * launch span live in one retained public app.data lifetime. No private cast
 * of app.owner, PC callback or session-specific address is used. Config and
 * original shader bytes are distinct immutable slices of that same parent.
 * Begin performs no provider work. The native entry below drives bounded
 * serialized INIT/FRAME/STOP events; known pre-entry failures close actual
 * owners before returning their error. Unknown outcomes retain everything.
 * The exact original engine/profile remains the packager's obligation. */
size_t iz_ps2_application_bytes(void);
uint32_t iz_ps2_application_begin(void*,size_t,IzNativeApp6*,const IzPs2ApplicationConfig*);
uint32_t iz_ps2_application_step(IzPs2Application*,uint32_t,IzPs2ApplicationProgress*);
uint32_t iz_ps2_application_select(IzPs2Application*,uint32_t);
uint32_t iz_ps2_application_input(IzPs2Application*,uint64_t);
uint32_t iz_ps2_application_stop(IzPs2Application*);
uint32_t iz_ps2_application_ui(IzPs2Application*,IzPs2ApplicationUi*);
uint32_t iz_ps2_application_event(IzNativeApp*,uint32_t);
#endif
