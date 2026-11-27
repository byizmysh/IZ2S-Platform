#!/usr/bin/env python3
"""Build an opt-in dual-pane native launcher using the pinned RUN32 builder."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f'Split launcher build anchor changed: {before.strip()}')
    return source.replace(before, after, 1)


def split_catalog(source):
    source = replace_once(source,
        'static u32 present_at(IzContext*c,const u64*commands,u64 controls_address,u32 sdk_epoch){',
        '''#include <vulkan/vulkan.h>
static bool split_display_lost(u32 result);
static u32 present_at_mode(IzContext*c,const u64*commands,u64 controls_address,u32 sdk_epoch,
                           bool split_mode){''')
    source = replace_once(source,
        'Q(acquire,24,~0ull);',
        'Q(acquire,24,split_mode ? 16666667 : UINT64_MAX);')
    source = replace_once(source,
        'u32 rc=VK(c,F_ACQUIRE,c->device,acquire,&index,0,0,0,0,0);if(rc)return rc;',
        '''u32 rc=VK(c,F_ACQUIRE,c->device,acquire,&index,0,0,0,0,0);
 // SUBOPTIMAL still acquired an image and signalled the semaphore.
 if(rc&&rc!=VK_SUBOPTIMAL_KHR)return rc;
 ''')
    source = replace_once(source,
        '''rc=VK(c,F_PRESENT,c->queue,present_info,0,0,0,0,0,0);if(rc)return rc;return VK(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);''',
        '''rc=VK(c,F_PRESENT,c->queue,present_info,0,0,0,0,0,0);
 const bool display_lost=split_mode&&split_display_lost(rc);
 if(rc&&rc!=VK_SUBOPTIMAL_KHR&&!display_lost)return rc;
 // vkQueueSubmit already succeeded. Retire its work before returning a
 // surface error that lets Split release the submitted commands.
 u32 idle=VK(c,F_QUEUE_IDLE,c->queue,0,0,0,0,0,0,0);
 return idle ? idle : display_lost ? rc : 0;''')
    source = replace_once(source,
        'static u32 present(IzContext*c,const u64*commands){',
        '''static u32 present_at(IzContext*c,const u64*commands,u64 controls_address,u32 sdk_epoch){
 return present_at_mode(c,commands,controls_address,sdk_epoch,false);
}
static u32 present(IzContext*c,const u64*commands){''')
    source = replace_once(source,
        'static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,IzCheckpointSession*checkpoint){',
        '''static u32 launch_application(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,
                              IzCheckpointSession*checkpoint,bool split_mode){''')
    source = replace_once(source,
        'if(!rc)rc=record(c,&h,g);',
        'if(!rc&&!split_mode)rc=record(c,&h,g);')
    source = replace_once(source, ' iz2_menu_cache_retire();\n iz2_lifecycle_trace(fs->c,',
        ' if(!split_mode)iz2_menu_cache_retire();\n iz2_lifecycle_trace(fs->c,')
    source = replace_once(source,
        ' else if(!c->poisoned){\n  // INIT may have succeeded',
        ' else if(!c->poisoned&&!split_mode){\n  // INIT may have succeeded')
    source = replace_once(source,
        'static void sdk_input(IzContext*c',
        '''static u32 launch(IzFs*fs,Entry*entry,unsigned slot,Gpu*g,IzNative*n,
                  IzCheckpointSession*checkpoint){
 return launch_application(fs,entry,slot,g,n,checkpoint,false);
}
static void sdk_input(IzContext*c''')
    source = replace_once(source, 'u64 iz_run(IzContext*c){',
        '#include "split-mode.h"\nu64 iz_run(IzContext*c){')
    source = replace_once(source, ' MenuSelection remembered[IZ_MENU_PAGES]={0};',
        ''' MenuSelection remembered[IZ_MENU_PAGES]={0};
 IzSplitSelection split_selection = {0};
 bool split_enabled = false;
 u64 split_last_buttons = 0, split_consumed = 0;''')
    source = replace_once(source, 'u32 exit=input(c,&previous,&first_release);',
        '''u32 exit=input(c,&previous,&first_release);
  const u64 split_old_buttons = split_last_buttons;
  const u64 split_buttons = split_last_buttons = WG(controls,8);
  split_consumed &= split_buttons;''')
    source = replace_once(source,
        '   if(request){\n    if(c->host_mode&&request<=c->count',
        '''   const bool split_ready = (split_enabled || (split_buttons & SPLIT_TOGGLE) == SPLIT_TOGGLE) &&
       !c->host_mode && !c->ram_only && !page_changed &&
       !picker_open && !removing && idle_menu && !GET(controls,92) && !GET(controls,884) &&
       !c->host_launch && (!launching || (split_enabled && request));
   const IzSplitCommand command = split_menu_command(split_enabled, split_ready, split_buttons, split_old_buttons);
   if (command == SPLIT_COMMAND_TOGGLE || command == SPLIT_COMMAND_CANCEL) {
    split_consumed |= command == SPLIT_COMMAND_TOGGLE ? SPLIT_TOGGLE : SPLIT_PAD_B;
    split_enabled = command == SPLIT_COMMAND_TOGGLE && !split_enabled;
    split_selection.pending = false;
    iz_ounce_split_status(split_enabled ? "Split: choose first app" : "Split",
                          split_enabled ? "A selects a tile. B cancels." : "R + Up to select two apps");
    iz2_split_trace(split_enabled ? "split-mode-on" : "split-mode-off", "", 0, 0, 0);
    if (request) {request = 0; W(controls,1012,0); W(controls,964,0);}
   } else if (split_enabled && split_ready && (command == SPLIT_COMMAND_PICK || request)) {
     split_consumed |= SPLIT_PAD_A;
     const unsigned card = request ? request - 1 : GET(controls,80);
     request = 0; W(controls,1012,0); W(controls,964,0);
     Entry *app = card < c->count ? &apps[card] : NULL;
     Entry pair[2];
     switch (split_pick(&split_selection, app, pair)) {
     case SPLIT_PICK_LEFT: {
      char title[64];
      snprintf(title,sizeof title,"Split: %.38s selected",app->h.name);
      iz_ounce_split_status(title, "Choose a second app with A. B cancels.");
      iz2_split_trace("split-pick-first", app->h.id, SPLIT_LEFT, 0, 0);
      break;
     }
     case SPLIT_PICK_DUPLICATE:
      iz_ounce_split_status("Split: first app selected", "Choose a different app with A.");
      iz2_split_trace("split-pick-duplicate", app->h.id, SPLIT_LEFT, 0, 0);
      break;
     case SPLIT_PICK_UNSUPPORTED:
      if (app && app->host == 3)
       iz_ounce_split_status("Split: NRO uses full screen", "Choose a Split-compatible IZA with A.");
      else iz_ounce_split_status("Split: app unavailable", split_selection.pending ?
                            "Choose another second app with A." : "Choose another first app with A.");
      iz2_split_trace("split-pick-unsupported", app ? app->h.id : "", 0, SPLIT_UNAVAILABLE, 0);
      break;
     case SPLIT_PICK_PAIR:
      split_enabled = false;
      iz_ounce_split_status("Split", "R + Up to select two apps");
      iz2_split_trace("split-pick-second", app->h.id, SPLIT_RIGHT, 0, 0);
      split_neutral_input(controls);
      memcpy((void *)(uintptr_t)c->snapshot, controls, 1024);
      W((void *)(uintptr_t)c->snapshot, 92, 0);
      W((void *)(uintptr_t)c->snapshot, 960, 0);
      W((void *)(uintptr_t)c->snapshot, 964, 0);
      W((void *)(uintptr_t)c->snapshot, 1012, 0);
      W((void *)(uintptr_t)c->snapshot, 1016, 0);
      iz_pc_allow_auto_fetch(false);
      rc = run_split(c, fs, pair, &gpu, split_buttons);
      if (c->poisoned || stopping(c)) break;
      restore_menu(c, controls, rc);
      iz2_split_trace("split-menu-restored", "", 0, rc, 0);
      if (rc) iz_ounce_split_status("Split: could not open both apps", "R + Up to try again");
      c->last_app_error = rc;
      previous = first_release = 0;
      rc = 0;
      break;
     }
     if (c->poisoned || stopping(c)) break;
   }
   if (split_enabled && request) {request = 0; W(controls,1012,0); W(controls,964,0);}
   if (split_consumed) {
    Q(controls,8,WG(controls,8)&~split_consumed);
    W(controls,IZ_HOST_GESTURE_AT+28,GET(controls,IZ_HOST_GESTURE_AT+28)&~(u32)split_consumed);
   }
   if(request){
    split_selection.pending = false;
    if(c->host_mode&&request<=c->count''')
    return source


def split_host(source):
    revision = 'run34-0.34'
    source = replace_once(source, 'IZLAUNCHER_RUNTIME_ENTRY\\n', f'IZLAUNCHER_RUNTIME_ENTRY build={revision}\\n')
    source = replace_once(source, 'IZLAUNCHER_MENU_READY services=', f'IZLAUNCHER_MENU_READY build={revision} services=')
    source = replace_once(source, 'void iz_ounce_activity_record(const char *name,const char *action){',
        '''static IzActivity split_status = {.name="Split", .action="R + Up to select two apps"};

void iz_ounce_split_status(const char *title, const char *instructions){
    snprintf(split_status.name,sizeof split_status.name,"%.63s",title);
    snprintf(split_status.action,sizeof split_status.action,"%.63s",instructions);
    ++activity_revision;
    ++activity_text_revision;
}

void iz_ounce_activity_record(const char *name,const char *action){''')
    source = replace_once(source, '    if(!activity_count)return;',
        '''    const unsigned count=activity_count<ACTIVITY_LIMIT?activity_count+1:ACTIVITY_LIMIT;
    if(!activity_text_revision)++activity_text_revision;''')
    source = replace_once(source,
        '''        for(unsigned i=0;i<activity_count;i++){
            activity_label(i,0,0,496,activity[i].name,22);
            activity_label(i,0,28,496,activity[i].action,17);
            activity_label(i,580,3,208,activity[i].date,15);''',
        '''        for(unsigned i=0;i<count;i++){
            const IzActivity *row=i?&activity[i-1]:&split_status;
            activity_label(i,0,0,496,row->name,22);
            activity_label(i,0,28,496,row->action,17);
            activity_label(i,580,3,208,row->date,15);''')
    source = replace_once(source, 'header[1]=activity_count;', 'header[1]=count;')
    return replace_once(source,
        'for(unsigned row=0;row<activity_count;row++){\n            header[4+row*4]=UINT32_MAX;\n            header[6+row*4]=activity[row].progress;',
        'for(unsigned row=0;row<count;row++){\n            header[4+row*4]=UINT32_MAX;\n            header[6+row*4]=row?activity[row-1].progress:0;')


def compile_shaders(native):
    definitions = ['#include <stdint.h>']
    for stage in ('vert', 'frag'):
        source = native / f'split-composite.{stage}'
        binary = source.with_suffix(f'.{stage}.spv')
        subprocess.run(['glslangValidator', '-V', '-Os', '-o', binary, source], check=True)
        subprocess.run(['spirv-val', '--target-env', 'vulkan1.3', binary], check=True)
        words = list(struct.iter_unpack('<I', binary.read_bytes()))
        definitions.append(f'static const uint32_t split_composite_{stage}[] = {{')
        for i in range(0, len(words), 8):
            definitions.append('    ' + ', '.join(f'0x{word:08x}u' for (word,) in words[i:i+8]) + ',')
        definitions.append('};')
    (native / 'split-composite-shaders.h').write_text('\n'.join(definitions) + '\n')


def compile_split_font(base, native):
    from PIL import Image, ImageDraw, ImageFont

    source = base / 'language/fonts/DejaVuSans-Bold.ttf'
    supersample, width, height = 4, 12, 20
    font = ImageFont.truetype(str(source), 14 * supersample)
    atlas = bytearray()
    for code in range(32, 96):
        glyph = Image.new('L', (width * supersample, height * supersample))
        ImageDraw.Draw(glyph).text((width * supersample / 2, 15 * supersample),
                                   chr(code), fill=255, anchor='ms', font=font)
        atlas.extend(glyph.resize((width, height), Image.Resampling.LANCZOS).tobytes())
    words = list(struct.iter_unpack('<I', atlas))
    definition = ['static const uint32_t split_font_words[] = {']
    for i in range(0, len(words), 8):
        definition.append('    ' + ', '.join(f'0x{word:08x}u' for (word,) in words[i:i+8]) + ',')
    (native / 'split-font.h').write_text('\n'.join(definition + ['};']) + '\n')
    return hashlib.sha256(source.read_bytes()).hexdigest()


def build(base, record, output, menu_only=False, ouncevk=None, version_table=None):
    overlay = Path(__file__).resolve().parent.parent
    output.mkdir(parents=True, exist_ok=False)
    status = output / 'status'
    shutil.copytree(overlay / 'status', status, ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(overlay / 'native', output / 'native')
    source_hashes = {
        file.relative_to(output).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
        for folder in (status, output / 'native') for file in folder.rglob('*') if file.is_file()
    }
    compile_shaders(output / 'native')
    font_hash = compile_split_font(base, output / 'native')
    code = (status / 'build-homebrew.py').read_text()
    staged_builder = status / 'build-homebrew-split.py'
    code = replace_once(code, "'-iquote', str(archived_catalog.parent), '-o',",
        "'-iquote', str(archived_catalog.parent), '-I', str(NATIVE_OVERLAY), '-o',")
    code = replace_once(code, "        elf = work / 'izlauncher.elf'",
        """        split_objects = []
        source_names = ['split-audio', 'split-audio-switch']
        for name in source_names:
            path = NATIVE_OVERLAY / f'{name}.c'
            obj = work / f'{name}.o'
            args = list(compile_step['argv'])
            args[-1] = str(path)
            args.extend(['-iquote', str(archived_catalog.parent), '-o', str(obj)])
            subprocess.run(args, cwd=work, env=env, check=True)
            split_objects.append(str(obj))

        elf = work / 'izlauncher.elf'""")
    code = replace_once(code, "        link_args[link_args.index('-o') + 1] = str(elf)",
        """        index = link_args.index(str(compiled_catalog))
        link_args[index + 1:index + 1] = split_objects
        link_args[link_args.index('-o') + 1] = str(elf)""")
    staged_builder.write_text(code)

    spec = importlib.util.spec_from_file_location('iz_run33_builder', staged_builder)
    builder = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    spec.loader.exec_module(builder)
    integrate = builder.integrate
    builder.integrate = lambda source: split_host(integrate(source))
    integrate_catalog = builder.integrate_catalog
    builder.integrate_catalog = lambda source: split_catalog(integrate_catalog(source))
    nro = output / 'embedded-menu-reference.nro'
    main = None if menu_only else output / 'main'
    builder.build(base, record, nro, main, ouncevk, version_table)
    report = {
        'sources': source_hashes,
        'generatedBuilder': {'path': staged_builder.relative_to(output).as_posix(),
                             'sha256': builder.digest(staged_builder)},
        'fontSha256': font_hash,
        'menuSha256': builder.digest(nro),
        'appVersionTableSha256': hashlib.sha256(builder.shipped_versions(base, version_table).encode()).hexdigest(),
    }
    if ouncevk is not None:
        report['ouncevk'] = {'path': str(ouncevk), 'sha256': builder.digest(ouncevk)}
    if version_table is not None:
        report['appVersions'] = {'path': str(version_table), 'sha256': builder.digest(version_table)}
    if main is not None:
        report['mainSha256'] = builder.digest(main)
    (output / 'build-inputs.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('record', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--menu-only', action='store_true', help='build the native menu reference')
    parser.add_argument('--ouncevk', type=Path, help='link a separately built OunceVK archive')
    parser.add_argument('--app-versions', type=Path, help='pin version recognition independently of bundled apps')
    args = parser.parse_args()
    build(args.base.resolve(), args.record.resolve(), args.output.resolve(), args.menu_only,
          args.ouncevk.resolve() if args.ouncevk else None,
          args.app_versions.resolve() if args.app_versions else None)
