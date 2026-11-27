#!/usr/bin/env python3
"""Build IZ2S RO and the serial NRO loader with an explicit embedded menu."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--menu',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();src=Path(__file__).resolve().parent;sdk=a.sdk.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
lib=sdk/'libounce';prefix=sdk/'devkitA64/bin/aarch64-none-elf-';env=dict(os.environ,DEVKITPRO=str(sdk));calls=[]
def run(cmd):
 cmd=list(map(str,cmd));calls.append(cmd);subprocess.run(cmd,env=env,check=True,cwd=src)
def identity(p):return {'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
common=['-mcpu=cortex-a78c+crypto','-mtp=soft','-fPIE','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-ffunction-sections','-fdata-sections','-D__SWITCH__','-D__SWITCH2__','-I'+str(src/'include'),'-I'+str(lib/'include'),'-I'+str(lib/'include/switch'),'-I'+str(src/'libkaihen/include')]
spec=out/'switch.specs';spec.write_text((lib/'switch.specs').read_text().replace('%:getenv(DEVKITPRO /libnx/switch.ld)',str(lib/'switch.ld')))
def build_object(path,folder,extra=()):
 obj=folder/(path.stem+'.o');cxx=path.suffix=='.cpp'
 args=common+(['-std=gnu++20','-fno-exceptions','-fno-rtti'] if cxx else ['-std=gnu11'])+list(extra)
 run([str(prefix)+('g++' if cxx else 'gcc'),*args,'-c',path,'-o',obj]);return obj
def link(objects,folder):
 elf=folder/'main.elf';main=folder/'main'
 run([str(prefix)+'g++','-mcpu=cortex-a78c+crypto','-mtp=soft','-ffixed-x18','-fPIE','-specs='+str(spec),'-Wl,--gc-sections,-Map,'+str(folder/'main.map'),*objects,lib/'lib/libounce_core.a','-o',elf])
 run([sdk/'tools/bin/elf2nso',elf,main]);return main
ro=out/'ro';ro.mkdir(exist_ok=True)
ro_objects=[build_object(path,ro) for path in sorted((src/'ro/source').glob('*.cpp'))]
ro_main=link(ro_objects,ro)
hbl=out/'loader';hbl.mkdir(exist_ok=True)
loader_objects=[build_object(src/'hbloader'/name,hbl,['-ffixed-x18']) for name in ['clean_main.cpp','config.c','cycle.c','leak_check.c','builtin_image.c']]
loader_objects.append(build_object(src/'dynlink.c',hbl,['-ffixed-x18']))
for path in [src/'hbloader/entry.S']:
 obj=hbl/(path.stem+'.o');run([str(prefix)+'gcc','-mcpu=cortex-a78c+crypto','-mtp=soft','-ffixed-x18','-fPIE','-c',path,'-o',obj]);loader_objects.append(obj)
# elf2nro reference may have NACP/icon appended; embed only the executable NRO.
raw=a.menu.read_bytes();size=int.from_bytes(raw[24:28],'little');assert raw[16:20]==b'NRO0' and 128<=size<=len(raw)
menu=hbl/'embedded-menu.nro';menu.write_bytes(raw[:size])
asm=hbl/'builtin_runtime.S';asm.write_text('.section .rodata.iz_runtime,"a",%progbits\n.balign 4096\n.global iz_runtime_start\n.global iz_runtime_end\niz_runtime_start:\n.incbin "'+str(menu)+'"\niz_runtime_end:\n')
obj=hbl/'builtin_runtime.o';run([str(prefix)+'gcc','-mcpu=cortex-a78c+crypto','-mtp=soft','-ffixed-x18','-fPIE','-c',asm,'-o',obj]);loader_objects.append(obj)
main=link(loader_objects,hbl)
(out/'build-record.json').write_text(json.dumps({'sdk':str(sdk),'sourceFiles':{p.relative_to(src).as_posix():identity(p) for p in src.rglob('*') if p.is_file() and '__pycache__' not in p.parts},'baseLibounceCore':identity(lib/'lib/libounce_core.a'),'menu':identity(menu),'ro':identity(ro_main),'main':identity(main),'commands':calls},indent=2)+'\n')
print('Built',main,ro_main)
