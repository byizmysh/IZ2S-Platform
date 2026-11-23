"""Build the R1311 combined GS shutdown, EE warm and SD trace changes using the recorded RUN25 recipes."""
from collections import Counter
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess as sp
import sys
from tempfile import TemporaryDirectory

out = Path(__file__).resolve().parent
base = out.parent/'R1310-nfs-ee-warm-boot'
helpers = out.parent/'R1292-gs-back-worker'
panel = out.parents[3].parents[2]
run23 = panel/'iz2s-platform-run23'
run25 = panel/'iz2s-platform-run25'


def load(path):
    return json.loads(path.read_text())


def save(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def run(args, log, *, cwd=out, env=None):
    with (out/log).open('wb') as f:
        sp.run(args,cwd=cwd,env=env,stdout=f,stderr=sp.STDOUT,check=True)


def build(work):
    recipes = load(base/'compile-recipes.json')
    for recipe in recipes.values():
        recipe['inheritedObject'] = True
    link = load(base/'link-command.json')
    for name in ('ps2_vm_runner.cpp', 'GSState.cpp', 'MTGS.cpp'):
        recipe = recipes[name]
        argv = [arg.replace('r1310-r1175-nfs-ee-warm-boot','r1311-r1175-gs-native-exit')
                .replace('-DIZPS2_BUILD_REVISION="R1310"','-DIZPS2_BUILD_REVISION="R1311"')
                for arg in recipe['argv']]
        source_index = argv.index('-c') + 1
        source_parent = Path(argv[source_index]).parent
        argv[source_index] = str(out/name)
        argv[1:1] = ['-iquote', str(source_parent)]
        if name == 'GSState.cpp':
            argv[1:1] = ['-iquote', str(out.parents[3]/'src'), '-I'+str(run23/'engines/izhen/sdk/sb4/include')]
        assert '-o' in argv
        argv[argv.index('-o')+1] = str(out/(name+'.obj'))
        for flag, extension in (('-MF','.d'), ('-MT','.obj')):
            if flag in argv:
                argv[argv.index(flag)+1] = str(out/(name+extension))
        run(argv,'compile-'+name+'.log',cwd=recipe['cwd'])
        recipes[name] = {'argv':argv,'cwd':recipe['cwd']}
    save(out/'compile-recipes.json',recipes)

    changed = ('ps2_vm_runner.cpp.obj', 'GSState.cpp.obj', 'MTGS.cpp.obj')
    mapped = {part:out/part for part in changed} | {'engine.elf':work/'engine.elf','engine.map':work/'engine.map'}
    assert Counter(Path(a).name for a in link['argv'] if Path(a).name in mapped)==Counter(mapped.keys())
    link['argv'] = [str(mapped.get(Path(a).name,a)) for a in link['argv']]
    save(out/'link-command.json',link)
    run(link['argv'],'link.log',cwd=link['cwd'])
    run(['zstd','-f','-q','-T1','-5','-o',out/'engine.map.zst','--',work/'engine.map'],'map-archive.log')
    run([sys.executable,helpers/'seal-image.py',work/'engine.elf'],'seal.log')

    packed = work/'engine-linked.izdata'
    engine = work/'engine.izdata'
    run([sys.executable,run23/'tools/sd-launcher/relocatable.py','--elf',work/'engine.elf',
         '--output',packed],'export.log')
    run([sys.executable,helpers/'portable-image.py',packed,engine],'portable.log')
    report = load(packed.with_suffix('.izdata.json'))
    data = engine.read_bytes()
    offset,count,stride = struct.unpack_from('<Q2I',data,72)
    report.update(bytes=len(data),sha256=sha256(data).hexdigest(),
                  fileBytes=struct.unpack_from('<2I',data,40),memoryBytes=struct.unpack_from('<2I',data,48),
                  records=count, recordKinds=Counter(struct.unpack_from('<H',data,offset+i*stride+4)[0] for i in range(count)),
                  linkedBytesPreserved=False,addressIndependent=True,originalEngineObjectRevision='R1310',
                  output=str(engine))
    save(out/'engine.izdata.json',report)
    reloc = run25/'core/console/sd-launcher/relocatable'
    checker = work/'verify-relocation'
    run(['cc','-O2','-I',reloc,helpers/'verify-relocation.c',reloc/'rebase.c','-lz','-o',checker],
        'verify-relocation-build.log')
    with (out/'relocation-verification.jsonl').open('wb') as log:
        for spread in (32,-32,128,-64):
            log.write(sp.check_output([checker,engine,str(spread)]))
    run([sys.executable,out.parent/'R1122-ee-loop-smc-epoch-charge/render-tile.py',
         run25/'language/fonts',helpers/'input.tile',out/'output.tile','IZPS2 R1311'],'tile.log')
    frontend = out/'frontend'
    frontend.mkdir(exist_ok=True)
    shutil.copyfile(base/'frontend/frontend.bin',frontend/'frontend.bin')
    package_source = (base/'package-app.cjs').read_text().replace('R1310','R1311')
    (out/'package-app.cjs').write_text(package_source)
    verifier = (base/'verify-app.cjs').read_text()
    (out/'verify-app.cjs').write_text(verifier)
    env = os.environ | {'IZPS2_BUILD_SCRATCH':str(work)}
    run(['node',out/'package-app.cjs'],'package.log',env=env)
    run(['node',out/'verify-app.cjs'],'verify-app.log',env=env)
    package = out/'IZPS2-R1311.iza'
    (out/'SHA256SUMS').write_text(f'{sha256(package.read_bytes()).hexdigest()}  {package.name}\n')


if __name__=='__main__':
    with TemporaryDirectory(dir='/tmp',prefix='izps2-r1311-build-') as tmp:
        build(Path(tmp))
