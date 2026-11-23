#!/usr/bin/env python3
"""Build the application-owned image; import only the declared SDK functions."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def build(args):
    if 'libounce 9382b1ec48ac' not in (args.devkitpro / 'ounce-devkit-version.txt').read_text().splitlines():
        raise ValueError('Native services v1 requires libounce 9382b1ec48ac')
    work = args.work
    work.mkdir(parents=True, exist_ok=True)
    names = json.loads((HERE / 'imports.json').read_text())
    if len(names) != len(set(names)) or any(not n.isidentifier() for n in names):
        raise ValueError('Invalid import manifest')
    table = '#include <sb4/compat.h>\n'
    table += f'Sb4CFunction store_imports[{len(names)}];\n'
    table += f'const unsigned store_import_count = {len(names)};\n'
    table += 'const char *const store_import_names[] = {' + ','.join(json.dumps(n) for n in names) + '};\n'
    (work / 'imports.c').write_text(table)
    asm = '.arch armv8-a\n'
    for i, name in enumerate(names):
        asm += (f'.section .text.{name},"ax",%progbits\n.balign 8\n.global {name}\n'
                f'.type {name},%function\n{name}:\n bti c\n ldr x16, 1f\n ldr x16, [x16]\n br x16\n'
                f'.balign 8\n1: .quad store_imports + {8*i}\n.size {name}, .-{name}\n')
    (work / 'imports.S').write_text(asm)
    flags = ['-std=gnu11', '-Os', '-Wall', '-Wextra', '-Werror', '-mcmodel=large',
             '-fno-pic', '-fno-pie', '-fno-builtin', '-fno-stack-protector', '-fno-jump-tables',
             '-ffunction-sections', '-fdata-sections', '-mbranch-protection=bti', '-mno-outline-atomics',
             '-D__SWITCH__', '-D__SWITCH2__', '-DIZ_STORE_MODULE', '-DSTBI_NO_THREAD_LOCALS', '-DIZ_STORE_VERSION="0.8.7"',
             '-I', args.include, '-isystem', args.devkitpro / 'libounce/include']
    sources = [HERE / 'engine.c', work / 'imports.c', work / 'imports.S',
               *(HERE.parent / name for name in ('main.c', 'store.c', 'ui.c', 'download.c', 'installed-artwork.c'))]
    objects = []
    for source in sources:
        obj = work / (source.name + '.o')
        run(args.cross + 'gcc', *flags, '-c', source, '-o', obj)
        objects.append(obj)
    elf = work / 'store-engine.elf'
    run(args.cross + 'ld', '--no-undefined', '--fatal-warnings', '--gc-sections', '--emit-relocs',
        '--no-relax', '-T', HERE / 'engine.ld', *objects, '-o', elf)
    spec = importlib.util.spec_from_file_location('relocatable', args.packer)
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    image, report = packer.pack(elf)
    if not report['passed'] or set(report['recordKinds']) != {1}:
        raise ValueError('Engine requires only absolute relocations')
    filename = 'asset-' + report['sha256'] + '.izdata'
    (HERE / filename).write_bytes(image)
    for stale in HERE.glob('asset-*.izdata'):
        if stale.name != filename:
            stale.unlink()
    (work / 'engine-report.json').write_text(json.dumps(report, indent=2) + '\n')
    (work / 'engine-asset.h').write_text(f'#define STORE_ENGINE_ASSET "{filename}"\n')
    manifest = HERE / 'app.json'
    app = json.loads(manifest.read_text())
    app['native']['capabilities'] = ['engine-image']
    app['native']['assets'] = [dict(file=filename, path=filename, bytes=len(image), sha256=report['sha256'])]
    manifest.write_text(json.dumps(app, indent=2) + '\n')
    descriptor = HERE.parent.parent / 'module.json'
    module = json.loads(descriptor.read_text())
    module['switch']['files'] = [f for f in module['switch']['files'] if not f.endswith('.izdata')]
    module['switch']['files'].append('switch/iza/' + filename)
    # Source archive must carry the bound companion; a loose sdImage cannot.
    module['switch'].pop('sdImage', None)
    module['switch']['files'] = [f for f in module['switch']['files'] if f != 'switch/sd/iz2s-cheese.iza']
    descriptor.write_text(json.dumps(module, indent=2) + '\n')
    print(filename)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--devkitpro', type=Path, required=True)
    parser.add_argument('--include', type=Path, required=True)
    parser.add_argument('--cross', required=True)
    parser.add_argument('--packer', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    build(parser.parse_args())
