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
        ('#include <time.h>\n', '#include <time.h>\n#include "menu-status.h"\n'),
        ('#include "menu-factory.h"\n', '#include "menu-factory.h"\n#include "menu-pipeline-cache.h"\n#include "menu-lifecycle.h"\n'),
        ('static u32 run_menu(bool import){\n', 'static u32 run_menu(bool import){\n    iz2_start_begin();\n'),
        ('    result=(u32)iz_run(c);\n', '    result=(u32)iz_run(c);\n    iz2_lifecycle_trace(c,"menu-exit",result);\n'),
        ('    if(import)record_sd_import(iz_import_incoming(0));\n', '    if(import)record_sd_import(iz_import_incoming(0));\n    iz2_start_mark("mount_and_import");\n'),
        ('    result=iz_ounce_platform_open(c);if(result)goto cleanup;\n', '    iz2_start_mark("resources");\n    result=iz_ounce_platform_open(c);if(result)goto cleanup;\n    iz2_start_mark("graphics_device");\n'),
        ('    result=iz_native_menu_build(c,header,image,&menu);if(result)goto cleanup;\n',
         '    iz2_cache_install(c,image,image_bytes);\n    result=iz_native_menu_build(c,header,image,&menu);\n    iz2_cache_unhook(c);if(result)goto cleanup;\n    iz2_start_mark("menu_ready");\n'),
        ('        if(!close)close=iz_ounce_platform_close(c);\n', '        if(!close)iz2_cache_close();\n        if(!close)close=iz_ounce_platform_close(c);\n'),
        ('typedef struct {char version[16];uint8_t digest[32];} IzVersionReceipt;\n',
         'typedef struct {char version[16];uint8_t digest[32];} IzVersionReceipt;\n#include "menu-versions.h"\n'),
        ('    }while(menu_reload);\n', '    }while(menu_reload);\n    iz2_menu_status_close();\n'),
    ))


def integrate_catalog(source):
    return apply_hooks(source, (
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
         'extern void iz2_menu_status_publish(u8 *controls,u8 *snapshot,_Bool menu_owned);\n'
         'extern void iz2_lifecycle_trace(const IzContext *c,const char *phase,u32 result);\n'
         'extern void iz2_menu_cache_retire(void);\n'
         'extern void iz2_startup_catalog_ready(void);\nextern void iz2_startup_frame(u64 frames);\n'
         'extern void iz2_menu_versions_clear(u8 *snapshot);\n'
         'extern void iz2_menu_version_write(u8 *snapshot,u32 slot,const IzAppHeader *header,u32 program);\n'),
        (' u32 rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,0);\n',
         ' u32 rc=publish_catalog(fs,apps,sd_page,dirs,listing?0:dir_count,listing,0,0);\n#ifdef IZ_OUNCE_PRESENTATION\n iz2_startup_catalog_ready();\n#endif\n'),
        ('if(c->active)sdkm_frame(c);}c->frames++;\n',
         'if(c->active)sdkm_frame(c);}c->frames++;\n#ifdef IZ_OUNCE_PRESENTATION\n if(!c->active&&!held)iz2_startup_frame(c->frames);\n#endif\n'),
        (' memset(snapshot+MENU_TILES_AT,0,MENU_TILES_BYTES);\n',
         ' memset(snapshot+MENU_TILES_AT,0,MENU_TILES_BYTES);\n#ifdef IZ_OUNCE_PRESENTATION\n'
         ' iz2_menu_versions_clear(snapshot);\n#endif\n'),
        ('   iz_ounce_apps_icon(programs[i].token,snapshot+MENU_TILES_AT+c->count*IZ_TILE_BYTES+65536,96*96*4);\n',
         '   iz_ounce_apps_icon(programs[i].token,snapshot+MENU_TILES_AT+c->count*IZ_TILE_BYTES+65536,96*96*4);\n'
         '   iz2_menu_version_write(snapshot,c->count,NULL,programs[i].token);\n'),
        ('  if(!rc){if(c->platform_title)c->platform_title(tile,h.name);',
         '  if(!rc){\n#ifdef IZ_OUNCE_PRESENTATION\n'
         '   iz2_menu_version_write(snapshot,c->count,&h,0);\n#endif\n'
         '   if(c->platform_title)c->platform_title(tile,h.name);'),
        ('  if(!c->active&&!held)iz_ounce_activity_publish(controls,(void*)(uintptr_t)c->snapshot);\n',
         '  iz2_menu_status_publish(controls,(void*)(uintptr_t)c->snapshot,!c->active&&!held&&!c->poisoned);\n'
         '  if(!c->active&&!held)iz_ounce_activity_publish(controls,(void*)(uintptr_t)c->snapshot);\n'),
    ))


def shipped_versions(base):
    profile = json.loads((base / 'distribution/profile.json').read_text())
    entries = []
    for name in profile['moduleDirectories']:
        directory = base / 'modules' / name
        module = json.loads((directory / 'module.json').read_text())
        image = directory / module['switch']['sdImage']
        with image.open('rb') as file:
            header = file.read(512)
        version = module.get('moduleVersion', module['version'])
        if header[:8] != b'IZSDAPP1' or len(header) != 512 or not re.fullmatch(r'[A-Za-z0-9._+-]{1,15}',version):
            raise ValueError(f'Unexpected shipped IZA metadata: {name}')
        hash_bytes = ','.join(f'0x{x:02x}' for x in hashlib.sha256(header).digest())
        entries.append(f'    {{{{{hash_bytes}}},"{version}"}},\n')
    return 'static const struct { uint8_t header[32]; char version[16]; } iz2_shipped_versions[] = {\n' + ''.join(entries) + '};\n'


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
    patched.write_text(apply_hooks(source.read_text(), ((
        '    if(armed&&!current->active&&(padGetButtonsDown(&primary)&HidNpadButton_Plus))current->stop=1;\n',
        '',
    ),)))
    output = work / 'ounce-platform.o'
    args[source_index] = str(patched)
    args[output_index] = str(output)
    args[dependency_index] = str(output.with_suffix('.d'))
    subprocess.run(args, cwd=work, env=env, check=True)
    return output


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


def build(base, record, output, r_entry):
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
        (work / OFFICIAL_HEADER).write_text(shipped_versions(base))
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
        args.extend(['-DIZ_STORE_HOSTED', '-iquote', str(archived_catalog.parent), '-o', str(compiled_catalog)])
        subprocess.run(args,cwd=catalog_step['cwd'],env=env,check=True)

        extra_objects = {'ounce-platform.o': compile_platform(link_step, work, env)}
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
            if arg.endswith('/ounce-main.o'):
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
        subprocess.run([elf2nro.with_name('nacptool'), '--create', 'IZ2S Launcher', 'IZMYSH', '0.32', nacp], check=True)
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
    args = parser.parse_args()
    build(args.run32.resolve(), args.recorded_commands.resolve(), args.output.resolve(),
          args.r_entry.resolve() if args.r_entry else None)
    write_patch(args.run32.resolve(), STATUS_HEADER.with_name('run32-system-status.patch'))
