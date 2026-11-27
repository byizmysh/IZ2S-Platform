#!/usr/bin/env python3
"""Build the embedded RUN32 menu and its NRO validation reference."""

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


NATIVE_MAIN = Path('engines/iz2s-core/source/izlauncher/ounce/main.c')
RECORDED_MAIN = Path('sources/runtime-build') / NATIVE_MAIN
STATUS_HEADER = Path(__file__).with_name('menu-status.h')
VERSION_HEADER = Path(__file__).with_name('menu-versions.h')
OFFICIAL_HEADER = Path('menu-official-versions.h')
NATIVE_OVERLAY = STATUS_HEADER.parent.parent / 'native'
CATALOG_SOURCE = Path('engines/iz2s-core/source/izlauncher/legacy/native/launcher.c')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def apply_hooks(source, hooks):
    for original, updated in hooks:
        if source.count(original) != 1:
            raise ValueError(f'RUN32 menu hook unavailable: {original.strip()}')
        source = source.replace(original, updated, 1)
    return source


def integrate(source):
    return apply_hooks(source, (
        ('#include <time.h>\n', '#include <time.h>\n#include "menu-status.h"\n#include "menu-nro-result.h"\n'),
        ('static bool next_program_ready;\n', '''static bool next_program_ready;
uint32_t iz2_nro_return_message(void) {
    static bool consumed;
    if (consumed) return 0;
    consumed = true;
    const uint32_t result = envGetLastLoadResult();
    if (result) fprintf(stderr,"IZLAUNCHER_NRO_RETURN result=0x%08x\\n",result);
    return iz2_nro_result_message(result);
}
'''),
        ('#include "menu-factory.h"\n', '#include "menu-factory.h"\n#include "menu-pipeline-cache.h"\n#include "menu-lifecycle.h"\n'),
        ('static u32 run_menu(bool import){\n',
         '''static bool read_menu_atlas(const char *path,void *output,size_t size){
    FILE *file=fopen(path,"rb");
    if(!file)return false;
    bool complete=fread(output,1,size,file)==size&&fgetc(file)==EOF&&!ferror(file);
    return fclose(file)==0&&complete;
}

extern u64 iz_run_with_menu_snapshot(IzContext *c);
static u32 run_menu(bool import){
    iz2_start_begin();
    startup_import_pending |= import;
'''),
        ('    result=(u32)iz_run(c);\n',
         '    result=(u32)iz_run_with_menu_snapshot(c);\n    iz2_lifecycle_trace(c,"menu-exit",result);\n'),
        ('    fprintf(stderr,"IZLAUNCHER_ADD_APP_RAIL version=run25-02\\n");\n'
         '    const Sb4Profile *profile=sb4GetProfile();\n'
         '    fprintf(stderr,"IZLAUNCHER_SB4_SDK version=%u backend=%s services=%u audio=%u originalImage=%u\\n",profile->version,profile->backend,profile->services,profile->audioFunctions,profile->originalSdkImageLoaded);\n',
         ''),
        ('    if(import)record_sd_import(iz_import_incoming(0));\n', '    iz2_start_mark("mount");\n'),
        ('    c->platform_apps=iz_ounce_apps_page;c->platform_select=select_program;\n',
         '    extern void iz_ounce_apps_invalidate(void);\n'
         '    iz_ounce_apps_invalidate();\n'
         '    c->platform_apps=iz_ounce_apps_page;c->platform_select=select_program;\n'),
        ('static int add_packages(IzLocalAdd *message) {\n',
         '''static bool startup_import_pending;

bool iz2_startup_import_if_idle(void) {
    if(!startup_import_pending||!iz2_first_frame_tick||
       armGetSystemTick()-iz2_first_frame_tick<armNsToTicks(UINT64_C(500000000))||
       store_busy||pending_local_import||!iz_pc_menu_idle())return false;
    startup_import_pending=false;
    iz2_start_mark("deferred_import_begin");
    const IzImportResult result=iz_import_incoming(0);
    record_sd_import(result);
    iz2_start_mark("deferred_import_end");
    return result==IZ_IMPORT_ADDED||result==IZ_IMPORT_UPDATED||result==IZ_IMPORT_PARTIAL;
}

static int add_packages(IzLocalAdd *message) {
    // An explicit import already covers the incoming folder.
    startup_import_pending=false;
'''),
        ('void *menu=NULL,*image=NULL,*atlas=NULL,*legacy_atlas=NULL;size_t image_bytes=0,atlas_bytes=0,legacy_bytes=0;',
         'void *menu=NULL,*image=NULL;size_t image_bytes=0;'),
        ('''    atlas=read_resource("romfs:/menu/type-atlas.bin",&atlas_bytes);
    legacy_atlas=read_resource("romfs:/menu/legacy-atlas.bin",&legacy_bytes);
    if(!image||image_bytes<512||!atlas||atlas_bytes!=IZ_FOCUS_FONT_BYTES||!legacy_atlas||legacy_bytes!=IZ_LEGACY_FONT_BYTES||
       (iz_menu_factory()&&(iz_crc(atlas,atlas_bytes)!=IZ_FOCUS_FONT_CRC||
                          iz_crc(legacy_atlas,legacy_bytes)!=IZ_LEGACY_FONT_CRC))){result=0xe85a;goto cleanup;}
''', '    if(!image||image_bytes<512){result=0xe85a;goto cleanup;}\n'),
        ('''    result=iz_ounce_platform_open(c);if(result)goto cleanup;
    fprintf(stderr,"IZLAUNCHER_PLATFORM_READY\\n");
    unsigned char *controls=(void*)(uintptr_t)c->controls;
    memcpy(controls,(unsigned char*)image+header->section[SEC_STATE].offset,header->section[SEC_STATE].bytes);
    memcpy(controls+1024,(unsigned char*)image+header->section[SEC_DATA].offset,header->section[SEC_DATA].bytes);
    memcpy(controls+1024,legacy_atlas,legacy_bytes);
    memcpy(controls+1024+65536,atlas,atlas_bytes);
    fprintf(stderr,"IZLAUNCHER_FONTS legacy=0x%08x focus=0x%08x\\n",iz_crc(controls+1024,legacy_bytes),iz_crc(controls+1024+65536,atlas_bytes));
    *(u32*)(controls+0x70001c)=0x495a4335;
    IzLocalAdd *add=(void*)(controls+IZ_LOCAL_ADD_AT);
''', '''    u8 *snapshot=(void*)(uintptr_t)c->snapshot;
    memcpy(snapshot,(const u8*)image+header->section[SEC_STATE].offset,header->section[SEC_STATE].bytes);
    memcpy(snapshot+IZ_STATE_BYTES,(const u8*)image+header->section[SEC_DATA].offset,header->section[SEC_DATA].bytes);
    u8 *legacy=snapshot+IZ_STATE_BYTES,*focus=legacy+65536;
    if(!read_menu_atlas("romfs:/menu/type-atlas.bin",focus,IZ_FOCUS_FONT_BYTES)||
       !read_menu_atlas("romfs:/menu/legacy-atlas.bin",legacy,IZ_LEGACY_FONT_BYTES)){
        result=0xe85a;goto cleanup;
    }
    const u32 focus_crc=iz_crc(focus,IZ_FOCUS_FONT_BYTES),legacy_crc=iz_crc(legacy,IZ_LEGACY_FONT_BYTES);
    if(iz_menu_factory()&&(focus_crc!=IZ_FOCUS_FONT_CRC||legacy_crc!=IZ_LEGACY_FONT_CRC)){
        result=0xe85a;goto cleanup;
    }
    iz2_start_mark("resources");
    result=iz_ounce_platform_open(c);if(result)goto cleanup;
    iz2_start_mark("graphics_device");
    fprintf(stderr,"IZLAUNCHER_PLATFORM_READY\\n");
    fprintf(stderr,"IZLAUNCHER_FONTS legacy=0x%08x focus=0x%08x\\n",legacy_crc,focus_crc);
    *(u32*)(snapshot+0x70001c)=0x495a4335;
    IzLocalAdd *add=(void*)(snapshot+IZ_LOCAL_ADD_AT);
'''),
        ('    free(legacy_atlas);free(atlas);free(image);free(c);\n', '    free(image);free(c);\n'),
        ('    result=iz_native_menu_build(c,header,image,&menu);if(result)goto cleanup;\n',
         '    iz2_cache_install(c,image,image_bytes);\n    result=iz_native_menu_build(c,header,image,&menu);\n'
         '    iz2_cache_unhook(c);if(result)goto cleanup;\n    free(image);image=NULL;\n'
         '    iz2_start_mark("menu_ready");\n'),
        ('        if(!close)close=iz_ounce_platform_close(c);\n', '        if(!close)iz2_cache_close();\n        if(!close)close=iz_ounce_platform_close(c);\n'),
        ('typedef struct {char version[16];uint8_t digest[32];} IzVersionReceipt;\n',
         'typedef struct {char version[16];uint8_t digest[32];} IzVersionReceipt;\n#include "menu-versions.h"\n'),
        ('    }while(menu_reload);\n', '    }while(menu_reload);\n    iz2_menu_status_close();\n'),
    ))


def integrate_catalog(source):
    return apply_hooks(source, (
        (' c->state=2;u64 previous=0,first_release=0,next_scan=0,offer_frame=0;',
         '''#ifdef IZ_OUNCE_PRESENTATION
 extern uint32_t iz2_nro_return_message(void);
 W((void*)(uintptr_t)c->controls,92,iz2_nro_return_message());
#endif
 c->state=2;u64 previous=0,first_release=0,next_scan=0,offer_frame=0;'''),
        ('u64 iz_run(IzContext*c){',
         '''#include "menu-idle.h"
static u64 run_launcher(IzContext*c,bool snapshot_prepared);
u64 iz_run(IzContext*c){return run_launcher(c,false);}
u64 iz_run_with_menu_snapshot(IzContext*c){return run_launcher(c,true);}
static u64 run_launcher(IzContext*c,bool snapshot_prepared){
 Iz2MenuIdle menu_idle={0};'''),
        (''' memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,c->controls_bytes);
 int sd_page=0,requested_page=0,picker_open=0;
''', ''' // Catalog publication reads the previous menu epoch from controls.
 // An ordinary startup has already staged the rest in cached memory.
 if(snapshot_prepared)memcpy((void*)(uintptr_t)c->controls,(void*)(uintptr_t)c->snapshot,IZ_STATE_BYTES);
 else memcpy((void*)(uintptr_t)c->snapshot,(void*)(uintptr_t)c->controls,c->controls_bytes);
 int sd_page=0,requested_page=0,picker_open=0;
'''),
        ('u32 exit=input(c,&previous,&first_release);\n',
         '''u32 exit=input(c,&previous,&first_release);
  int32_t menu_axes[4];
  memcpy(menu_axes, controls+16, sizeof menu_axes);
  const bool menu_input = iz2_menu_input_active(WG(controls,8), GET(controls,136), menu_axes);
  const u64 menu_tick = WG(controls,0x700d70);
  const bool menu_had_app = c->active != 0;
  const u32 menu_epoch = GET(controls,108);
  bool menu_redraw = false;
'''),
        ('static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,IzCheckpointSession*checkpoint){\n',
         'static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,IzCheckpointSession*checkpoint){\n#ifdef IZ_OUNCE_PRESENTATION\n iz2_menu_cache_retire();\n iz2_lifecycle_trace(fs->c,"launch-begin",0);\n#endif\n'),
        ('   u32 sdk_stopped=sdkm_stop(c);',
         '   iz2_lifecycle_trace(c,"stop-request",0);\n   u32 sdk_stopped=sdkm_stop(c);'),
        ('   rc=iz_native_unload(native);\n',
         '   iz2_lifecycle_trace(c,"native-stop-enter",0);\n   rc=iz_native_unload(native);\n   iz2_lifecycle_trace(c,"native-stop-return",rc);\n'),
        ('    restore_menu(c,controls,c->last_app_error);\n',
         '    iz2_lifecycle_trace(c,"restore-menu",c->last_app_error);\n    restore_menu(c,controls,c->last_app_error);\n'),
        ('c->transitions++;first_release=0;\n',
         'c->transitions++;first_release=0;\n    iz2_lifecycle_trace(c,"menu-restored",c->last_app_error);\n'),
        ('  if(!c->active&&held)__atomic_store_n(&c->host_hold_ready,hold,__ATOMIC_RELEASE);\n',
         '  if(!c->active&&held){iz2_menu_cache_retire();__atomic_store_n(&c->host_hold_ready,hold,__ATOMIC_RELEASE);}\n'),
        ('   if(!c->active&&request&&request!=released&&claim==request){\n',
         '   if(!c->active&&request&&request!=released&&claim==request){\n    iz2_menu_cache_retire();\n'),
        ('extern void iz_ounce_activity_publish(u8 *controls,u8 *snapshot);\n',
         'extern void iz_ounce_activity_publish(u8 *controls,u8 *snapshot);\n'
         'extern bool iz2_menu_status_publish(u8 *controls,u8 *snapshot,_Bool menu_owned);\n'
         'extern void iz2_lifecycle_trace(const IzContext *c,const char *phase,u32 result);\n'
         'extern void iz2_menu_cache_retire(void);\n'
         'extern void iz2_startup_catalog_ready(void);\nextern void iz2_startup_frame(u64 frames);\n'
         'extern bool iz2_startup_import_if_idle(void);\n'
         'extern void iz_ounce_apps_invalidate(void);\n'
         'extern bool iz_ounce_apps_refresh_pending(void);\n'
         'extern void iz2_menu_versions_clear(u8 *snapshot);\n'
         'extern void iz2_menu_version_write(u8 *snapshot,u32 slot,const IzAppHeader *header,u32 program);\n'),
        (' u32 rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,0);\n',
         ' u32 rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,0);\n#ifdef IZ_OUNCE_PRESENTATION\n iz2_startup_catalog_ready();\n#endif\n'),
        ('   if(store_changed||ftp_changed||pc_changed)\n',
         '   menu_redraw = store_changed||ftp_changed||pc_changed;\n'
         '   if(menu_redraw)\n'),
        ('   char remove_id[48];\n',
         '''   if(c->platform_apps&&!c->host_mode&&iz_ounce_apps_refresh_pending())
    __atomic_store_n(&c->rescan,1,__ATOMIC_RELEASE);
   char remove_id[48];
'''),
        ('}else if(current&&directories_valid&&!sd_page&&requested_page)reuse_directories=1;',
         '}else if(current&&directories_valid&&(c->platform_apps||(!sd_page&&requested_page)))reuse_directories=1;'),
        ('   if(!launching&&scan_allowed&&(rescan||refresh||reuse_directories)){\n',
         '''   if(!launching&&scan_allowed&&(rescan||refresh||reuse_directories)){
#ifdef IZ_OUNCE_PRESENTATION
    if(c->platform_apps&&!c->host_mode&&(rescan||refresh))iz_ounce_apps_invalidate();
#endif
'''),
        ('''  if(presentation_paused)CALL(c,F_SLEEP,16666667,0,0,0,0,0,0,0);
  else{rc=present(c,c->active?gpu.commands:c->menu_commands);if(rc){c->poisoned=1;break;}if(c->active)sdkm_frame(c);}c->frames++;
''',
         '''  // The shader decrements these notification timers on rendered frames.
  const u32 progress = 1024u + 0x120000u, toast = 1024u + 0x1f0000u;
  const u32 progress_phase = GET(controls,progress+8);
  const bool menu_animating =
      (GET(controls,progress)==0x495a5031u && GET(controls,progress+4)==1u &&
       progress_phase>=1 && progress_phase<=7) ||
      (GET(controls,toast)==0x495a4e31u && (GET(controls,toast+16) ||
       GET(controls,toast+4)!=GET(controls,toast+12)));
  const bool quiet_menu = !presentation_paused && !menu_had_app && !c->active && !c->host_mode &&
      !held && !picker_open && !GET(controls,0x700004) && !GET(controls,800) &&
      !GET(controls,884) && !GET(controls,940) && !GET(controls,944) &&
      !GET(controls,964) && !GET(controls,1012) && !menu_animating &&
      menu_epoch==GET(controls,108);
  const bool draw = iz2_menu_draw(&menu_idle, menu_tick, c->tick_hz, quiet_menu,
                                 menu_input, menu_redraw);
  if(presentation_paused||!draw)CALL(c,F_SLEEP,IZ2_MENU_IDLE_POLL_NS,0,0,0,0,0,0,0);
  else{rc=present(c,c->active?gpu.commands:c->menu_commands);if(rc){c->poisoned=1;break;}if(c->active)sdkm_frame(c);}
  if(draw)c->frames++;
#ifdef IZ_OUNCE_PRESENTATION
  if(draw&&!presentation_paused&&!c->active&&!held){
   iz2_startup_frame(c->frames);
   if(quiet_menu&&!menu_input&&iz2_startup_import_if_idle())
    __atomic_store_n(&c->rescan,1,__ATOMIC_RELEASE);
  }
#endif
'''),
        ('  if(!c->active&&!held&&!c->host_mode&&iz_ounce_menu_frame(\n',
         '  if(draw&&!c->active&&!held&&!c->host_mode&&iz_ounce_menu_frame(\n'),
        (' memset(snapshot+MENU_TILES_AT,0,MENU_TILES_BYTES);\n',
         ' memset(snapshot+MENU_TILES_AT,0,MENU_TILES_BYTES);\n#ifdef IZ_OUNCE_PRESENTATION\n'
         ' iz2_menu_versions_clear(snapshot);\n#endif\n'),
        ('   iz_ounce_apps_icon(programs[i].token,snapshot+MENU_TILES_AT+c->count*IZ_TILE_BYTES+65536,96*96*4);\n',
         '   iz_ounce_apps_icon(programs[i].token,snapshot+MENU_TILES_AT+c->count*IZ_TILE_BYTES+65536,96*96*4);\n'
         '   iz2_menu_version_write(snapshot,c->count,NULL,programs[i].token);\n'),
        ('   if((rc||!GET(tile,102400)||GET(tile,102400)>512||!GET(tile,102404)||GET(tile,102404)>128)&&!c->poisoned&&!(h.flags&IZ_APP_MANAGED)){\n',
         '   const bool fallback=(rc||!GET(tile,102400)||GET(tile,102400)>512||!GET(tile,102404)||GET(tile,102404)>128)&&!c->poisoned&&!(h.flags&IZ_APP_MANAGED);\n'
         '   if(fallback){\n'),
        ('  if(!rc){if(c->platform_title)c->platform_title(tile,h.name);',
         '  if(!rc){\n#ifdef IZ_OUNCE_PRESENTATION\n'
         '   iz2_menu_version_write(snapshot,c->count,&h,0);\n#endif\n'
         '   if(!fallback&&c->platform_title)c->platform_title(tile,h.name);'),
        ('  if(!c->active&&!held)iz_ounce_activity_publish(controls,(void*)(uintptr_t)c->snapshot);\n',
         '''  const u32 activity_revision = 1024u + 0xf0000u + 8u;
  const u32 previous_revision = GET(controls,activity_revision);
  menu_redraw |= iz2_menu_status_publish(controls,(void*)(uintptr_t)c->snapshot,
                                        !c->active&&!held&&!c->poisoned);
  if(!c->active&&!held)iz_ounce_activity_publish(controls,(void*)(uintptr_t)c->snapshot);
  menu_redraw |= previous_revision!=GET(controls,activity_revision);
'''),
    ))


def shipped_versions(base, version_table=None):
    if version_table is not None:
        entries = []
        for row in json.loads(version_table.read_text()):
            if not re.fullmatch(r'[a-f0-9]{64}', row['headerSha256']) or not re.fullmatch(r'[A-Za-z0-9._+-]{1,15}', row['version']):
                raise ValueError('Invalid pinned app version table')
            hash_bytes = ','.join(f'0x{x:02x}' for x in bytes.fromhex(row['headerSha256']))
            entries.append(f'    {{{{{hash_bytes}}},"{row["version"]}"}},\n')
        if not entries:
            raise ValueError('Empty pinned app version table')
        return 'static const struct { uint8_t header[32]; char version[16]; } iz2_shipped_versions[] = {\n' + ''.join(entries) + '};\n'
    profile = json.loads((base / 'distribution/profile.json').read_text())
    entries = []
    for name in profile['moduleDirectories']:
        directory = base / 'modules' / name
        module = json.loads((directory / 'module.json').read_text())
        image_path = module['switch'].get('sdImage')
        if not image_path:
            continue  # NRO versions come from NACP.
        image = directory / image_path
        with image.open('rb') as file:
            header = file.read(512)
        version = module.get('moduleVersion', module['version'])
        if header[:8] != b'IZSDAPP1' or len(header) != 512 or not re.fullmatch(r'[A-Za-z0-9._+-]{1,15}',version):
            raise ValueError(f'Unexpected shipped IZA metadata: {name}')
        hash_bytes = ','.join(f'0x{x:02x}' for x in hashlib.sha256(header).digest())
        entries.append(f'    {{{{{hash_bytes}}},"{version}"}},\n')
    return 'static const struct { uint8_t header[32]; char version[16]; } iz2_shipped_versions[] = {\n' + ''.join(entries) + '};\n'


def integrate_platform(source):
    font_probe_start = source.index('    if(!font_checked&&')
    font_probe_end = source.index('    if(!appletMainLoop())', font_probe_start)
    return apply_hooks(source, (
        ('static bool armed,font_checked;', 'static bool armed;'),
        (source[font_probe_start:font_probe_end], ''),
        ('c->controls_bytes=IZ_SHARED_BYTES;memset(mapped,0,IZ_SHARED_BYTES);\n',
         'c->controls_bytes=IZ_SHARED_BYTES;\n'
         '    // The staged menu initializes the entire mapping before the first submission.\n'),
        ('    if(armed&&!current->active&&(padGetButtonsDown(&primary)&HidNpadButton_Plus))current->stop=1;\n',
         ''),
        ('static u64 pad_sample(u32 id,u64 output,u64 selected){',
         'static IzServiceFunction pad_readers[SB4_PAD_STYLES];\n'
         'static u64 pad_sample(IzServiceFunction fn,u64 output,u64 selected){'),
        ('    IzServiceFunction fn=iz_service_target(iz_ounce_services(),id);\n', ''),
        ('''CALLBACK(pad_handheld){return pad_sample(4845408,a,b);}
CALLBACK(pad_full){return pad_sample(4845184,a,b);}
CALLBACK(pad_dual){return pad_sample(4847136,a,b);}
CALLBACK(pad_left){return pad_sample(4847360,a,b);}
CALLBACK(pad_right){return pad_sample(4847584,a,b);}
''',
         '''CALLBACK(pad_handheld){return pad_sample(pad_readers[SB4_PAD_HANDHELD],a,b);}
CALLBACK(pad_full){return pad_sample(pad_readers[SB4_PAD_FULL],a,b);}
CALLBACK(pad_dual){return pad_sample(pad_readers[SB4_PAD_DUAL],a,b);}
CALLBACK(pad_left){return pad_sample(pad_readers[SB4_PAD_LEFT],a,b);}
CALLBACK(pad_right){return pad_sample(pad_readers[SB4_PAD_RIGHT],a,b);}
'''),
        ('    c->functions[F_TOUCH]=PTR(touch);c->service_bridge=iz_ounce_services();\n',
         '''    c->functions[F_TOUCH]=PTR(touch);c->service_bridge=iz_ounce_services();
    /* This bridge has immutable callbacks. The callbacks still sample live HID. */
    const u32 pad_ids[SB4_PAD_STYLES]={
        [SB4_PAD_HANDHELD]=SB4_HID_READ_HANDHELD, [SB4_PAD_FULL]=SB4_HID_READ_FULL_KEY,
        [SB4_PAD_DUAL]=SB4_HID_READ_JOY_DUAL, [SB4_PAD_LEFT]=SB4_HID_READ_JOY_LEFT,
        [SB4_PAD_RIGHT]=SB4_HID_READ_JOY_RIGHT,
    };
    for(unsigned i=0;i<SB4_PAD_STYLES;i++)
        pad_readers[i]=iz_service_target(c->service_bridge,pad_ids[i]);
'''),
    ))


def compile_platform(link_step, work, env):
    original, = (Path(arg) for arg in link_step['argv'] if arg.endswith('/ounce-platform.o'))
    transcript = json.loads((original.parent.parent / 'commands.json').read_text())
    step, = (step for step in transcript if step['name'] == 'compile-platform')
    args = step['argv'][:]
    compiler = Path(args[0])
    for index, arg in enumerate(args):
        if arg.startswith('-I') and arg.endswith('/sdk/sb4/include') and not Path(arg[2:]).is_dir():
            args[index] = '-I' + str(compiler.parents[3] / 'sb4/include')

    source_index = args.index('-c') + 1
    output_index = args.index('-o') + 1
    dependency_index = args.index('-MF') + 1
    source = Path(args[source_index])
    rebuilt = work / 'stock-platform.o'
    args[output_index] = str(rebuilt)
    args[dependency_index] = str(rebuilt.with_suffix('.d'))
    subprocess.run(args, cwd=work, env=env, check=True)
    if digest(rebuilt) != digest(original):
        raise ValueError('Recorded platform source and flags do not reproduce the linked object')

    patched = work / 'platform.c'
    patched.write_text(integrate_platform(source.read_text()))
    output = work / 'ounce-platform.o'
    args[source_index] = str(patched)
    args[output_index] = str(output)
    args[dependency_index] = str(output.with_suffix('.d'))
    subprocess.run(args, cwd=work, env=env, check=True)
    return output


def integrate_pc_link(source):
    hooks = [
        ('static Thread worker;\n', 'static Thread worker;\nstatic UEvent channel_wake;\n'),
        ('        if(!begin_channel_job()){\n            svcSleepThread(100000000);\n',
         '        if(!begin_channel_job()){\n            waitSingle(waiterForUEvent(&channel_wake),UINT64_MAX);\n'),
        ('            atomic_store(&channel_open,true);\n',
         '            atomic_store(&channel_open,true);\n            ueventSignal(&channel_wake);\n'),
        ('    atomic_store(&ending,false);\n    rc=threadCreate(&worker',
         '    atomic_store(&ending,false);\n    ueventCreate(&channel_wake,true);\n    rc=threadCreate(&worker'),
        ('    started=true;\n    atomic_fetch_add(&channel_generation,1);\n    atomic_store(&channel_open,true);\n',
         '    started=true;\n    atomic_fetch_add(&channel_generation,1);\n    atomic_store(&channel_open,true);\n'
         '    ueventSignal(&channel_wake);\n'),
        ('        atomic_store(&ending,true);\n        atomic_store(&video_input_token,0);\n',
         '        atomic_store(&ending,true);\n        atomic_store(&video_input_token,0);\n'
         '        ueventSignal(&channel_wake);\n'
         '        if(audio_started)ueventSignal(&audio_wake);\n'
         '        if(video_started)ueventSignal(&video_wake);\n'),
    ]
    for stream in ('audio', 'video'):
        worker = f'{stream}_worker'
        phase = f'{stream}_phase'
        wake = f'{stream}_wake'
        hooks.extend((
            (f'static Thread {worker};\n',
             f'static Thread {worker};\nstatic UEvent {wake};\n'),
            (f'        if(atomic_load(&{phase})!=VIDEO_REQUEST){{svcSleepThread(1000000);continue;}}\n',
             f'        waitSingle(waiterForUEvent(&{wake}),UINT64_MAX);\n'
             f'        if(atomic_load(&ending)||atomic_load(&{phase})!=VIDEO_REQUEST)continue;\n'),
            (f'    atomic_store(&{phase},VIDEO_IDLE);\n    Result rc=threadCreate(&{worker}',
             f'    atomic_store(&{phase},VIDEO_IDLE);\n    ueventCreate(&{wake},true);\n'
             f'    Result rc=threadCreate(&{worker}'),
            (f'        atomic_store(&{phase},VIDEO_REQUEST);\n',
             f'        atomic_store(&{phase},VIDEO_REQUEST);\n        ueventSignal(&{wake});\n'),
        ))
    return apply_hooks(source, hooks)


def compile_pc_link(compile_step, link_step, work, env):
    linked, = (Path(arg) for arg in link_step['argv'] if arg.endswith('/ounce-pc-link.o'))
    args = compile_step['argv'][:-1]
    source = Path(compile_step['argv'][-1]).with_name('pc-link.c')
    output = work / linked.name
    subprocess.run([*args, str(source), '-o', str(output)], cwd=work, env=env, check=True)
    if digest(output) != digest(linked):
        raise ValueError('Recorded PC Link source and flags do not reproduce the linked object')
    patched = work / source.name
    patched.write_text(integrate_pc_link(source.read_text()))
    subprocess.run([*args, '-iquote', str(source.parent), str(patched), '-o', str(output)],
                   cwd=work, env=env, check=True)
    return output


def compile_ftp_transfers(compile_step, work, env):
    folder = NATIVE_OVERLAY / 'ftp'
    common = compile_step['argv'][:-1]
    definitions = ['-D_DEFAULT_SOURCE', '-DHAVE_POLL=1', '-DHAVE_SO_REUSEADDR=1',
                   '-DHAVE_TCP_NODELAY=1', '-DHAVE_LOCALTIME_R=1', '-DHAVE_GMTIME_R=1',
                   '-DHAVE_STRNCASECMP=1', '-DFTP_MAX_SESSIONS=2',
                   '-DFTPSRV_VERSION_STR="99253bd-iz2s-v25"', '-DFTP_SOCKET_HEADER="socket_unistd.h"',
                   '-DFTP_VFS_HEADER="vfs_sd.h"', '-DIZ_FTP_NOTIFY_CATALOG']
    objects = {}
    for filename, object_name in [('ftpsrv.c', '017-ftp-ftpsrv.o'), ('vfs_sd.c', '018-ftp-vfs_sd.o')]:
        args = [arg for arg in common if arg != '-Werror' or filename != 'ftpsrv.c']
        output = work / object_name
        subprocess.run([*args, *definitions, str(folder / filename), '-o', str(output)],
                       cwd=work, env=env, check=True)
        objects[object_name] = output
    return objects


def build_r_entry(elf, work, steps, env, output):
    package_step = steps['embedded-image']
    raw_nro = work / 'native-menu.nro'
    subprocess.run([package_step['argv'][0], elf, raw_nro], check=True)
    builtin = steps['loader-builtin_runtime']
    original = Path(builtin['cwd'])
    source_arg = next(arg for arg in builtin['argv'] if arg.endswith('builtin_runtime.S'))
    previous_source = (original / source_arg).read_text()
    previous_data = str(original / 'iz-runtime.bin')
    if previous_source.count(previous_data) != 1:
        raise ValueError('Embedded native menu input is missing from the RUN32 build record')
    assembly = work / 'builtin_runtime.S'
    assembly.write_text(previous_source.replace(previous_data, str(raw_nro), 1))
    args = list(builtin['argv'])
    args[args.index(source_arg)] = str(assembly)
    args[args.index('-MF') + 1] = str(work / 'builtin_runtime.d')
    args[args.index('-o') + 1] = str(work / 'builtin_runtime.o')
    subprocess.run(args, cwd=work, env=env, check=True)

    r_elf = work / 'main.elf'
    link_step = steps['runtime-link']
    args = list(link_step['argv'])
    for i, arg in enumerate(args):
        if arg.endswith('/builtin_runtime.o'):
            args[i] = str(work / 'builtin_runtime.o')
        elif ',-Map,' in arg:
            args[i] = arg.split(',-Map,', 1)[0] + ',-Map,' + str(work / 'r-entry.map')
    args[args.index('-o') + 1] = str(r_elf)
    subprocess.run(args, cwd=link_step['cwd'], env=env, check=True)

    nso = work / 'main'
    subprocess.run([steps['runtime-package']['argv'][0], r_elf, nso], check=True)
    result = nso.read_bytes()
    if result[:4] != b'NSO0':
        raise ValueError('R-entry build did not produce an NSO')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as file:
        file.write(result)


def build(base, record, output, r_entry, ouncevk=None, version_table=None):
    runtime = base / 'engines/iz2s-core/runtime'
    # Keep the validated native link baseline separate from current release metadata.
    manifest = json.loads(STATUS_HEADER.with_name('baseline-menu-build.json').read_text())
    expected_core = manifest['outputs']['engines/iz2s-core/runtime/package/game/main']
    if digest(record.parent / 'main') != expected_core:
        raise ValueError('Recorded application build is from a different RUN32 runtime')
    source = base / RECORDED_MAIN
    if digest(source) != manifest['inputs'][RECORDED_MAIN.as_posix()]:
        raise ValueError('Recorded native menu source does not match RUN32')

    steps = {step['name']: step for step in json.loads(record.read_text())}
    compile_step = steps['compile-ounce-main.o']
    link_step = steps['application-link']
    linked_ouncevk, = (Path(arg) for arg in link_step['argv']
                      if arg.endswith('/libouncevk.a'))
    if ouncevk is not None:
        with ouncevk.open('rb') as library:
            if library.read(8) != b'!<arch>\n':
                raise ValueError('OunceVK override must be a static archive')
    package_step = steps['embedded-image']
    compiler_args = list(compile_step['argv'])
    compiler = Path(compiler_args[0])
    env = dict(os.environ, DEVKITPRO=str(compiler.parents[2]), DEVKITA64=str(compiler.parents[1]))
    archived_main = Path(compiler_args[-1])
    if digest(archived_main) != digest(source):
        raise ValueError('Compiler transcript used a different menu source')
    apps_source = base / 'engines/iz2s-core/source/izlauncher/ounce/apps.c'
    if digest(apps_source) != manifest['inputs'][apps_source.relative_to(base).as_posix()]:
        raise ValueError('Base NRO catalog source does not match the recorded runtime')
    catalog_step = steps['compile-legacy-launcher.o']
    archived_catalog = Path(catalog_step['argv'][-1])
    if digest(archived_catalog) != manifest['inputs'][CATALOG_SOURCE.as_posix()]:
        raise ValueError('Compiler transcript used a different RUN32 catalog source')
    if output.exists():
        raise FileExistsError(output)
    if r_entry and r_entry.exists():
        raise FileExistsError(r_entry)
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix='iz2s-status-') as tmp:
        work = Path(tmp)
        patched = work / 'main.c'
        patched.write_text(integrate(source.read_text()))
        (work / OFFICIAL_HEADER).write_text(shipped_versions(base, version_table))
        compiled = work / 'ounce-main.o'
        compiler_args[-1] = str(patched)
        compiler_args.extend(['-iquote', str(archived_main.parent), '-I', str(STATUS_HEADER.parent),
                              '-I', str(work), '-o', str(compiled)])
        subprocess.run(compiler_args, cwd=compile_step['cwd'], env=env, check=True)
        patched_catalog = work / 'launcher.c'
        patched_catalog.write_text(integrate_catalog(archived_catalog.read_text()))
        compiled_catalog = work / 'legacy-launcher.o'
        args = list(catalog_step['argv'])
        args[-1] = str(patched_catalog)
        args.extend(['-DIZ_STORE_HOSTED', '-I', str(STATUS_HEADER.parent),
                     '-iquote', str(archived_catalog.parent), '-o', str(compiled_catalog)])
        subprocess.run(args,cwd=catalog_step['cwd'],env=env,check=True)

        extra_objects = {
            'ounce-platform.o': compile_platform(link_step, work, env),
            'ounce-pc-link.o': compile_pc_link(compile_step, link_step, work, env),
        }
        extra_objects.update(compile_ftp_transfers(compile_step, work, env))
        native_dir = archived_main.parent
        for name, linked_name in [('apps', 'ounce-apps.o'), ('font', '009-ounce-font.o')]:
            source_file = NATIVE_OVERLAY / (name + '.c')
            compiled_extra = work / (name + '.o')
            args = list(compile_step['argv'])
            args[-1] = str(source_file)
            args.extend(['-iquote', str(native_dir), '-o', str(compiled_extra)])
            subprocess.run(args, cwd=work, env=env, check=True)
            extra_objects[linked_name] = compiled_extra

        elf = work / 'izlauncher.elf'
        link_args = []
        for arg in link_step['argv']:
            if ouncevk is not None and arg == str(linked_ouncevk):
                arg = str(ouncevk)
            elif arg.endswith('/ounce-main.o'):
                arg = str(compiled)
            elif arg.endswith('/legacy-launcher.o'):
                arg = str(compiled_catalog)
            elif Path(arg).name in extra_objects:
                arg = str(extra_objects[Path(arg).name])
            elif ',-Map,' in arg:
                arg = arg.split(',-Map,', 1)[0] + ',-Map,' + str(work / 'izlauncher.map')
            link_args.append(arg)
        link_args[link_args.index('-o') + 1] = str(elf)
        subprocess.run(link_args, cwd=link_step['cwd'], env=env, check=True)

        elf2nro = Path(package_step['argv'][0])
        nacp = work / 'izlauncher.nacp'
        icon = work / 'icon.jpg'
        subprocess.run([elf2nro.with_name('nacptool'), '--create', 'IZ2S Launcher', 'IZMYSH', '0.34', nacp], check=True)
        subprocess.run(['magick', base / 'shells/izlauncher/app/assets/icon.png', '-background', '#10172b',
                        '-alpha', 'remove', '-resize', '256x256', '-quality', '90', icon], check=True)
        nro = work / 'IZ2S.nro'
        subprocess.run([elf2nro, elf, nro, f'--icon={icon}', f'--nacp={nacp}'], check=True)
        with nro.open('rb') as src, output.open('xb') as dest:
            for block in iter(lambda: src.read(1024 * 1024), b''):
                dest.write(block)
        if r_entry:
            build_r_entry(elf, work, steps, env, r_entry)


def write_patch(base, output):
    lines = []
    for path, integrate_source in ((NATIVE_MAIN,integrate),(CATALOG_SOURCE,integrate_catalog)):
        source = (base / path).read_text()
        lines.extend(difflib.unified_diff(source.splitlines(True),integrate_source(source).splitlines(True),
                                          'a/'+path.as_posix(),'b/'+path.as_posix()))
    for name, source in ((STATUS_HEADER.name,STATUS_HEADER.read_text()),
                         (VERSION_HEADER.name,VERSION_HEADER.read_text()),
                         ('menu-lifecycle.h',STATUS_HEADER.with_name('menu-lifecycle.h').read_text()),
                         ('menu-pipeline-cache.h',STATUS_HEADER.with_name('menu-pipeline-cache.h').read_text()),
                         (str(OFFICIAL_HEADER),shipped_versions(base))):
        lines.extend(difflib.unified_diff([],source.splitlines(True),'/dev/null',
                                          'b/'+NATIVE_MAIN.with_name(name).as_posix()))
    output.write_text(''.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run32', type=Path)
    parser.add_argument('recorded_commands', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--r-entry', type=Path)
    parser.add_argument('--ouncevk', type=Path, help='link a separately built OunceVK archive')
    parser.add_argument('--app-versions', type=Path, help='pin version recognition independently of bundled apps')
    args = parser.parse_args()
    build(args.run32.resolve(), args.recorded_commands.resolve(), args.output.resolve(),
          args.r_entry.resolve() if args.r_entry else None,
          args.ouncevk.resolve() if args.ouncevk else None,
          args.app_versions.resolve() if args.app_versions else None)
    write_patch(args.run32.resolve(), STATUS_HEADER.with_name('run32-system-status.patch'))
