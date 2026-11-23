"""Cross-build the IZPS2 ABI-7 frontend using the pinned SDK recipe."""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess

release = Path(__file__).resolve().parent
root = release.parents[3]
out = release / 'frontend'
out.mkdir(exist_ok=True)
history = root/'build/izps2-homebrew/logs/iz2s-modules-migration-y7I2Bc/izps2-frontend-build.log'
commands = [shlex.split(line) for line in history.read_text().splitlines() if line.startswith(('/', 'ld.lld'))]
invocations = {}
for argv in commands:
    if argv[0].endswith('-gcc'):
        source = Path(argv[argv.index('-c')+1])
        invocations[source.name] = argv
template = invocations['ps2_sb4_app.c']
sources = (
    'ps2_sb4_app.c', 'ps2_sb4_audio.c', 'ps2_sb4_library.c', 'ps2_sb4_session.c',
    'ps2_sb4_video.c', 'native_frontend_memory.c', 'ps2_psx_ui.c',
    'ps2_psx_settings_menu.c', 'ps2_sb4_preferences.c',
)

def run(argv, log):
    with (out/log).open('wb') as stream:
        subprocess.run(list(map(str, argv)), cwd=root, check=True,
                       stdout=stream, stderr=subprocess.STDOUT)

def compile_unit(name):
    argv = invocations.get(name, template).copy()
    source = release/'frontend-source'/name
    if not source.is_file():
        source = Path(argv[argv.index('-c')+1])
    target = out/(name.removesuffix('.c')+'.o')
    argv[argv.index('-c')+1] = str(source)
    argv[1:1] = ['-I'+str(release/'frontend-source')]
    argv[argv.index('-o')+1] = str(target)
    for flag in ('-MF', '-MT'):
        if flag in argv:
            argv[argv.index(flag)+1] = str(target.with_suffix('.d'))
    run(argv, name+'.log')
    return target

with ThreadPoolExecutor(6) as executor:
    objects = list(executor.map(compile_unit, sources))

elf = out/'frontend.elf'
run(['ld.lld','-T',root/'tools/ps2_sb4_frontend.ld','--gc-sections','--no-undefined',
     '--build-id=none',*objects,'-o',elf], 'link.log')
run(['llvm-readelf','-r',elf], 'relocations.log')
assert 'There are no relocations' in (out/'relocations.log').read_text()
objcopy = next(argv[0] for argv in commands if argv[0].endswith('-objcopy'))
run([objcopy,'-O','binary','-j','.text',elf,out/'frontend.bin'], 'objcopy.log')
