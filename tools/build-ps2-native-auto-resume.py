"""Add a libc-initialized launcher main wrapper; preserve every PS2 input object."""
from pathlib import Path
from io import BytesIO
import copy, hashlib, importlib.util, json, os, shutil, signal, subprocess, sys, time
from elftools.elf.elffile import ELFFile

R = Path(__file__).resolve().parents[1]
W = R / 'data/ps2-cold-work'
OLD = W / 'build/r286-performance-ready-source-v1'
OUT = W / ('build/launcher-native-auto-resume-' + str(time.time_ns() // 1000000))
SOURCE = R / 'core/console/sd-launcher/ps2/auto-resume-entry.c'
H = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
J = lambda p: json.loads(Path(p).read_text())
def put(p, v):
    Path(p).write_text(json.dumps(v, indent=2) + '\n')
def main():
    assert Path.cwd() == R
    OUT.mkdir(); (OUT/'logs').mkdir()
    m = J(OLD/'manifest.json'); old = copy.deepcopy(m)
    report = dict(format='ps2-native-launcher-auto-resume/1', passed=False,
        change='Enable existing PS2_AUTO_RESUME through the original libc main entry',
        performanceGainPercent=None, metric='native wrapper build and exact inherited inputs',
        before=None, after=None, status='Hardware latency gain has not been measured',
        scenario='Unchanged R286 PS2 / R324 allocator with launcher startup opt-in',
        environment='Local native compile/link/pack; no console operation',
        SDKCalls=0, consoleContacted=False, children=[], coreObjectsChanged=False,
        foreignSDKCallsToMuslFunctions=False, hardwareEntryAllowed=False,
        limitations=['Initial GTA gameplay state has not yet been captured.', 'No hardware activation or performance claim.'])
    def run(argv, label):
        put(OUT/(label+'-command.json'), list(map(str, argv)))
        env={k:v for k,v in os.environ.items() if not k.startswith(('LD_','PS2_','QEMU_','CLANG_'))}
        env.update(LC_ALL='C', HBXL_TOOLCHAIN=m['toolchain'])
        with (OUT/'logs'/(label+'.log')).open('xb') as log:
            p=subprocess.Popen(list(map(str,argv)),cwd=W,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            row=dict(label=label,pid=p.pid,owned=True,reaped=False);report['children'].append(row)
            put(OUT/'report.json',report);print(json.dumps(dict(stage=label,pid=p.pid)),flush=True)
            try: rc=p.wait(55); timed=False
            except subprocess.TimeoutExpired:
                timed=True;os.killpg(p.pid,signal.SIGKILL);rc=p.wait(5)
            try:os.killpg(p.pid,0);empty=False
            except ProcessLookupError:empty=True
            row.update(reaped=True,returnCode=rc,timedOut=timed,groupEmpty=empty)
            put(OUT/'report.json',report)
            assert rc==0 and not timed and empty,(label,rc)
    try:
        pins={**m['executionInputs'],**m['metadataInputs']}
        for f,h in pins.items():assert H(f)==h,f
        compiler=m['linkCommand'][0]; obj=OUT/'auto-resume-entry.o'
        run([compiler,'-O2','-ffunction-sections','-fdata-sections','-fPIC','-mbranch-protection=bti','-fno-stack-protector','-c',SOURCE,'-o',obj],'compile-launcher-entry')
        code_base=0x2800000000; data_base=0x28c0000000
        cmd=m['linkCommand'][:]
        for i,v in enumerate(cmd):
            if v.startswith('-Wl,--defsym=HBXL_CODE_BASE='):cmd[i]='-Wl,--defsym=HBXL_CODE_BASE='+hex(code_base)
            elif v.startswith('-Wl,--defsym=HBXL_DATA_BASE='):cmd[i]='-Wl,--defsym=HBXL_DATA_BASE='+hex(data_base)
            elif v.startswith('-Wl,-Map='):cmd[i]='-Wl,-Map='+str(OUT/'link.map')
            elif v=='-o':cmd[i+1]=str(OUT/'native.elf')
        cmd[1:1]=[str(obj),'-Wl,--wrap=main']
        run(cmd,'native-link')
        elf=ELFFile(BytesIO((OUT/'native.elf').read_bytes())); loads=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        assert len(loads)==2 and [s['p_flags']for s in loads]==[5,6]
        syms={s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        wrap=syms['__wrap_main']; original=syms['main'];setenv=syms['setenv']
        assert wrap['st_value']!=original['st_value'] and wrap['st_size']>0
        assert loads[0].data()[wrap['st_value']-code_base:wrap['st_value']-code_base+4]==bytes.fromhex('5f2403d5')
        # The retained relocation table proves run_libc takes __wrap_main,
        # while the new wrapper calls the original main and native setenv.
        references=[]
        for section in elf.iter_sections():
            if section['sh_type'] not in ['SHT_RELA','SHT_REL']:continue
            table=elf.get_section(section['sh_link'])
            for rel in section.iter_relocations():
                name=table.get_symbol(rel['r_info_sym']).name
                if name in ['__wrap_main','main','setenv']:
                    references.append(dict(name=name,address=rel['r_offset'],type=rel['r_info_type']))
        assert any(r['name']=='__wrap_main' for r in references)
        assert any(r['name']=='main' and wrap['st_value']<=r['address']<wrap['st_value']+wrap['st_size'] for r in references)
        assert any(r['name']=='setenv' and wrap['st_value']<=r['address']<wrap['st_value']+wrap['st_size'] for r in references)
        actual_loads=[s[5:]for s in (OUT/'link.map').read_text().splitlines() if s.startswith('LOAD ')]
        assert [s for s in actual_loads if s!=str(obj)]==old['linkMapLoads']
        m['linkMapLoads']=actual_loads
        m['imageBounds'].update(codeBytes=loads[0]['p_filesz'],dataBytes=loads[1]['p_filesz'],dataRegionBytes=loads[1]['p_memsz'],bssBytes=loads[1]['p_memsz']-loads[1]['p_filesz'],codePages=(loads[0]['p_filesz']+4095)//4096)
        for name,row in m['expectedExports'].items():
            s=syms[name];base=code_base if row['segment']=='code' else data_base
            assert s['st_size']==row['bytes'];row['offset']=s['st_value']-base
        fused=m['nativeFusedOpaqueCopy']['selectedHelper'];s=syms[fused['name']]
        fused['offset']=s['st_value']-code_base
        fused['wholeFunctionSha256']=hashlib.sha256(loads[0].data()[fused['offset']:fused['offset']+s['st_size']]).hexdigest()
        hidden=m['nativeActivity']['hiddenStatistics'];s=syms[hidden['name']]
        hidden['offset']=s['st_value']-code_base;hidden['address']=s['st_value']
        bank=hidden['bank'];bank['offset']=syms[bank['name']]['st_value']-data_base
        bank['address']=syms[bank['name']]['st_value']
        spec=importlib.util.spec_from_file_location('original_relink_audit',OLD/'relink.py'); audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
        exports,code,data,diagnostic=audit.audit(OUT/'native.elf',m,code_base,data_base)
        assert list(m['expectedExports'])==list(old['expectedExports'])
        app=OUT/'app';app.mkdir()
        manifest=copy.deepcopy(m['appManifest']);manifest['linux'].update(code_base=hex(code_base),data_base=hex(data_base));put(app/'manifest.json',manifest)
        run([m['python'],m['packer'],'--pack',OUT/'native.elf',app],'native-pack')
        image=J(app/'build/report.json')
        assert (app/'build/code.bin').read_bytes()==code and (app/'build/data.bin').read_bytes()==data
        for k,v in m['imageBounds'].items():assert image[k]==v,(k,image[k],v)
        for f,h in pins.items():assert H(f)==h,f
        report.update(passed=True,allInputsPostExact=True,allOwnedChildrenReaped=True,allOwnedGroupsEmpty=True,
            inheritedManifest=dict(path=str(OLD/'manifest.json'),sha256=H(OLD/'manifest.json')),
            originalExecutionInputs=old['executionInputs'],newObject=dict(path=str(obj),sha256=H(obj)),
            source=dict(path=str(SOURCE),sha256=H(SOURCE)),linkCommand=cmd,originalInputObjectsUnchanged=True,
            mainEntryReferences=references,libcInitializesTLSBeforeWrapper=True,image=image,exports=exports,
            nativeHelperCodeUnchanged=True,wrapperEntryHasBTI=True)
        put(OUT/'report.json',report)
        bundle=OUT/'ready';bundle.mkdir();(bundle/'profile').mkdir()
        shutil.copytree(OLD/'retail63-jit105',bundle/'retail63-jit105')
        profile=J(OLD/'profile/report.json');profile.update(image=image,exports=exports,nativeActivity=m['nativeActivity'],nativeFusedOpaqueCopy=m['nativeFusedOpaqueCopy'])
        m.update(sourceNativeProfile=str(bundle/'profile'),linkCommand=cmd)
        m['executionInputs'][str(obj)]=H(obj);m['executionInputCount']=len(m['executionInputs'])
        m['metadataInputs'].update({str(SOURCE):H(SOURCE),str(OUT/'report.json'):H(OUT/'report.json')})
        relink=(OLD/'relink.py').read_text().replace('build/r286-performance-ready-source-v1/retail63-jit105',str((bundle/'retail63-jit105').relative_to(W)))
        (bundle/'relink.py').write_text(relink);m['relinkToolSha256']=H(bundle/'relink.py')
        put(bundle/'profile/report.json',profile);put(bundle/'manifest.json',m)
        put(OUT/'ready-pointer.json',dict(bundle=str(bundle),report=str(OUT/'report.json'),sourceSha256=H(SOURCE),manifestSha256=H(bundle/'manifest.json'),profileSha256=H(bundle/'profile/report.json'),toolSha256=H(bundle/'relink.py')))
        print(json.dumps(dict(passed=True,report=str(OUT/'report.json'),ready=str(OUT/'ready-pointer.json'))),flush=True)
    except BaseException as e:
        report.update(passed=False,error=repr(e));put(OUT/'report.json',report);raise
if __name__=='__main__':main()
