from pathlib import Path
import hashlib,json,os,subprocess
ROOT=Path(__file__).resolve().parents[1];VENDOR=ROOT/'vendor/pcsx_rearmed';OUT=ROOT/'build/arm64';TREE=OUT/'tree';OUT.mkdir(parents=True,exist_ok=True)
from local_build import CC, PREFIX, mirror_link
for p in VENDOR.rglob('*'):
 rel=p.relative_to(VENDOR)
 if '.git'in rel.parts or p.suffix in ['.o','.d','.so','.a']or not p.is_file():continue
 dest=TREE/rel;dest.parent.mkdir(parents=True,exist_ok=True)
 if str(rel)=='include/revision.h':
  if not dest.exists():dest.write_bytes(p.read_bytes())
 else:mirror_link(dest,p)
flags='-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-pic -mcmodel=large -mbranch-protection=bti -mno-outline-atomics -fwrapv'
args=['make','-f','Makefile.libretro','-j4','platform=unix','ARCH=aarch64','DYNAREC=ari64','BASE_ADDR_DYNAMIC=1','STATIC_LINKING=1','HAVE_CHD=0','HAVE_PHYSICAL_CDROM=0','USE_ASYNC_CDROM=0','USE_ASYNC_GPU=0','USE_ASYNC_SPU=0','NDRC_THREAD=0','USE_LIBRETRO_VFS=1','GIT_VERSION=" 7b0e8f8"','CC='+str(CC),'CC_AS='+str(CC),'AR='+PREFIX+'ar','LD='+PREFIX+'ld','CFLAGS_LAST='+flags,'TARGET='+str(OUT/'pcsx.a')]
with(OUT/'build.log').open('w')as log:
 r=subprocess.run(args,cwd=TREE,stdout=log,stderr=subprocess.STDOUT)
if r.returncode:print((OUT/'build.log').read_text()[-7000:]);raise SystemExit(r.returncode)
binary=OUT/'reference';subprocess.run([str(CC),'-O2','-static','-no-pie','-mbranch-protection=bti','-DPS1_STATIC_CORE','-I'+str(VENDOR/'deps/libretro-common/include'),str(ROOT/'tools/host.c'),str(OUT/'pcsx.a'),'-lm','-lpthread','-ldl','-o',str(binary)],check=True)
report={'upstreamCommit':json.loads((ROOT/'vendor/provenance.json').read_text())['commit'],'target':'Linux AArch64 baseline, not Nintendo SDK','dynarec':'Ari64 ARM64','gpu':'NEON','asyncCoreWorkers':False,'staticArchiveSha256':hashlib.sha256((OUT/'pcsx.a').read_bytes()).hexdigest(),'referenceSha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'buildArguments':args}
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
