"""Build an offline IZPSX SD application for the public native ABI 5.
No installation, console requests, network access or vendor edits.
"""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys,zlib
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from local_build import CC,PREFIX
ROOT=Path(__file__).resolve().parents[1]
from local_build import PANEL
OUT=ROOT/'build/izpsx-sd';OUT.mkdir(parents=True,exist_ok=True)
sha=lambda b:hashlib.sha256(b).hexdigest()
report={'format':'izpsx-native-sd-build/1','passed':False,'consoleExecuted':False,'performanceGainPercent':None,'commands':[]}
def run(args):
 args=list(map(str,args));report['commands'].append(args)
 r=subprocess.run(args,cwd=ROOT,capture_output=True,text=True)
 if r.returncode:raise RuntimeError(' '.join(args[:4])+'\n'+r.stdout+r.stderr)
 return r.stdout
try:
 native=json.loads((ROOT/'build/native/report.json').read_text());count=len(native['imports'])
 # Public ABI is checked by compile-time offsets and runtime prefix/capabilities.
 # Do not pin unrelated launcher internals or its release version.
 sdk_functions=json.loads((PANEL/'core/console/abi/sdk-functions.json').read_text())
 sdk_functions['sleep']=json.loads((PANEL/'core/console/abi/thread-code.json').read_text())['functions']['sleep']
 sdk_functions['tick']=json.loads((PANEL/'core/console/abi/timing-code.json').read_text())['functions']['tick']
 lines=['static const struct {uint64_t offset;uint8_t bytes[32];} izpsx_imports[]={']
 for i in native['imports']:lines.append('{'+str(i['sdkOffset'])+', {'+','.join('0x'+i['bytes'][j:j+2] for j in range(0,64,2))+'}},')
 lines+=['};','static const struct {uint64_t offset;uint8_t length,bytes[32];} izpsx_services[]={']
 for key in ['send','close','allocate','free','sleep','tick']:
  i=sdk_functions[key];signature=i['bytes'][:64];lines.append('{'+str(i['offset'])+', '+str(len(signature)//2)+', {'+','.join('0x'+signature[j:j+2] for j in range(0,len(signature),2))+'}},')
 (OUT/'sd-imports.h').write_text('\n'.join(lines+['};'])+'\n')
 common=['-O2','-D_GNU_SOURCE','-DPS1_IMPORT_COUNT='+str(count),'-ffreestanding','-fno-builtin','-fno-stack-protector','-mbranch-protection=bti','-mno-outline-atomics','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/sd-runtime'),'-I'+str(OUT)]
 core=common+['-fno-pie','-fno-pic','-mcmodel=large',*['-I'+str(ROOT/'vendor/pcsx_rearmed'/p) for p in ['deps/libretro-common/include','include','libpcsxcore']]]
 objects=[]
 for name,source in [('replay','src/replay.c'),('controllers','src/sd-runtime/controllers.c'),('frontend-sd','src/frontend-sd.c'),('engine','src/sd-runtime/engine.c'),('sd-entry','src/sd-runtime/entry.S'),('address','src/sd-runtime/address.c'),('system','src/sd-runtime/system.S')]:
  obj=OUT/(name+'.o');run([CC,*core,'-c',ROOT/source,'-o',obj]);objects.append(obj)
 for name in ['platform','settings','settings-menu','memory-card','ro-jit','fs-ipc','display','audio','fs-dir','fs-file','native-menu','keepawake','imports']:objects.append(ROOT/'build/native'/(name+'.o'))
 script='''ENTRY(izpsx_engine)
SECTIONS {
 . = CODE; .text : { *(.text.izpsx_entry) *(.text .text.*) } . = ALIGN(16); .rodata : { *(.rodata .rodata.*) }
 . = DATA; .data : { QUAD(0); *(.data .data.*) *(.data.rel.ro .data.rel.ro.*) *(.got .got.*) }
 . = ALIGN(16); __bss_start = .; .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } . = ALIGN(16); __bss_end = .;
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}'''
 def link(label,cb,db):
  ld=OUT/(label+'.ld');ld.write_text(script.replace('CODE',hex(cb)).replace('DATA',hex(db)))
  elf=OUT/(label+'.elf');run([CC,'-nostdlib','-static','-no-pie','-Wl,-u,retro_get_system_info,-u,ps1_sd_core_frame,-u,izpsx_release_stack,--build-id=none,--gc-sections,--emit-relocs,-T,'+str(ld),*objects,ROOT/'build/native/pcsx.a','-lgcc','-o',elf]);assert not run([PREFIX+'nm','-u',elf]).strip();return elf
 def extract(elf):
  f=elf.open('rb');e=ELFFile(f);t=e.get_section_by_name('.text');r=e.get_section_by_name('.rodata');d=e.get_section_by_name('.data');b=e.get_section_by_name('.bss');cb=t['sh_addr'];db=d['sh_addr'];code=bytearray(r['sh_addr']+r['sh_size']-cb);code[:t['sh_size']]=t.data();code[r['sh_addr']-cb:]=r.data();data=bytearray(d.data());region=b['sh_addr']+b['sh_size']-db
  symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};return f,e,[code,data],[cb,db],region,symbols
 elf=link('engine',0x48000000,0x4c000000);f,e,segments,bases,region,symbols=extract(elf)
 (OUT/'code.bin').write_bytes(segments[0]);(OUT/'data.bin').write_bytes(segments[1])
 records=[];seen=set();invariants=0
 allowed={275,277,280,282,283,286,299}
 for section in e.iter_sections():
  if not isinstance(section,RelocationSection):continue
  source=e.get_section(section['sh_info']);assert source.name in ('.text','.rodata','.data');sg=int(source.name=='.data');table=e.get_section(section['sh_link'])
  for r in section.iter_relocations():
   symbol=table.get_symbol(r['r_info_sym']);assert isinstance(symbol['st_shndx'],int);target=e.get_section(symbol['st_shndx']);assert target.name in ('.text','.rodata','.data','.bss');tg=int(target.name in ('.data','.bss'));kind=r['r_info_type']
   if kind!=257:assert kind in allowed and sg==tg==0,(kind,source.name,target.name);invariants+=1;continue
   offset=r['r_offset']-bases[sg];value=struct.unpack_from('<Q',segments[sg],offset)[0];assert value==symbol['st_value']+r['r_addend'] and offset%8==0;assert (sg,offset)not in seen;seen.add((sg,offset));records.append((sg,tg,offset,value,value-bases[tg]))
 f.close();records.sort(key=lambda r:(r[0],r[2]))
 # Independent relink, with different code/data deltas, validates every byte.
 far=link('independent',0x6700100000,0x6808200000);ff,_,expected,new_bases,_,_=extract(far);ff.close();actual=[bytearray(x) for x in segments]
 for name,blob in zip(['independent-code.bin','independent-data.bin'],expected):(OUT/name).write_bytes(blob)
 for sg,tg,offset,value,target in records:struct.pack_into('<Q',actual[sg],offset,new_bases[tg]+target)
 assert actual==expected,'Independent relocation oracle differs'
 fixups=b''.join(struct.pack('<IHBBQQQ',offset,1,sg,tg,value,target,0) for sg,tg,offset,value,target in records)
 header=bytearray(256);header[:8]=b'IZNATRL1';struct.pack_into('<II',header,8,1,256)
 offsets=[256,(256+len(segments[0])+7)&~7,0];offsets[2]=(offsets[1]+len(segments[1])+7)&~7
 struct.pack_into('<Q2Q2I2I3QII3IIII',header,16,offsets[2]+len(fixups),*bases,*map(len,segments),(len(segments[0])+4095)&~4095,(region+4095)&~4095,*offsets,len(records),32,*[zlib.crc32(x) for x in [*segments,fixups]],symbols['izpsx_engine']-bases[0],symbols['izpsx_release_stack']-bases[0],0)
 asset=bytearray(offsets[2]+len(fixups));asset[:256]=header
 for at,part in zip(offsets,[*segments,fixups]):asset[at:at+len(part)]=part
 asset=bytes(asset);digest=sha(asset);asset_name='asset-'+digest+'.izdata';(OUT/asset_name).write_bytes(asset)
 (OUT/'sd-asset.h').write_text('#define IZPSX_ENGINE_ASSET "'+asset_name+'"\n')
 pic=common+['-fPIC','-fvisibility=hidden']
 for name,source in [('shim','src/sd-runtime/shim.c'),('pic-menu','src/native-menu.c')]:run([CC,*pic,'-c',ROOT/source,'-o',OUT/(name+'.o')])
 ld=OUT/'shim.ld';ld.write_text('ENTRY(iz_app)\nSECTIONS { . = 0; .text : { *(.text.iz_app) *(.text*) *(.rodata*) } .data : { *(.data*) *(.got*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) } }\n')
 shim=OUT/'shim.elf';run([CC,'-nostdlib','-static','-Wl,--build-id=none,--gc-sections,-T,'+str(ld),OUT/'shim.o',OUT/'pic-menu.o','-o',shim]);assert not run([PREFIX+'nm','-u',shim]).strip()
 with shim.open('rb') as sf:
  se=ELFFile(sf);assert all(not se.get_section_by_name(n) or not se.get_section_by_name(n)['sh_size'] for n in ['.data','.bss']);shim_bytes=se.get_section_by_name('.text').data()
 (OUT/'shim.bin').write_bytes(shim_bytes)
 assembly=run([PREFIX+'objdump','-d',elf]);svc=[line.strip() for line in assembly.splitlines() if '\tsvc\t' in line];assert len(svc)==3 and not any(op in assembly for op in ['\thvc\t','\tsmc\t','tpidr_el0','tpidrro_el0'])
 report.update(readOnlyKernelBoundaries=svc,finiteJitSelfProbeExecutions=64,passed=True,codeBytes=len(segments[0]),dataBytes=len(segments[1]),dataRegionBytes=region,engineBytes=len(asset),asset=asset_name,assetSha256=digest,shimBytes=len(shim_bytes),absoluteFixups=len(records),invariantFixups=invariants,independentRelinkExact=True,entryOffset=symbols['izpsx_engine']-bases[0],releaseOffset=symbols['izpsx_release_stack']-bases[0],symbols={k:symbols[k] for k in ['ps1_sd_core_begin','ps1_sd_core_frame','ps1_sd_core_frame4','ps1_sd_core_end','ps1_sd_jit_probe','ps1_imports','Mcd1Data','Mcd2Data','SaveMcd','ps1_cards_open','in_keystate','in_type','ps1_sd_core_ports','ps1_active_settings','ps1_preferences_default','multitap1','multitap2','PAD1_startPoll','PAD1_poll','PAD2_startPoll','PAD2_poll','retro_get_system_info']},launcherRoot=str(PANEL),sdRoot='/sd/IZ2S/Homebrew/Modules/izpsx',sources={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in [ROOT/'src/frontend-sd.c',ROOT/'src/replay.c',ROOT/'src/replay.h',*sorted((ROOT/'src/sd-runtime').iterdir())] if p.is_file()},nativeSourceHashes=native['sources'],sdkProfileSha256=sha((ROOT/'abi/sdk-profile.json').read_bytes()),nativeAbi=5,consoleScope='Unexecuted candidate; hardware loading, audio and lifecycle qualification still required')
finally:
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['passed','engineBytes','shimBytes','absoluteFixups','independentRelinkExact','consoleExecuted']}))
