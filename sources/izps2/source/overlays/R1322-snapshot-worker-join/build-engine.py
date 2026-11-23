"""Build R1321 Switch 2 GS close acknowledgement and NFS podium diagnostics."""
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
base = out.parent/'R1321-gs-exit-decision'
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


def archive_members(path):
    raw = memoryview(path.read_bytes())
    assert raw[:8] == b'!<arch>\n'
    offset = 8
    entries = []
    names = b''
    while offset < len(raw):
        header = raw[offset:offset+60].tobytes()
        assert header[58:60] == b'`\n'
        name = header[:16].rstrip()
        size = int(header[48:58])
        body = raw[offset+60:offset+60+size]
        offset += 60 + size + (size & 1)
        if name == b'//':
            names = body.tobytes()
        elif name not in (b'/', b'/SYM64/'):
            if name.startswith(b'/'):
                start = int(name[1:])
                name = names[start:names.index(b'/\n',start)]
            else:
                name = name.removesuffix(b'/')
            entries.append((name.decode(),sha256(body).hexdigest()))
    assert offset == len(raw)
    return entries


def build(work):
    import shlex

    reuse = '--reuse-compiled' in sys.argv
    saved_recipes = load(out/'compile-recipes.json') if reuse else None
    recipes = load(base/'compile-recipes.json')
    for existing in recipes.values():
        existing['inheritedObject'] = True
    link = load(base/'link-command.json')
    name = 'ps2_vm_runner.cpp'
    recipe = recipes[name]
    argv = [arg.replace('-DIZPS2_BUILD_REVISION="R1321"','-DIZPS2_BUILD_REVISION="R1322"')
            for arg in recipe['argv']]
    assert '-DIZ_JIT_BUILD_ID="run25-hosted-r1321-r1175-gs-exit-decision"' in argv
    old_parent = Path(argv[argv.index('-c')+1]).parent
    argv[1:1] = ['-iquote',str(old_parent)]
    argv[argv.index('-c')+1] = str(out/name)
    argv[argv.index('-o')+1] = str(out/(name+'.obj'))
    for flag, extension in (('-MF','.d'), ('-MT','.obj')):
        if flag in argv:
            argv[argv.index(flag)+1] = str(out/(name+extension))
    runner_recipe = {'argv':argv,'cwd':recipe['cwd']}
    if reuse:
        assert saved_recipes[name] == runner_recipe and (out/(name+'.obj')).is_file()
    else:
        run(argv,'compile-'+name+'.log',cwd=recipe['cwd'])
    recipes[name] = runner_recipe

    module = out.parents[3]
    native_cmake = module/'build/izps2-homebrew/logs/hen-izps2-shaderc-5pa3zJ/izps2-vulkan'
    all_commands = load(native_cmake/'compile_commands.json')
    objcopy = run23/'engines/izhen/sdk/devkitpro/devkitA64/bin/aarch64-none-elf-objcopy'
    restore = out.parent/'R1149-control-r1110-in-iza/restore-vulkan-symbols.txt'
    core_index, = (i for i,arg in enumerate(link['argv']) if Path(arg).name == 'libpcsx2-hosted.a')
    inherited_core = Path(link['argv'][core_index])
    sources = (
        ('GS/Renderers/Common/GSRenderer.cpp', '51b223138963d98c784016c9a58c3bb4d1f6eb608d30807d0b23d4d93eaa70b3',
         'bc309b2374ae7439d5ae2c9ea7b1dc32d6cac07b1b53997ce00318fff9ee938e'),
        ('GS/DriverReport/GSDriverReport.cpp', 'f591c5f3504234315580f98f8ebcef7336203c8fff1290df0744b576a043bbda',
         'c64f22de4d89e4a13250b57963286fd8823093af4eccc7cc934ccf24df828f94'),
    )
    changed = {}
    for relative, expected_source, expected_member in sources:
        name = Path(relative).name
        rec, = [rec for rec in all_commands if rec['file'].endswith('/'+relative)]
        archived_cmake_object = native_cmake/'pcsx2/CMakeFiles/PCSX2.dir'/(relative+'.obj')
        reconstructed = work/(name+'.base.obj')
        run([objcopy,'--redefine-syms='+str(restore),archived_cmake_object,reconstructed],'baseline-'+name+'.log')
        member = out/(name+'.obj')
        original = sp.check_output(['llvm-ar','p',inherited_core,member.name])
        assert sha256(original).hexdigest() == expected_member and original == reconstructed.read_bytes()
        assert sha256(Path(rec['file']).read_bytes()).hexdigest() == expected_source
        argv = shlex.split(rec['command'])
        raw = work/(name+'.raw.obj')
        argv[argv.index('-c')+1] = str(out/name)
        argv[argv.index('-o')+1] = str(raw)
        recipe = {'argv':argv,'cwd':rec['directory'],'objcopy':str(objcopy),'restoreSymbols':str(restore)}
        if reuse:
            saved = saved_recipes[name]
            saved_argv = saved['argv'].copy()
            saved_argv[saved_argv.index('-o')+1] = str(raw)
            assert saved|{'argv':saved_argv} == recipe and member.is_file()
            recipes[name] = saved
        else:
            run(argv,'compile-'+name+'.log',cwd=rec['directory'])
            run([objcopy,'--redefine-syms='+str(restore),raw,member],'rebase-'+name+'.log')
            recipes[name] = recipe
        changed[member.name] = {'before':expected_member,'after':sha256(member.read_bytes()).hexdigest()}
    save(out/'compile-recipes.json',recipes)

    new_core = out/inherited_core.name
    if not reuse:
        shutil.copyfile(inherited_core,new_core)
        run(['llvm-ar','rD',new_core,*(out/name for name in changed)],'archive.log')
    ordered = sp.check_output(['llvm-ar','t',inherited_core],text=True).splitlines()
    assert ordered == sp.check_output(['llvm-ar','t',new_core],text=True).splitlines()
    entries = archive_members(inherited_core)
    rebuilt = archive_members(new_core)
    assert [name for name,_ in entries] == [name for name,_ in rebuilt] == ordered
    diff = [(before[0],before[1],after[1]) for before,after in zip(entries,rebuilt) if before != after]
    assert len(diff) == len(changed) and all(changed[name] == {'before':before,'after':after} for name,before,after in diff), diff
    reconstructed = work/'GSRenderer.cpp.base.obj'
    renderer = out/'GSRenderer.cpp.obj'
    before_symbols = set(sp.check_output(['llvm-nm','-g','--defined-only','-j',reconstructed],text=True).splitlines())
    after_symbols = set(sp.check_output(['llvm-nm','-g','--defined-only','-j',renderer],text=True).splitlines())
    old_detach = sp.check_output(['llvm-nm','-u',reconstructed],text=True)
    new_detach = sp.check_output(['llvm-nm','-u',renderer],text=True)
    assert '_ZNSt6thread6detachEv' in old_detach and '_ZNSt6thread6detachEv' not in new_detach
    assert '_ZNSt6thread4joinEv' in new_detach
    assert all(symbol in after_symbols for symbol in before_symbols if symbol.startswith(('_ZN10GSRenderer','_Z21GSJoinSnapshotThreads')))
    driver = sp.check_output(['llvm-nm','-u',out/'GSDriverReport.cpp.obj'],text=True)
    assert 'pthread_attr_setdetachstate' not in driver and 'pthread_create' not in driver
    save(out/'archive-audit.json', {'members':changed, 'otherMembersUnchanged':True,
         'checkedMembers':len(ordered), 'detachReferenceRemoved':True, 'joinReferencePreserved':True,
         'driverHelperThreadRemoved':True, 'nativeBaseReconstructedExactly':True, 'guestJitBuildFingerprintPreserved':True})
    link['argv'][core_index] = str(new_core)
    mapped = {part:out/part for part in ('ps2_vm_runner.cpp.obj',)} | {'engine.elf':work/'engine.elf','engine.map':work/'engine.map'}
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
                  linkedBytesPreserved=False,addressIndependent=True,originalEngineObjectRevision='R1321',
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
         run25/'language/fonts',helpers/'input.tile',out/'output.tile','IZPS2 R1322'],'tile.log')
    frontend = out/'frontend'
    frontend.mkdir(exist_ok=True)
    shutil.copyfile(base/'frontend/frontend.bin',frontend/'frontend.bin')
    package_source = (base/'package-app.cjs').read_text().replace('R1321','R1322')
    (out/'package-app.cjs').write_text(package_source)
    verifier = (base/'verify-app.cjs').read_text()
    (out/'verify-app.cjs').write_text(verifier)
    env = os.environ | {'IZPS2_BUILD_SCRATCH':str(work)}
    run(['node',out/'package-app.cjs'],'package.log',env=env)
    run(['node',out/'verify-app.cjs'],'verify-app.log',env=env)
    package = out/'IZPS2-R1322.iza'
    (out/'SHA256SUMS').write_text(f'{sha256(package.read_bytes()).hexdigest()}  {package.name}\n')


if __name__=='__main__':
    with TemporaryDirectory(dir='/tmp',prefix='izps2-r1322-build-') as tmp:
        build(Path(tmp))
