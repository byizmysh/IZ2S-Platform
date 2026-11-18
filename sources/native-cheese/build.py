from pathlib import Path
import hashlib, json, os, shutil, struct, subprocess

work = Path(__file__).resolve().parent
root = work.parents[2]
platform = root/'iz2s-panel/iz2s-platform-run25'
module = platform/'modules/iz2s-cheese'
sdk = platform.parent/'iz2s-platform-run23/engines/izhen/sdk'
base = platform.parent/'iz2s-platform-run22/data/ps2-cold-work/IZ-R799/run25/jit-single-range/R1043-prelinked-menu'
rows = {r['name']:r for r in json.loads((base/'commands.json').read_text())}
output = work/'runtime'
output.mkdir(exist_ok=True)
os.environ['DEVKITPRO'] = str(sdk/'devkitpro')
cross = str(sdk/'devkitpro/devkitA64/bin/aarch64-none-elf-')
commands = []
def run(args, cwd=platform):
    args = list(map(str,args))
    commands.append(dict(argv=args,cwd=str(cwd)))
    subprocess.run(args, cwd=cwd, check=True)
sha = lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(work/'console-main') == '938c9bc54944aa8cdb0283ace613e19534b43361068f551604262fbe59e18021'
run(['make','-C',sdk/'sb4','lib/libsb4.a'])
flags = ['-std=gnu11','-Os','-Wall','-Wextra','-Werror','-mcpu=cortex-a78c+crypto','-mtp=soft',
         '-fPIE','-ffunction-sections','-fdata-sections','-D__SWITCH__','-D__SWITCH2__',
         '-DIZ_STORE_MODULE','-DIZ_STORE_VERSION="0.8.0"','-I'+str(sdk/'sb4/include'),
         '-isystem',sdk/'devkitpro/libounce/include']
objects = []
for name in ['main.c','store.c','ui.c','download.c','installed-artwork.c','iza/host.c']:
    obj = output/('store-'+Path(name).stem+'.o')
    run([cross+'gcc',*flags,'-c',module/'switch'/name,'-o',obj])
    objects.append(obj)
snapshot = base/'source/menu-support'
run([cross+'gcc',*flags,'-I'+str(snapshot/'legacy/native'),'-c',work/'store-loader.c',
     '-o',output/'store-loader.o'])
objects.append(output/'store-loader.o')
# Recompile the exact pinned launcher source with only tile/return fixes.
old = base.parent/'R949-ee-warm-publication-menu-bound'
recipes = {r['name']:r for r in json.loads((old/'commands.json').read_text())}
for name, src, obj in [('compile-legacy-launcher','legacy/native/launcher.c','legacy-launcher.o'),
                       ('compile-apps','ounce/apps.c','ounce-apps.o')]:
    args = recipes[name]['argv']
    flags = args[1:args.index('-MMD')]
    flags = [a.replace(str(old/'source/menu-support'),str(work/'source/izlauncher')) for a in flags]
    run([args[0],*flags,'-c',work/'source/izlauncher'/src,'-o',output/obj])
link = rows['application-link']
old_lib = next(Path(a) for a in link['argv'] if a.endswith('041-libsb4.a'))
lib = output/'libsb4.a'
shutil.copyfile(old_lib, lib)
run([cross+'ar','r',lib,sdk/'sb4/obj/compat.o'])
args = [a.replace(str(old_lib),str(lib)).replace(str(base/'izlauncher-sdk'),str(output/'izlauncher-sdk'))
        for a in link['argv']]
args = [str(output/Path(a).name) if a.endswith(('/legacy-launcher.o','/ounce-apps.o')) else a for a in args]
run([args[0],*objects,'-Wl,--wrap=iz_native_load_saved',*args[1:]],link['cwd'])
nm = subprocess.check_output([cross+'nm',output/'izlauncher-sdk.elf'],text=True)
for name in ['iz_store_iza_event','iz_ounce_store_compat','sb4GetApplicationIoCompat','__assert_func',
             '__wrap_iz_native_load_saved','sslConnectionDoHandshake','swkbdShow','iz_ounce_apps_icon','installed_artwork']:
    assert ' T '+name+'\n' in nm, name
run([sdk/'devkitpro/tools/bin/elf2nro',output/'izlauncher-sdk.elf',output/'embedded.nro'])
data = (output/'embedded.nro').read_bytes()
assert data[16:20] == b'NRO0' and b'sb4IZ2SCheeseEvent\0' in data
(output/'iz-runtime.bin').write_bytes(data[:struct.unpack_from('<I',data,24)[0]])
asm = base/'objects/loader/builtin_runtime.S'
(output/'builtin_runtime.S').write_text(asm.read_text().replace(str(base/'iz-runtime.bin'),str(output/'iz-runtime.bin')))
for name in ['loader-builtin_runtime','runtime-link']:
    row = rows[name]
    replacements = {str(asm):str(output/'builtin_runtime.S'),
                    str(asm.with_suffix('.o')):str(output/'builtin_runtime.o'),
                    str(asm.with_suffix('.d')):str(output/'builtin_runtime.d'),
                    str(base/'main.elf'):str(output/'main.elf')}
    for a in row['argv']:
        if '-Map,' in a: replacements[a] = a.replace(str(base),str(output))
    run([replacements.get(a,a) for a in row['argv']],row['cwd'])
run([sdk/'devkitpro/tools/bin/elf2nso',output/'main.elf',output/'main'])
report = dict(base='R1043 (R1038 family)',baseSha256=sha(work/'console-main'), immediateMenuRefresh=True, nroTileIcons=True,
              sha256=sha(output/'main'),nativeServices=1,storeCodeInRuntime=True,
              independentStoreModulePreserved=True,
              sources={str(module/'switch'/name):sha(module/'switch'/name)
                       for name in ['main.c','store.c','ui.c','download.c','installed-artwork.c','iza/host.c']})
(output/'build.json').write_text(json.dumps(report,indent=2)+'\n')
(output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print(json.dumps(report,indent=2))
