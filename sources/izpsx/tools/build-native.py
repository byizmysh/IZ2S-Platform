"""Build the IZPSX emulator native image; vendor sources stay unchanged."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];VENDOR=ROOT/'vendor/pcsx_rearmed';OUT=ROOT/'build/native';TREE=OUT/'tree';OUT.mkdir(parents=True,exist_ok=True)
from local_build import CC, PREFIX, mirror_link
from product_brand import apply as apply_product_brand
p=argparse.ArgumentParser();p.add_argument('--code-base',type=lambda s:int(s,0),default=0x48000000);p.add_argument('--data-base',type=lambda s:int(s,0),default=0x4c000000);a=p.parse_args()
patches=[]
for src in VENDOR.rglob('*'):
 rel=src.relative_to(VENDOR)
 if '.git'in rel.parts or src.suffix in ['.o','.d','.so','.a']or not src.is_file():continue
 dest=TREE/rel;dest.parent.mkdir(parents=True,exist_ok=True)
 if str(rel)=='libpcsxcore/new_dynarec/new_dynarec.c':
  old=src.read_text();new=old.replace('#include <stdio.h>','#include <stdio.h>\n#include "platform.h"',1)
  new=new.replace('void new_dyna_clear_cache(void *start, void *end)\n{','void new_dyna_clear_cache(void *start, void *end)\n{\n  ps1_jit_publish(start, end);\n  return;')
  needle='#ifdef BASE_ADDR_DYNAMIC\n  #ifdef VITA'
  new=new.replace(needle,'#ifdef BASE_ADDR_DYNAMIC\n  #if defined(PS1_NATIVE)\n  ndrc = ps1_jit_rx(sizeof(*ndrc));\n  ndrc_write_ofs = ps1_jit_offset();\n  #elif defined(VITA)',1)
  pos=new.index('void new_dynarec_cleanup(void)');new=new[:pos]+new[pos:].replace(needle,'#ifdef BASE_ADDR_DYNAMIC\n  #if defined(PS1_NATIVE)\n  ps1_jit_close();\n  ndrc = NULL;\n  #elif defined(VITA)',1)
  assert new!=old and 'ndrc = ps1_jit_rx'in new and 'ps1_jit_close();'in new
  if dest.is_symlink():dest.unlink()
  if not dest.exists()or dest.read_text()!=new:dest.write_text(new)
  patches.append(dict(file=str(rel),purpose='Owned RX cache, separate RW shadow and explicit publication; original CPU/GPU/SPU emulation unchanged',originalSha256=hashlib.sha256(old.encode()).hexdigest(),generatedSha256=hashlib.sha256(new.encode()).hexdigest()))
 elif str(rel)=='libpcsxcore/sio.c':
  old=src.read_text();needle='void SaveMcd(char *mcd, char *data, uint32_t adr, int size) {'
  assert old.count(needle)==1
  new=old.replace(needle,'extern void ps1_card_mark(const void *);\n'+needle+'\n\tps1_card_mark(data);',1)
  if dest.is_symlink():dest.unlink()
  if not dest.exists() or dest.read_text()!=new:dest.write_text(new)
  patches.append(dict(file=str(rel),purpose='Notify frontend of PS1 card writes; console-local dual-generation persistence',originalSha256=hashlib.sha256(old.encode()).hexdigest(),generatedSha256=hashlib.sha256(new.encode()).hexdigest()))
 elif str(rel)=='include/revision.h':
  if not dest.exists():dest.write_bytes(src.read_bytes())
 elif str(rel) in ('frontend/libretro.c','frontend/main.c','frontend/libretro_core_options.h','frontend/libretro_core_options_intl.h'):
  branded=apply_product_brand(str(rel),src.read_text());assert branded is not None
  if dest.is_symlink():dest.unlink()
  if not dest.exists()or dest.read_text()!=branded:dest.write_text(branded)
  patches.append(dict(file=str(rel),purpose='IZPSX emulator display metadata and diagnostics; compatibility identifiers and license notices preserved',originalSha256=hashlib.sha256(src.read_bytes()).hexdigest(),generatedSha256=hashlib.sha256(branded.encode()).hexdigest()))
 else:mirror_link(dest,src)
names='abs atof atoi isalnum isxdigit memchr memcmp memcpy memmove memset qsort roundf snprintf sprintf sscanf strcasecmp strcasestr strcat strchr strcmp strcpy strerror strftime strlen strncasecmp strncmp strncpy strrchr strstr strtok strtol strtoul tolower toupper vsnprintf vsprintf'.split()
profile=json.loads((ROOT/'abi/sdk-profile.json').read_text())
by_name={i['name']:i for i in profile['imports']}
imports=[by_name[n] for n in names];assert all(len(i['bytes'])==64 for i in imports)
(OUT/'imports.json').write_text(json.dumps(imports,indent=2)+'\n')
(OUT/'imports.h').write_text('#define PS1_IMPORT_COUNT '+str(len(names))+'\n')
assembly=['.section .data.ps1imports,"aw"','.balign 16','.global ps1_imports','ps1_imports:',f'.zero {8*len(names)}','.section .text.ps1imports,"ax"','.balign 16']
for i,n in enumerate(names):assembly+=['.global '+n,'.type '+n+',%function',n+':','bti jc','ldr x16, 1f',f'ldr x16, [x16, #{8*i}]','br x16','.balign 8','1: .quad ps1_imports']
(OUT/'imports.S').write_text('\n'.join(assembly)+'\n')
flags='-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -mcmodel=large -mbranch-protection=bti -mno-outline-atomics -fwrapv -DPS1_NATIVE -DTC_WRITE_OFFSET -DNO_WRITE_EXEC -I'+str(ROOT/'src')
args=['make','-f','Makefile.libretro','-j4','platform=unix','ARCH=aarch64','DYNAREC=ari64','BASE_ADDR_DYNAMIC=1','NO_MMAP=1','NO_POSIX_MEMALIGN=1','STATIC_LINKING=1','HAVE_CHD=0','HAVE_PHYSICAL_CDROM=0','USE_ASYNC_CDROM=0','USE_ASYNC_GPU=0','USE_ASYNC_SPU=0','NDRC_THREAD=0','USE_LIBRETRO_VFS=1','GIT_VERSION=" 7b0e8f8"','CC='+str(CC),'CC_AS='+str(CC),'AR='+PREFIX+'ar','LD='+PREFIX+'ld','CFLAGS_LAST='+flags,'TARGET='+str(OUT/'pcsx.a')]
with(OUT/'build.log').open('w')as log:r=subprocess.run(args,cwd=TREE,stdout=log,stderr=subprocess.STDOUT)
if r.returncode:print((OUT/'build.log').read_text()[-5000:]);sys.exit(r.returncode)
objects=[]
for src in [ROOT/'src/entry.S',ROOT/'src/frontend.c',ROOT/'src/platform.c',ROOT/'src/ro-jit.c',ROOT/'src/fs-ipc.c',ROOT/'src/display.c',ROOT/'src/audio.c',*[ROOT/'src'/n for n in ['settings.c','settings-menu.c','memory-card.c','fs-dir.c','fs-file.c','native-menu.c','resident-launcher.c','resident.c','keepawake.c']],OUT/'imports.S']:
 obj=OUT/(src.stem+'.o');command=[str(CC),'-O2','-D_GNU_SOURCE','-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pie','-fno-pic','-mcmodel=large','-mbranch-protection=bti','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'src'),'-I'+str(VENDOR/'deps/libretro-common/include'),'-I'+str(VENDOR/'include'),'-I'+str(VENDOR/'libpcsxcore'),'-include',str(OUT/'imports.h'),'-c',str(src),'-o',str(obj)]
 subprocess.run(command,check=True);objects.append(obj)
script=OUT/'link.ld';script.write_text('''ENTRY(_start)
SECTIONS {
 . = CODE; .text : { *(.text.entry) *(.text .text.*) } . = ALIGN(16); .rodata : { *(.rodata .rodata.*) }
 . = DATA; .data : { QUAD(0); *(.data .data.*) *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) } . = ALIGN(16); __bss_start = .;
 .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } . = ALIGN(16); __bss_end = .; _end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}'''.replace('CODE',hex(a.code_base)).replace('DATA',hex(a.data_base)))
elf=OUT/'app.elf';r=subprocess.run([str(CC),'-nostdlib','-static','-no-pie','-Wl,--build-id=none,--gc-sections,-T,'+str(script),*[str(o)for o in objects],str(OUT/'pcsx.a'),'-lgcc','-o',str(elf)],capture_output=True,text=True);(OUT/'link.log').write_text(r.stdout+r.stderr)
if r.returncode:print((r.stdout+r.stderr)[-12000:]);sys.exit(r.returncode)
symbols={l.split()[2]:int(l.split()[0],16)for l in subprocess.check_output([PREFIX+'nm',str(elf)],text=True).splitlines()if len(l.split())==3};assert not subprocess.check_output([PREFIX+'nm','-u',str(elf)],text=True).strip()
for name,sections in [('code',['.text','.rodata']),('data',['.data'])]:subprocess.run([PREFIX+'objcopy','-O','binary',*[s for section in sections for s in ['-j',section]],str(elf),str(OUT/(name+'.bin'))],check=True)
code=(OUT/'code.bin').read_bytes();data=(OUT/'data.bin').read_bytes();sha=lambda b:hashlib.sha256(b).hexdigest()
report=dict(format='native-heavy-image-v1',name='IZPSX ARM64 native',codeBase=a.code_base,dataBase=a.data_base,entry=symbols['_start'],codeBytes=len(code),dataBytes=len(data),bssBytes=symbols['__bss_end']-symbols['__bss_start'],dataRegionBytes=symbols['__bss_end']-a.data_base,codeSha256=sha(code),dataSha256=sha(data),undefinedSymbols=[],contextBytes=512,imports=imports,platformPatches=patches,upstreamCommit=subprocess.check_output(['git','-C',str(VENDOR),'rev-parse','HEAD'],text=True).strip(),consoleExecuted=False)
report['sources']={str(f.relative_to(ROOT)):sha(f.read_bytes())for f in [ROOT/'src'/name for name in ['settings.c','settings.h','settings-menu.c','settings-menu.h','logo-data.h','ui-font.h','brand.h','memory-card.c','memory-card.h','sd-layout.h','abi.h','platform.h','display.h','audio.h','entry.S','frontend.c','platform.c','ro-jit.c','fs-ipc.c','display.c','audio.c','fs-dir.h','fs-dir.c','fs-file.h','fs-file.c','native-menu.h','native-menu.c','resident-launcher.h','resident-launcher.c','resident.h','resident.c','keepawake.h','keepawake.c']]};report['runtimeFeatures']={'continuousResidentPlay':{'flag':16,'pacingCounterBits':64,'boundedTrace':True,'boundedPerformance':True}};report['buildScriptSha256']=sha(Path(__file__).read_bytes());report['sdkSnapshotSha256']=profile['sdkSnapshotSha256'];report['coreBuildArguments']=args;report['compilerVersion']=subprocess.check_output([str(CC),'-dumpfullversion'],text=True).strip()
report['sdkFilesystem']=profile['sdkFilesystem']
report['runtimeFeatures']['memoryCards']={'flag':128,'slots':2,'bytesPerSlot':131072,'dualGeneration':True,'flushReadback':True,'debounceMs':500}
report['sdRoot']='/sd/IZ2S/Homebrew/Modules/izpsx'
asm=subprocess.check_output([PREFIX+'objdump','-d',str(elf)],text=True);sections=subprocess.check_output([PREFIX+'readelf','-S',str(elf)],text=True);syscalls=[l for l in asm.splitlines()if any('\t'+op in l for op in ['svc\t','hvc\t','smc\t','eret'])];tls=[l for l in sections.splitlines()if '.tdata'in l or '.tbss'in l];assert not syscalls and not tls,(syscalls,tls)
report['freestandingAudit']=dict(elfSha256=sha(elf.read_bytes()),syscallInstructions=syscalls,tlsSections=tls,noUndefinedSymbols=True);(OUT/'freestanding-audit.json').write_text(json.dumps(report['freestandingAudit'],indent=2)+'\n')
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k]for k in ['codeBytes','dataRegionBytes','undefinedSymbols','consoleExecuted']}))
