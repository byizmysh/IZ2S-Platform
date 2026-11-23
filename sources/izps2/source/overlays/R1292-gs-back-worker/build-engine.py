"""Build the RUN25 GS split trial and retain its link map and .iza."""
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from hashlib import file_digest, sha256
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

out = Path(__file__).resolve().parent
base = out.parent/'R1288-spu2-certified-worker'
root = out.parents[3]
panel = root.parents[2]
run23 = panel/'iz2s-platform-run23'
run25 = panel/'iz2s-platform-run25'
cmake = root/'build/izps2-homebrew/logs/hen-izps2-shaderc-5pa3zJ/izps2-vulkan/compile_commands.json'
graphics = ('GSState.cpp','GS.cpp')
names = ('hosted-entry.c','ps2_vm_runner.cpp',*graphics)
dependencies = (
    'hosted-entry.c','spu2-mix-timing.h','spu2-certified-mixer.h','ps2_frame_windows.hpp','ps2_frame_windows.inc',
    'checkpoint-extended-seed.h','checkpoint-burst-seed.h','native_ee_warm_profile.hpp','native_ee_warm_retain.hpp',
    'iop-slice-breakdown.h','embedded-spirv.h','mtgs-command-trace.h','mtgs-submit-trace.h','engine.ld',
    'ee-span-trace.hpp','ee-affinity-native.inc','ee-affinity-trial.hpp','portable-image.py','verify-relocation.c',
    'input.tile','seal-image.py','checkpoint-late-suffix.h','checkpoint-late-suffix.bin','ps2_sb4_ee_warm.inc',
    'vif1-queue-publication.h','gs-field-profile.h','nfs-gs-live.h','nfs-gs-peaks.h',
    'ee-late-warm.hpp','ps2_mtvu_bulk_v4.hpp','gta-vu-cache-policy.h',
)


def load(path):
    return json.loads(path.read_text())


def write(path, value):
    path.write_text(json.dumps(value,indent=2)+'\n')


def digest(path):
    with Path(path).open('rb') as f:
        return file_digest(f,'sha256').hexdigest()


def run(args, logfile, *, cwd=out, env=None):
    with (out/logfile).open('wb') as log:
        subprocess.run(args,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)


def recipe_map():
    recipes = load(base/'compile-recipes.json')
    for name in graphics:
        row, = (entry for entry in load(cmake) if entry['file'].endswith('/pcsx2/GS/'+name))
        recipes[name] = {'argv':shlex.split(row['command']), 'cwd':row['directory']}
    return recipes


def compile_one(name, recipe):
    argv = [arg.replace(str(base),str(out))
            .replace('r1288-r1175-spu2-certified-worker','r1292-r1175-gs-back-worker')
            .replace('-DIZPS2_BUILD_REVISION="R1288"','-DIZPS2_BUILD_REVISION="R1292"')
            for arg in recipe['argv']]
    argv.insert(1,'-I'+str(out))
    obj = out/(name+'.obj')
    argv[argv.index('-o')+1] = str(obj)
    argv[argv.index('-c')+1] = str(out/name)
    for flag in ('-MF','-MT'):
        if flag in argv:
            argv[argv.index(flag)+1] = str(obj.with_suffix('.d'))
    run(argv,'compile-'+name+'.log',cwd=recipe['cwd'])
    return name, {'argv':argv,'cwd':recipe['cwd']}


def disassembly(obj):
    return subprocess.check_output(['llvm-objdump','-dr',obj]).splitlines()[3:]


def verify_originals(work, stock, recipes):
    rows = []
    for name,index in (('GSState.cpp',1),('GS.cpp',2)):
        member = name+'.obj'
        subprocess.run(['llvm-ar','xN',str(index),stock,member],cwd=work,check=True)
        archived = work/member
        recipe = recipes[name]
        reference = Path(recipe['cwd'])/recipe['argv'][recipe['argv'].index('-o')+1]
        assert disassembly(archived)==disassembly(reference), (archived,reference)
        rows.append({'member':member,'indexAmongSameNames':index,'archivedSha256':digest(archived),
                     'sourceRecipeReference':str(reference),'referenceRelocatedInstructionsMatch':True,
                     'replacementSha256':digest(out/member)})
    return rows


def rebase_and_package(work, engine, export_report):
    data = engine.read_bytes()
    offset,count,stride = struct.unpack_from('<Q2I',data,72)
    verify = load(export_report)
    verify.update(bytes=len(data),sha256=sha256(data).hexdigest(),
                  fileBytes=struct.unpack_from('<2I',data,40),memoryBytes=struct.unpack_from('<2I',data,48),
                  records=count,recordKinds=Counter(struct.unpack_from('<H',data,offset+i*stride+4)[0] for i in range(count)),
                  linkedBytesPreserved=False,addressIndependent=True,originalEngineObjectRevision='R1288')
    write(out/'engine.izdata.json',verify)
    reloc = run25/'core/console/sd-launcher/relocatable'
    checker = work/'verify-relocation'
    run(['cc','-O2','-I',reloc,out/'verify-relocation.c',reloc/'rebase.c','-lz','-o',checker],
        'verify-relocation-build.log')
    with (out/'relocation-verification.jsonl').open('wb') as report:
        for spread in (32,-32,128,-64):
            report.write(subprocess.check_output([checker,engine,str(spread)]))
    run([sys.executable,out.parent/'R1122-ee-loop-smc-epoch-charge/render-tile.py',run25/'language/fonts',
         out/'input.tile',out/'output.tile','IZPS2 R1292'],'tile.log')
    frontend = base/'frontend/frontend.bin'
    (out/'frontend').mkdir(exist_ok=True)
    shutil.copyfile(frontend,out/'frontend/frontend.bin')
    write(out/'frontend-reuse.json',{'inherited':str(frontend),'sha256':digest(frontend)})

    app_source = (base/'package-app.cjs').read_text().replace('R1288','R1292')
    app_source = app_source.replace("path.join(__dirname, 'engine.izdata')",
        "path.join(process.env.IZPS2_BUILD_SCRATCH ?? __dirname, 'engine.izdata')")
    app_source = app_source.replace("path.join(__dirname, 'prepare-')",
        "path.join(process.env.IZPS2_BUILD_SCRATCH ?? __dirname, 'prepare-')")
    (out/'package-app.cjs').write_text(app_source)
    verifier = (base/'verify-app.cjs').read_text().replace("path.join(__dirname, 'verify-')",
        "path.join(process.env.IZPS2_BUILD_SCRATCH ?? __dirname, 'verify-')")
    (out/'verify-app.cjs').write_text(verifier)
    env = os.environ | {'IZPS2_BUILD_SCRATCH':str(work)}
    run(['node',out/'package-app.cjs'],'package.log',env=env)
    run(['node',out/'verify-app.cjs'],'verify-app.log',env=env)
    app = out/'IZPS2-R1292.iza'
    (out/'SHA256SUMS').write_text(f'{digest(app)}  {app.name}\n')


def build(work):
    for name in dependencies:
        shutil.copyfile(base/name,out/name)
    recipes = recipe_map()
    with ThreadPoolExecutor(3) as pool:
        compiled = dict(pool.map(lambda name: compile_one(name,recipes[name]),names))
    compiled.update({name:{**recipe,'inheritedObject':True} for name,recipe in recipes.items() if name not in compiled})
    write(out/'compile-recipes.json',compiled)
    link=load(base/'link-command.json')
    stock, = (Path(arg) for arg in link['argv'] if Path(arg).name=='libpcsx2-hosted.a')
    provenance=verify_originals(work,stock,recipes)
    replace={'hosted-entry.c.obj':out/'hosted-entry.c.obj','ps2_vm_runner.cpp.obj':out/'ps2_vm_runner.cpp.obj',
             'engine.ld':out/'engine.ld','engine.elf':work/'engine.elf','engine.map':work/'engine.map'}
    argv=[str(replace.get(Path(arg).name,arg)) for arg in link['argv']]
    offset=argv.index(str(stock))
    argv[offset:offset]=[str(out/(name+'.obj')) for name in graphics]
    link['argv']=argv
    write(out/'link-command.json',link)
    run(argv,'link.log',cwd=link['cwd'])
    mapping=(work/'engine.map').read_text()
    assert str(stock)+'(GSState.cpp.obj)' not in mapping
    # The other GS.cpp member owns hardware registers and must remain linked.
    for name in graphics:
        assert str(out/(name+'.obj')) in mapping
    assert 'IZPS2' in subprocess.check_output(['strings',work/'engine.elf'],text=True)
    provider = next(row['path'] for row in load(base/'native-audit.json')['sdkArchives']
                    if row['path'].endswith('/libsb4-host-posix.a'))
    assert provider+'(host-posix.o)' in mapping and '.text.__wrap_pthread_create' in mapping and '.text.__wrap_pthread_join' in mapping
    write(out/'native-audit.json',{'inheritedArchive':str(stock),'unchangedArchiveSha256':digest(stock),'replacements':provenance,
         'sdkArchives':load(base/'native-audit.json')['sdkArchives'],'threadProvider':provider,
         'linkMapRawSha256':digest(work/'engine.map')})
    run(['zstd','-q','-T1','-5','-o',out/'engine.map.zst','--',work/'engine.map'],'map-archive.log')
    run([sys.executable,out/'seal-image.py',work/'engine.elf'],'seal.log')
    packed=work/'engine-linked.izdata'
    engine=work/'engine.izdata'
    run([sys.executable,run23/'tools/sd-launcher/relocatable.py','--elf',work/'engine.elf','--output',packed],'export.log')
    run([sys.executable,out/'portable-image.py',packed,engine],'portable.log')
    rebase_and_package(work,engine,packed.with_suffix('.izdata.json'))


if __name__=='__main__':
    with TemporaryDirectory(dir='/dev/shm',prefix='izps2-r1292-native-') as scratch:
        build(Path(scratch))
