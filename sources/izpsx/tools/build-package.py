"""Create the IZPSX IZM and native SD bundle, without installing either."""
from pathlib import Path
import hashlib,json,shutil,subprocess,re
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/izpsx-sd';HOME=OUT/'module';APP=HOME/'switch';APP.mkdir(parents=True,exist_ok=True)
from local_build import PANEL
VERSION=re.search(r'#define IZPSX_VERSION "([^"]+)"',(ROOT/'src/brand.h').read_text()).group(1)
report=json.loads((OUT/'report.json').read_text());assert report['passed']
sha=lambda b:hashlib.sha256(b).hexdigest()
asset=report['asset'];assert sha((OUT/asset).read_bytes())==report['assetSha256']
for stage in ['vert','frag','comp']:
 out=APP/('app.'+stage+'.spv');subprocess.run(['glslangValidator','-V','--target-env','vulkan1.0',str(ROOT/'shaders'/('display.'+stage)),'-o',str(out)],check=True,capture_output=True);subprocess.run(['spirv-val','--target-env','vulkan1.0',str(out)],check=True)
(APP/'state.bin').write_bytes(bytes(1024));(APP/'data.bin').write_bytes(bytes(4));(APP/'pixels.bin').write_bytes(bytes(4096))
(APP/'native').mkdir(exist_ok=True)
for folder in [APP/'native',OUT/'sd']:
 if folder.exists():
  for old in folder.glob('asset-*.izdata'):
   if old.name!=asset:old.unlink()
for name in ['COPYING','README.txt']:
 old=HOME/name
 if old.exists():old.unlink()
shutil.copyfile(OUT/asset,APP/'native'/asset);shutil.copyfile(OUT/'shim.bin',APP/'native/shim.bin')
shutil.copyfile(ROOT/'assets/brand/izpsx-icon-v1.png',APP/'icon.png')
manifest={'format':'hblauncher.gpu-app','abi':1,'id':'izpsx','name':'IZPSX emulator','version':VERSION,'description':'IZPSX emulator. Up to four players with Multitap and sideways Joy-Con controls, local SD games and persistent memory cards. @byizmysh','transition':'none','hostRuntime':'module:izpsx/launch','category':'game','shaders':{'vertex':'app.vert.spv','fragment':'app.frag.spv','compute':'app.comp.spv'},'initialState':'state.bin','data':'data.bin','icon':'icon.png','memory':{'bytes':8*1024*1024,'initial':'pixels.bin'},'native':{'format':'iz2s.native-app','abi':5,'code':'native/shim.bin','dataBytes':256*1024,'capabilities':['audio-out'],'assets':[{'file':'native/'+asset,'path':asset,'bytes':(OUT/asset).stat().st_size,'sha256':report['assetSha256']}]}}
(APP/'app.json').write_text(json.dumps(manifest,indent=2)+'\n')
descriptor={'format':'izlauncher.module','formatVersion':1,'apiVersion':1,'id':'izpsx','name':'IZPSX emulator','version':VERSION,'author':'@byizmysh','platforms':['linux-x64','linux-arm64'],'requires':{'platformApi':1,'shell':'izlauncher','consoleAbi':1},'switch':{'entry':'switch/app.json','abi':1,'files':['switch/'+p.relative_to(APP).as_posix() for p in sorted(APP.rglob('*')) if p.is_file()]},'dependencies':[],'settingsSchema':[],'license':'GPL-2.0-only'}
descriptor['server']={'entry':'server/entry.cjs','runtime':'node','implementations':{'launch':'modules/izpsx/server/launch.cjs'},'settings':{'entry':'server/settings.cjs','label':'Games and BIOS'}}
shutil.copytree(ROOT/'server',HOME/'server',dirs_exist_ok=True)
(HOME/'module.json').write_text(json.dumps(descriptor,indent=2)+'\n')
shutil.copyfile(ROOT/'vendor/pcsx_rearmed/COPYING',HOME/'LICENSE')
shutil.copyfile(ROOT/'assets/brand/FONT-LICENSE.txt',HOME/'FONT-LICENSE.txt')
(HOME/'README.md').write_text('IZPSX emulator '+VERSION+' — @byizmysh\n\nSD:/IZ2S/Homebrew/Modules/izpsx/\n  launcher.izapp + declared asset-*.izdata\n  games/ — BIN/CUE or PBP, each game in its own directory\n  bios/scph101.bin — your own 512 KiB BIOS\n  saves/ — two shared memory cards, two verified generations each\n\nSource is detected automatically: session RAM or public native SD launcher ABI 5.\nRAM entry needs the local PC launcher; the SD entry reads all game data locally.\nAbout shows the current source. Games/BIOS are not included.\nIn-game memory card saves are supported; save states are not implemented.\nPlus is PS1 Start (game menu). Minus opens the IZPSX menu or library settings.\nChoose Exit IZPSX emulator in the menu to return to the launcher. R3 has no shortcut.\nSettings: L/R changes tabs.\nControls: choose Player 1-4 with ZL/ZR or the player selector.\nEach player has separate device, button swap, stick D-pad, deadzone and turbo settings.\nChoose Off to disconnect game input; Disconnect from Switch requests system disconnection.\nB saves both profiles. X resets one setting; Y resets only the selected player.\nExisting shared and two-player settings migrate automatically.\nMultitap: Off keeps two normal PS1 ports. Port 1 / Port 2 enables up to four players.\nChoose the game again after changing Multitap. The game must support Multitap.\nPair Joy-Con halves individually using SL/SR in HOME before entering SB4.\nSingle Joy-Con: hold sideways. SL/SR = L1/R1; outer shoulders = L2/R2.\nStick click = Select; +/- = Start; SL+SR+/- = IZPSX menu.\nFor paired Joy-Con and Pro controllers: + = Start, - = IZPSX menu.\nDisplay FPS cap preserves native game speed/audio.\nSource and local qualification reports accompany IZPSX emulator.\n')
builder=ROOT/'tools/package-sd.cjs';subprocess.run(['node',str(builder)],cwd=ROOT,check=True)
