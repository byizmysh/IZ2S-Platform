"""Use the pinned RUN25 recipe with the qualified audio worker object."""
import hashlib
from pathlib import Path

recipe = Path('/home/k2/Antigravity/MyLoveSwitch/iz2s-panel/run25-audit/r799-sdk/build.py')
audio = Path(__file__).resolve().parent.parent/'R921-disc-audio-counters/inputs/native_audio_stream.cpp.obj'

asm_helpers = Path(__file__).resolve().parent.parent/'R992-ee-warm-alignment/candidate-source/AsmHelpers.cpp'

emutls = Path(__file__).resolve().parent.parent/'R936-libounce-direct-tls/inputs/emutls.direct.o'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

assert digest(recipe) == 'e2188cccb5bfd869c0b257aad763ac6a8972be7fb98db68d98fffb26060345db'
assert digest(audio) == 'f2177a449da3523eba9fa3de8c2ad8a6560c30c01d9df62b184ec731b03e9fd6'
assert digest(emutls) == '78760f0b72d5868a1e5fb1642a6ebc24f9c1310540aa16407641c272c412462f'
host_sys = Path(__file__).resolve().parent.parent/'R949-ee-warm-publication-tls/candidate-source/HostSys.cpp'
soundtouch = Path(__file__).resolve().parent/'candidate-source/soundtouch/TDStretch.cpp'
text = recipe.read_text()
# The pinned commands use absolute inputs. Their historical staging directory
# can be removed after the inputs have been copied into the local snapshot.
command = 'result=subprocess.run(cmd,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)'
assert text.count(command) == 1
text = text.replace(command, 'result=subprocess.run(cmd,cwd=cwd if Path(cwd).is_dir() else out,env=env,stdout=log,stderr=subprocess.STDOUT)')
archived_launcher = '/tmp/run24-native-iza/work/engines/izhen/source/izlauncher'
if not Path(archived_launcher).is_dir():
    archived = f"base_launcher=Path('{archived_launcher}')"
    durable_launcher = recipe.parents[2] / 'iz2s-platform-run23/engines/izhen/source/izlauncher'
    assert (durable_launcher.parent.parent / 'sdk/sb4/include/sb4/compat.h').is_file()
    assert text.count(archived)==1
    text = text.replace(archived, f'base_launcher=Path({str(durable_launcher)!r})')
iop_jit_provider=Path(__file__).resolve().parent/'candidate-source/jit-cache.c'
provider_marker='src_files=[jit_provider,'
assert text.count(provider_marker)==1
replacements = {
    "for rel in ['legacy/native/abi.h']:\n shutil.copy2(menu_source/rel,menu_support/rel)": "for header in sorted([*menu_source.rglob('*.h'),*menu_source.rglob('*.inc')]):\n target=menu_support/header.relative_to(menu_source);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(header,target)",
    "src_files=[jit_provider,source/'ounce/platform.c',source/'ounce/pc-files.c',launcher_main,legacy_source,menu_support/'legacy/native/abi.h',menu_support/'ounce/menu-factory.h',*menu_files,source/'sdk/sb4/jit-cache.h',host_source,baseline/'source/libounce-jit.c',baseline/'source/libounce-runtime-alloc.h',kaihen_source,kaihen/'include/kaihen.h',kaihen/'include/kaihen/protocol.h']": "menu_adapter_sources={name:menu_source/'ounce'/f'{name}.c' for name in ('apps','import','pc-link','pc-files')}\nmenu_memory=menu_source/'legacy/native/memory.c'\nsrc_files=[jit_provider,source/'ounce/platform.c',*menu_adapter_sources.values(),menu_memory,launcher_main,legacy_source,menu_support/'legacy/native/abi.h',menu_support/'ounce/menu-factory.h',*menu_files,source/'sdk/sb4/jit-cache.h',host_source,baseline/'source/libounce-jit.c',baseline/'source/libounce-runtime-alloc.h',kaihen_source,kaihen/'include/kaihen.h',kaihen/'include/kaihen/protocol.h']",
    "native_sources={'main':launcher_main,'platform':source/'ounce/platform.c','jit-cache':jit_provider,'pc-files':source/'ounce/pc-files.c'}": "native_sources={'main':launcher_main,'platform':source/'ounce/platform.c','jit-cache':jit_provider,**menu_adapter_sources}",
    'replacements[legacy.name]=str(legacy)': "replacements[legacy.name]=str(legacy)\nlegacy_memory=objects/'legacy-memory.o'\nrun([prefix+'gcc',*flags,'-DIZ_USE_LIBC','-MMD','-MF',legacy_memory.with_suffix('.d'),'-c',menu_memory,'-o',legacy_memory],'compile-legacy-memory',base_launcher)\nreplacements[legacy_memory.name]=str(legacy_memory)",
    "host_source=pair_sources/'HostSys.cpp' if pair_sources else baseline/'source/HostSys.cpp'": "host_source=host_sys",
    "vm_sources['AsmHelpers.cpp']=pair_sources/'AsmHelpers.cpp' if pair_sources else ps2/'build/izps2-homebrew/source/pcsx2/arm64/AsmHelpers.cpp'": "vm_sources['AsmHelpers.cpp']=asm_helpers",
    'src_files.extend(vm_sources.values())': "vm_sources['iR5900-arm64.cpp']=ee_medium_smc_source\nvm_sources['recVTLB-arm64.cpp']=ee_medium_smc_source.with_name('recVTLB-arm64.cpp')\nvm_sources['iFPU-arm64.cpp']=ee_medium_smc_source.with_name('iFPU-arm64.cpp')\nvm_sources['R5900.cpp']=ee_medium_smc_source.with_name('R5900.cpp')\nvm_sources['R3000A.cpp']=ee_medium_smc_source.with_name('R3000A.cpp')\nvm_sources['microVU-arm64.cpp']=vu1_permutation_source\nvm_sources['VMManager.cpp']=frame_work_source\nsrc_files.extend(vm_sources.values())",
    'identity={str(p):sha(p) for p in src_files}': '''if options.frame_windows and options.session_timing:
 src_files.append(ps2/'src/ps2_startup_gap_probe.hpp')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/IopOwnerIdleSlice.h')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Persist-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Lower-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Clamp-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_IR-arm64.h')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Compile-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Flags-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Upper-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU-arm64.h')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Misc-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Misc-arm64.h')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Alloc-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/microVU_Branch-arm64.inl')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/AsmHelpers.h')
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/arm64/iCore-arm64.h')
src_files.append(ps2/'build/izps2-homebrew/frontend/video.h')
src_files.extend(vu1_permutation_source.with_name(name) for name in ('microVU-arm64.h', 'microVU_Misc-arm64.h', 'microVU_Upper-arm64.inl', 'microVU_Flags-arm64.inl', 'microVU_IR-arm64.h', 'microVU_Lower-arm64.inl', 'microVU_Compile-arm64.inl', 'microVU_Alloc-arm64.inl', 'microVU_Clamp-arm64.inl'))
src_files.extend(vu1_permutation_source.with_name(name) for name in ('microVU_Misc-arm64.inl', 'microVU_Branch-arm64.inl', 'microVU_Persist-arm64.h', 'microVU_Persist-arm64.inl'))
src_files.append(ps2/'src/ps2_ee_warm_fastload.hpp')
src_files.extend(ee_medium_smc_source.with_name(name) for name in ('PollLatchEmit.h','PollMainRamSegment.h','EeSmcVectorLiteral.h','EeSmcVectorTail.h','EeSmcEarlyExit.h','EeByteCopyBurst.h','EeDmacCause.h','IopQuietEvent.h'))
src_files.append(asm_helpers.with_name('native_ee_reusable_assembler.h'))
src_files.append(expanded_checkpoint_seed)
src_files.extend((mixer, reverb))
src_files.extend((soundtouch, soundtouch.with_name('stereo-dot-four.h')))
src_files.extend(extra_sources.values())
src_files.append(extra_sources['Vif_Transfer.cpp'].with_name('VifNopRun.h'))
src_files.append(ps2/'build/izps2-homebrew/source/pcsx2/Vif_HashBucket.h')
src_files.append(extra_sources['Vif_Unpack.cpp'].with_name('VifShortCopy.h'))
src_files.extend(extra_sources['Vif_Dynarec.cpp'].with_name(name) for name in ('VifStrideUnpack.h','VifUnpackSpan.h'))
src_files.append(extra_sources['native_audio_stream.cpp'].with_name('stereo-dsp-block-queue.h'))
soundtouch_source = ps2/'build/izps2-homebrew/source/3rdparty/soundtouch'
soundtouch_recipe = ps2/'build/izps2-homebrew/obj-hen-sdk/compile_commands.json'
src_files.append(soundtouch_recipe)
src_files.extend(ee_medium_smc_source.with_name(name) for name in ('Arm64EeIndexedBaseBlocks.h','Arm64EeIndexedBlockArray.h'))
src_files.append(options.vm_runner_source.resolve().with_name('ps2_sb4_ee_warm.inc'))
identity={str(p):sha(p) for p in src_files}
original_audio, = [Path(arg) for arg in base if Path(arg).name == audio.name]
assert sha(Path(by_original.get(original_audio.as_posix(), str(original_audio)))) == original_audio_sha
identity[str(audio)] = sha(audio)
identity[str(emutls)] = sha(emutls)
new_iop_header = ps2/'src/ps2_iop_off_sampling.hpp'
identity["run25-iop-sampling-hotpath"] = "1"
identity["run25-ee-cop2-memory-batches"] = "1"
identity[str(new_iop_header)] = sha(new_iop_header)''',
    " vm_sources.update(scaled_core_sources)": " scaled_core_sources.update({name: pcsx/name for name in ('HwWrite.cpp', 'VMManager.cpp', 'Patch.cpp')})\n vm_sources.update(scaled_core_sources)",
    'replacements={}': '''replacements={}
import shlex
for filename, source_file in extra_sources.items():
    entry, = [entry for entry in json.loads(soundtouch_recipe.read_text())
              if Path(entry['file']).name == filename]
    argv = shlex.split(entry['command'])
    target = objects/(filename+'.obj')
    argv[argv.index('-c')+1] = str(source_file)
    argv[argv.index('-o')+1] = str(target)
    if filename == audio.name.removesuffix('.obj'):
        argv.extend(('-iquote', str(ps2/'src')))
        argv.append('-DIZPS2_SDK_DSP_WAKE=1')
    elif filename == 'AudioStream.cpp':
        argv.extend(('-iquote', str(ps2/'build/izps2-homebrew/source/pcsx2/Host')))
    run(argv, 'compile-'+filename, base_launcher)
    replacements[target.name] = str(target)
soundtouch_input, = [Path(arg) for arg in base if Path(arg).name.endswith('-libpcsx2-soundtouch.a')]
soundtouch_pinned = Path(by_original[str(soundtouch_input)])
assert sha(soundtouch_pinned) == '0e78ed8a2b2dd1b8e4fc8322aff49fea043ed1f4f19b0dbec3b4d7031d6dc639'
soundtouch_archive = objects/soundtouch_input.name
shutil.copy2(soundtouch_pinned, soundtouch_archive)
soundtouch_entry, = [entry for entry in json.loads(soundtouch_recipe.read_text())
                     if Path(entry['file']).name == soundtouch.name]
soundtouch_argv = shlex.split(soundtouch_entry['command'])
soundtouch_obj = objects/'TDStretch.cpp.obj'
soundtouch_argv[soundtouch_argv.index('-c')+1] = str(soundtouch)
soundtouch_argv[soundtouch_argv.index('-o')+1] = str(soundtouch_obj)
soundtouch_argv.extend(('-DIZPS2_STEREO_OVERLAP_BATCH=1', '-iquote', str(soundtouch_source/'source/SoundTouch')))
run(soundtouch_argv, 'compile-TDStretch.cpp', base_launcher)
previous_members = subprocess.check_output([prefix+'ar','t',soundtouch_archive],text=True).splitlines()
assert previous_members.count(soundtouch_obj.name) == 1
run([prefix+'ar','r',soundtouch_archive,soundtouch_obj], 'archive-soundtouch')
assert subprocess.check_output([prefix+'ar','t',soundtouch_archive],text=True).splitlines() == previous_members
replacements[soundtouch_archive.name] = str(soundtouch_archive)
mixer_entry, = [entry for entry in json.loads(soundtouch_recipe.read_text())
                 if entry['file'].endswith('/SPU2/Mixer.cpp')]
mixer_argv = shlex.split(mixer_entry['command'])
mixer_obj = objects/'Mixer.cpp.obj'
mixer_argv[mixer_argv.index('-c')+1] = str(mixer)
mixer_argv[mixer_argv.index('-o')+1] = str(mixer_obj)
run(mixer_argv, 'compile-Mixer.cpp', base_launcher)
reverb_entry, = [entry for entry in json.loads(soundtouch_recipe.read_text())
                 if entry['file'].endswith('/SPU2/ReverbResample.cpp')]
reverb_argv = shlex.split(reverb_entry['command'])
reverb_obj = objects/'ReverbResample.cpp.obj'
reverb_argv[reverb_argv.index('-c')+1] = str(reverb)
reverb_argv[reverb_argv.index('-o')+1] = str(reverb_obj)
run(reverb_argv, 'compile-ReverbResample.cpp', base_launcher)''',
    "run([prefix+'ar','r',core,*(replacements[member] for member in members)],'archive-core')": "assert before.count(mixer_obj.name)==before.count(reverb_obj.name)==1\n assert all(before.count(name+'.obj') == 1 for name in extra_core_cpp)\n run([prefix+'ar','r',core,*(replacements[member] for member in members),*(replacements[name+'.obj'] for name in extra_core_cpp),mixer_obj,reverb_obj],'archive-core')",
    'link.extend(map(str,(sdk_jit,kaihen_jit,libounce)))': 'link.extend(map(str,(sdk_jit,kaihen_jit,emutls,libounce)))',
    " if name=='ps2_vm_runner.cpp' and options.iop_session_timing:": """ if options.ee_tagged_pages and name in ('iR5900-arm64.cpp','recVTLB-arm64.cpp'):
  extra.extend(('-DPS2_EE_SOFTMEM_COP2_BATCH=1','-DPS2_EE_SOFTMEM_FPR_PAGE_GROUP=1'))
 if name in ('iR3000A-arm64.cpp','ps2_vm_runner.cpp'):
  extra.append('-DIZPS2_SWITCH2_IOP_IDLE_ENTRY=1')
 if name in ('R5900.cpp', 'iR3000A-arm64.cpp'):
  extra.append('-DPS2_IOP_OWNER_IDLE_SLICE=1')
 if name=='R5900.cpp' and options.iop_session_timing:
  extra.append('-DIZPS2_SWITCH2_IOP_SESSION_TIMING=1')
 if name in ('VMManager.cpp', 'ps2_vm_runner.cpp'):
  extra.append('-DIZPS2_SWITCH2_FRAME_END_PACING=1')
 if name=='iR5900-arm64.cpp':
  extra.extend(('-DIZPS2_SWITCH2_SHORT_SMC_EXPECTATIONS=1', '-DIZPS2_EE_POLL_WATCHED_CODE=1', '-DPS2_EE_SOFTMEM_GPR_LOAD_GROUP=1'))
  extra.extend(('-DIZPS2_EE_SMC_VECTOR_TAIL=1', '-DIZPS2_EE_SMC_FAR_LIVE_LITERAL=1', '-DIZPS2_EE_SMC_EARLY_EXIT=1'))
 if name=='microVU-arm64.cpp':
  extra.extend(('-DIZPS2_VU1_FIXED_SLOTS=1', '-DIZPS2_VU1_NUMERIC_OPERANDS=1'))
 if name in ('iR5900-arm64.cpp', 'microVU-arm64.cpp', 'recVTLB-arm64.cpp'):
  extra.extend(('-iquote', str(ps2/'build/izps2-homebrew/source/pcsx2/arm64')))
 if name in ('AsmHelpers.cpp', 'iR5900-arm64.cpp') and options.ee_warm_ranges and options.defer_ee:
  extra.append('-DIZPS2_SWITCH2_EE_WARM_ALIGN=1')
 if name in ('AsmHelpers.cpp', 'ps2_vm_runner.cpp'):
  extra.append('-DIZPS2_SWITCH2_REUSABLE_ASSEMBLER=1')
 if name=='ps2_vm_runner.cpp' and options.ee_warm_ranges:
  extra.append('-DIZPS2_SWITCH2_CHECKPOINT_PUBLICATION_SCOPE=1')
 if name=='Patch.cpp':
  extra.append('-DIZPS2_SWITCH2_GTA_CADENCE_TRIAL=1')
 if name=='ps2_vm_runner.cpp':
  extra.append('-DIZPS2_SWITCH2_EE_WARM_FASTLOAD=1')
  extra.append('-DIZPS2_SWITCH2_EAGER_AB=1')
 if name=='ps2_vm_runner.cpp' and options.iop_session_timing:""",
}
for before, after in replacements.items():
    assert text.count(before) == 1
    text = text.replace(before, after)
for before, after in {"for header in sorted([*menu_source.rglob('*.h'),*menu_source.rglob('*.inc')]):\n target=menu_support/header.relative_to(menu_source);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(header,target)": "menu_snapshot=options.reuse_launcher_from.resolve(strict=True)/'source/menu-support'\nshutil.copytree(menu_snapshot,menu_support,dirs_exist_ok=True)", "menu_adapter_sources={name:menu_source/'ounce'/f'{name}.c' for name in ('apps','import','pc-link','pc-files')}": "menu_adapter_sources={name:pinned_launcher_object('ounce-'+name+'.o') for name in ('apps','import','pc-link','pc-files')}", "menu_memory=menu_source/'legacy/native/memory.c'": "menu_memory=pinned_launcher_object('legacy-memory.o')", "if name=='main' and launcher_reference:": "if (name=='main' or name in menu_adapter_sources) and launcher_reference:", "run([prefix+'gcc',*flags,'-DIZ_USE_LIBC','-MMD','-MF',legacy_memory.with_suffix('.d'),'-c',menu_memory,'-o',legacy_memory],'compile-legacy-memory',base_launcher)": 'shutil.copy2(menu_memory,legacy_memory)\nassert sha(legacy_memory)==identity[str(menu_memory)]'}.items():
    assert text.count(before) == 1, before
    text = text.replace(before, after)
# FTP and PC Link share socket ownership. Compile both from the same RUN25
# source, replacing the archived objects together.
assert text.count("**menu_adapter_sources}") == 1
text = text.replace("**menu_adapter_sources}",
                    "**menu_adapter_sources, **current_network_sources}")
identity_anchor_network = "src_files.extend(menu_adapter_sources.values())"
if identity_anchor_network in text:
    assert text.count(identity_anchor_network) == 1
    text = text.replace(identity_anchor_network, identity_anchor_network +
                        "\nsrc_files.extend(sorted(path for path in current_network_root.rglob('*') if path.is_file()))")
else:
    identity_anchor_network = "identity={str(p):sha(p) for p in src_files}"
    assert text.count(identity_anchor_network) == 1
    text = text.replace(identity_anchor_network,
                        "src_files.extend(sorted(path for path in current_network_root.rglob('*') if path.is_file()))\n" + identity_anchor_network)
current_network_root = Path(__file__).resolve().parent/'launcher-source'
current_network_sources = {name: current_network_root/f"{name}.c" for name in ("ftp-connect", "pc-link", "pc-audio", "pc-video")}
# The last two dictionary entries replace archived names. Never copy the old
# PC Link object in the native loop when its replacement points to C source.
before = "(name=='main' or name in menu_adapter_sources) and launcher_reference"
assert text.count(before) == 1
text = text.replace(before, "(name=='main' or (name in menu_adapter_sources and "
                            "name not in current_network_sources)) and launcher_reference")
ftpsrv_loop = "for name,p in native_sources.items():"
assert text.count(ftpsrv_loop) == 1
text = text.replace(ftpsrv_loop, r"""
ftp_source = current_network_root/'ftpsrv'
flags.insert(1, '-I'+str(ftp_source))
ftp_object = objects/'ftp-ftpsrv.o'
ftp_flags = [
    '-D_DEFAULT_SOURCE', '-DHAVE_POLL=1', '-DHAVE_SO_REUSEADDR=1',
    '-DHAVE_TCP_NODELAY=1', '-DHAVE_LOCALTIME_R=1', '-DHAVE_GMTIME_R=1',
    '-DHAVE_STRNCASECMP=1', '-DFTP_MAX_SESSIONS=2',
    '-DFTPSRV_VERSION_STR="99253bd"', '-DFTP_SOCKET_HEADER="socket_unistd.h"',
    '-DFTP_VFS_HEADER="vfs_sd.h"', '-Wno-error',
]
run([prefix+'gcc',*flags,*ftp_flags,'-MMD','-MF',ftp_object.with_suffix('.d'),
     '-c',ftp_source/'ftpsrv.c','-o',ftp_object],'compile-ftpsrv',base_launcher)
replacements[ftp_object.name] = str(ftp_object)
""" + ftpsrv_loop)
assert text.count("run25/'engines/izhen/") == 4
text = text.replace("run25/'engines/izhen/", "run25/'engines/iz2s-core/")
native_platform = Path(__file__).resolve().parent.parent/'R954-ee-warm-fastload/candidate-source/platform.c'
before = "source/'ounce/platform.c'"
assert text.count(before) == 2
text = text.replace(before, 'native_platform')
candidate = Path(__file__).resolve().parent/'candidate-source'
new_c_files = ('ps2_sb4_preferences.c', 'ps2_psx_ui.c', 'ps2_psx_settings_menu.c', 'audio-dsp-wake-sdk.c')
adapter_names = ('native_pad.c', 'native_pad_frontend.cpp', *new_c_files)
identity_anchor = 'identity={str(p):sha(p) for p in src_files}'
assert text.count(identity_anchor) == 1
text = text.replace(identity_anchor, """
src_files.extend(candidate / name for name in (
 'native_pad.c', 'native_pad.h', 'native_pad_frontend.cpp', 'native_pad_frontend.h',
  'ps2_controller_settings.h', 'ps2_sb4_preferences.c', 'ps2_sb4_preferences.h',
 'ps2_psx_ui.c', 'ps2_psx_ui.h', 'ps2_psx_settings_menu.c', 'ps2_psx_settings_menu.h',
 'psx-ui-assets/logo-data.h', 'psx-ui-assets/ui-font.h', 'ps2_sb4_session.h',
 'audio-dsp-wake-sdk.c', 'audio-dsp-wake-sdk.h'))
""" + identity_anchor)
frontend = Path(__file__).resolve().parent / 'frontend/frontend.bin'
assert frontend.is_file() and frontend.stat().st_size % 4 == 0
line_before_hash = "src_files.extend(vm_sources.values())"
assert text.count(line_before_hash)==1
text = text.replace(line_before_hash, line_before_hash + "\nsrc_files.append(frontend)")
text = text.replace(line_before_hash, """
vm_sources['vtlb.cpp'] = candidate/'vtlb.cpp'
vm_sources['VMManager.cpp'] = candidate/'VMManager.cpp'
vm_sources['video.cpp'] = candidate/'video.cpp'
src_files.extend(candidate/name for name in ('EeDmaWrite.h', 'EeWriteShadow.h', 'EeCodeWriteIndex.h',
    'EeCodeWordCoverage.h', 'EeGprLoadPageGroup.h', 'EePollCodeEpoch.h', 'EeSoftmemTableStorage.h', 'EeSoftmemWriteTag.h',
    'ps2_frame_windows.inc', 'ps2_eager_ab.inc', 'ps2_eager_ab_frames.hpp'))
""" + line_before_hash)
link_anchor = "elf=out/'izlauncher-sdk.elf';link=[]"
assert text.count(link_anchor) == 1
text = text.replace(link_anchor, """
frontend_asm=objects/'linked-izps2-app.S'
frontend_obj=objects/'linked-izps2-app-paths.o'
frontend_asm.write_text('\\n'.join((
    '.section .text.izps2_linked_app,"ax",%progbits',
    '.p2align 12',
    '.global izps2_linked_app, izps2_linked_app_end',
    '.type izps2_linked_app, %function',
    'izps2_linked_app:',
    '.incbin "' + str(frontend) + '"',
    'izps2_linked_app_end:',
    '.size izps2_linked_app, izps2_linked_app_end - izps2_linked_app',
    '',
)))
run([prefix+'gcc','-c',frontend_asm,'-o',frontend_obj],'compile-linked-izps2-menu')
assert sum(Path(arg).name.endswith('linked-izps2-app-paths.o') for arg in base)==1
adapter_recipes = ps2/'build/izps2-homebrew/logs/hen-izps2-r799-integration-3RdPdi/compile'
for name in adapter_names:
    recipe_name = 'native_pad.c' if name in new_c_files else name
    record = json.loads((adapter_recipes/(recipe_name+'.json')).read_text())
    argv = record['argv']
    raw = objects/(name+'.raw.o')
    isolated = objects/(name+'.obj')
    argv[argv.index('-c')+1] = str(candidate/name)
    argv[argv.index('-o')+1] = str(raw)
    for flag in ('-MF','-MT'):
        if flag in argv:
            argv[argv.index(flag)+1] = str(raw.with_suffix('.d'))
    if name == 'audio-dsp-wake-sdk.c':
        argv.extend(('-isystem', str(sdk/'libounce/include')))
    run(argv,'compile-'+name,Path(record['cwd']))
    run([prefix+'objcopy','--redefine-syms='+str(ps2/'build/izps2-homebrew/logs/hen-izps2-static-HSkTmn/vulkan-symbols.txt'),raw,isolated],'isolate-'+name)
    replacements[isolated.name] = str(isolated)
""" + link_anchor)
line=" link.append(arg)"
insert=""" if Path(arg).name.endswith('linked-izps2-app-paths.o'):
  link.append(str(frontend_obj))
  continue
 link.append(arg)
 if Path(arg).name == 'native_pad.c.obj':
  link.extend(replacements[name+'.obj'] for name in new_c_files)"""
assert text.count(line) == 1
text = text.replace(line, insert)
pcsx = candidate.parents[4]/'build/izps2-homebrew/source/pcsx2'
extra_sources = {name: candidate/name for name in ('AudioStream.cpp','native_audio_stream.cpp','Vif_Transfer.cpp')}
extra_sources.update({'Vif_Dynarec.cpp':candidate/'Vif_Dynarec.cpp', 'Vif_Unpack.cpp':candidate/'Vif_Unpack.cpp'})
extra_sources.update({name: candidate/name for name in ('Sif0.cpp', 'sif2.cpp', 'IPUdma.cpp', 'SPR.cpp', 'Hw.cpp', 'R5900OpcodeImpl.cpp')})
provider_anchor = "src_files=[jit_provider,"
assert text.count(provider_anchor)==1
text=text.replace(provider_anchor, "jit_provider=iop_jit_provider\n"+provider_anchor)
text=text.replace("src_files.extend(vm_sources.values())",
                  "src_files.append(candidate/'ps2_iop_jit_lease.hpp')\nsrc_files.extend(vm_sources.values())", 1)
exec(compile(text, str(recipe), 'exec'), {
    '__name__':'__main__', '__file__':str(recipe), 'audio':audio, 'emutls':emutls, 'asm_helpers':asm_helpers, 'host_sys':host_sys, 'native_platform':native_platform, 'vu1_permutation_source':Path(__file__).resolve().parent/'candidate-source/microVU-arm64.cpp', 'expanded_checkpoint_seed':Path(__file__).resolve().parent.parent/'R993-ee-burst-evidence/model/ee-warm2-3024136446-3024136446-checkpoint.bin', 'frame_work_source':Path(__file__).resolve().parent.parent/'R990-single-frame-work-sample/candidate-source/VMManager.cpp', 'ee_medium_smc_source':Path(__file__).resolve().parent/'candidate-source/iR5900-arm64.cpp',
    'current_network_root':current_network_root, 'current_network_sources':current_network_sources,
    'frontend':frontend, 'adapter_names':adapter_names, 'new_c_files':new_c_files, 'candidate':candidate, 'extra_sources':extra_sources, 'extra_core_cpp':tuple(name for name in extra_sources if name != 'native_audio_stream.cpp'),
    'soundtouch':soundtouch, 'mixer':Path(__file__).resolve().parent/'candidate-source/Mixer.cpp',
    'reverb':Path(__file__).resolve().parent/'candidate-source/ReverbResample.cpp',
    'iop_jit_provider':iop_jit_provider,
    'original_audio_sha':'d4c93f9d9bd23df5c41718cc86da0b6e8ba5c35b970351784a97cdeaa4c907e3',
})
