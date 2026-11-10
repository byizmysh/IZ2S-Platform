"""IZ2S local USB jobs, shared by SSH and ARM-only modes. No host server over SSH."""
import fcntl,importlib.util,json,os,platform,re,select,signal,subprocess,sys,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from gamepad import Gamepad
import usb_gadget
JOBS=Path('/run/ns2jb-keyboard-jobs')
def identifier(value):
 if not isinstance(value,str) or not re.fullmatch(r'[a-f0-9-]{36}',value):raise ValueError('Invalid input job identifier')
 return value
def configuration():
 try:return json.loads((HERE/'input-config.json').read_text())
 except FileNotFoundError:return {}
def device():return configuration().get('hidDevice','/dev/hidg0')
def controller():return configuration().get('gamepadDevice')
def controller_ready():
 try:
  value=usb_gadget.config_paths(HERE)
  return value.get('usbGadget')==usb_gadget.NAME and bool(usb_gadget.read(usb_gadget.ROOT/usb_gadget.NAME/'UDC')) and usb()
 except (OSError,ValueError,KeyError,RuntimeError):return False
def usb():return Path(device()).exists() and any(p.read_text().strip()=='configured' for p in Path('/sys/class/udc').glob('*/state'))
def busy():
 for file in Path('/run').glob('ns2jb-keyboard-jobs*/current.json'):
  try:
   value=json.loads(file.read_text()).get('requestId','')
   if value and subprocess.run(['systemctl','is-active','--quiet','ns2jb-hid-'+value],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:return True
  except (OSError,ValueError):pass
 return False
def sender():
 spec=importlib.util.spec_from_file_location('protected_hid',HERE/'type-bootstrap.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def status():return {'reachable':True,'moduleReady':os.geteuid()==0 and Path('/run/systemd/system').is_dir() and all((HERE/n).is_file() for n in ['type-bootstrap.py']),'keyboard':usb(),'keyboardBusy':busy(),'usbState':', '.join(p.read_text().strip() for p in Path('/sys/class/udc').glob('*/state')) or 'not attached','architecture':platform.machine(),'service':'izlauncher-input','installed':True,'rawTextInput':True,'sourceLineSelection':True,'boundedLineInput':True,'batchRawLines':True,'gamepadInput':controller_ready(),'gamepadReady':controller_ready()}
def enqueue(body):
 key=identifier(body.get('requestId'));job=JOBS/key
 if body.get('action')!='plan':raise ValueError('Input jobs require an engine-provided plan')
 if body.get('action')=='connect' and not re.fullmatch(r'V=START\([0-9,]+\)',body.get('command','')):raise ValueError('Invalid generated connection command')
 if body.get('bootstrap','NS2') not in ['NS2','NS2R']:raise ValueError('Unsupported bootstrap')
 JOBS.mkdir(mode=0o700,exist_ok=True)
 with (JOBS/'lock').open('w') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  if job.exists():return {'accepted':True,'duplicate':True}
  if not usb():raise RuntimeError('USB input cable is not configured')
  if busy():raise RuntimeError('USB input device is busy')
  job.mkdir(mode=0o700);file=job/'request.json';file.write_text(json.dumps(body));file.chmod(0o600);(JOBS/'current.json').write_text(json.dumps({'requestId':key}))
  try:subprocess.run(['systemd-run','--quiet','--collect','--service-type=exec','--unit=ns2jb-hid-'+key,'--property=KillMode=control-group','--property=RuntimeMaxSec=7200','python3',str(HERE/'agent.py'),'execute',key],check=True,capture_output=True)
  except Exception:
   (job/'done.json').write_text(json.dumps({'ok':False,'error':'Input task did not start'}));raise
 return {'accepted':True}
def typed_plan(body,report):
 through=body.get('sourceThrough')
 if through is None:return typed_line(body,report)
 start=body.get('sourceLine');lines=body.get('source','').splitlines()
 if body.get('sourceMode')!='raw-lines' or type(start) is not int or type(through) is not int or not 0<=start<=through<len(lines):raise ValueError('Invalid stored line range')
 for index in range(start,through+1):
  typed_line({**body,'sourceLine':index},report)
  report('Stored line committed',line=index+1,total=len(lines),committedLine=index+1)
def typed_line(body,report):
 pad=None
 if any(step.get('type')=='gamepad' for step in body.get('steps',[])):
  if not controller_ready():raise RuntimeError('Prepare ARM USB keyboard + controller before automatic input')
  pad=Gamepad(controller())
 module=sender();special={**module.SPECIAL,'up':82,'down':81,'tab':43};keyboard=None
 try:
  keyboard=module.Keyboard(device(),.06,.08)
  # Observe the actual USB LED report; a silent host never receives a plan.
  for _ in range(2):keyboard.isolated_tap(0,special['capslock']);time.sleep(.4)
  leds=None
  while select.select([keyboard.fd],[],[],.2)[0]:leds=os.read(keyboard.fd,64)
  if not leds:raise RuntimeError('Console keyboard did not answer; no plan was typed')
  caps=bool(leds[0]&2)
  def text(value):
   for ch in value:
    if pad:pad.check()
    key=ch.swapcase() if caps and ch.isalpha() else ch
    keyboard.isolated_tap(*module.KEYS[key])
  def source(value):
   lines=value.replace('\r\n','\n').replace('\r','\n').splitlines()
   if body.get('sourceMode')=='raw-lines':
    index=body.get('sourceLine')
    if not isinstance(index,int) or isinstance(index,bool) or not 0<=index<len(lines):raise ValueError('Invalid raw source line selection')
    text(lines[index]);report('Entered selected text line',line=index+1,total=len(lines));return
   for number,line in enumerate(lines,1):
    keyboard.isolated_tap(module.SHIFT,special['home']);keyboard.isolated_tap(0,special['delete']);text(line)
    if line.strip():keyboard.isolated_tap(0,special['escape']);time.sleep(.2)
    keyboard.isolated_tap(0,special['enter']);time.sleep(.3)
    if line.strip().upper()=='END':time.sleep(.8)
    report('Entered source line',line=number,total=len(lines))
  steps=body['steps']
  for at in range(len(steps)+1):
   if body.get('source') is not None and at==body.get('sourceAfter',len(steps)):source(body['source'])
   if at==len(steps):break
   step=steps[at]
   when=step.get('when')
   if when:
    if body.get('sourceMode')!='raw-lines' or when not in ('first-line','not-last-line'):raise ValueError('Invalid conditional input step')
    index=body['sourceLine'];total=len(body['source'].splitlines())
    if when=='first-line' and index!=0 or when=='not-last-line' and index>=total-1:continue
   if pad:pad.check()
   if step['type']=='gamepad':pad.tap(step['button'],step.get('holdMs',50));time.sleep(step.get('delayMs',500)/1000)
   elif step['type']=='line':
    report('Typing generated connection command' if step['text'].startswith('V=START(') else 'Typing plan line',step=at+1,total=len(steps));text(step['text']);keyboard.isolated_tap(0,special['escape']);time.sleep(.5);keyboard.isolated_tap(0,special['enter']);time.sleep(step.get('delayMs',500)/1000)
   elif step['type']=='text':text(step['text']);time.sleep(step.get('delayMs',500)/1000)
   elif step['type']=='key':keyboard.isolated_tap(0,special[step['key']]);time.sleep(step.get('delayMs',500)/1000)
   elif step['type']=='sleep':time.sleep(step['ms']/1000)
   else:raise ValueError('Unsupported typed plan step')
 finally:
  try:
   if keyboard:keyboard.close()
  finally:
   if pad:pad.close()
def execute(key):
 job=JOBS/identifier(key);file=job/'request.json';body=json.loads(file.read_text());file.unlink()
 def report(detail,**fields):
  with (job/'events.jsonl').open('a') as out:out.write(json.dumps({'detail':detail,'fields':fields})+'\n')
 def interrupted(signum,frame):raise KeyboardInterrupt('Input job cancelled')
 signal.signal(signal.SIGTERM,interrupted)
 try:
  if not usb():raise RuntimeError('USB input cable is not configured')
  if body['action']!='plan':raise ValueError('Input jobs require an engine-provided plan')
  typed_plan(body,report)
  result={'ok':True}
 except BaseException as error:result={'ok':False,'error':str(error) or 'Input job interrupted'}
 (job/'done.json').write_text(json.dumps(result))
def follow(key):
 job=JOBS/identifier(key);position=0;deadline=time.monotonic()+7250
 while time.monotonic()<deadline:
  lines=(job/'events.jsonl').read_text().splitlines() if (job/'events.jsonl').exists() else []
  for line in lines[position:]:print(line,flush=True)
  position=len(lines)
  if (job/'done.json').exists():result=json.loads((job/'done.json').read_text());print(json.dumps({'done':result}),flush=True);return
  time.sleep(.2)
 raise RuntimeError('Input job completion is unconfirmed')
def halt(body):
 key=identifier(body.get('requestId'));job=JOBS/key;JOBS.mkdir(mode=0o700,exist_ok=True)
 with (JOBS/'lock').open('w') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX);existed=job.exists();job.mkdir(mode=0o700,exist_ok=True)
  if existed:
   unit='ns2jb-hid-'+key;subprocess.run(['systemctl','stop',unit],capture_output=True,timeout=10)
   result=subprocess.run(['systemctl','show',unit,'--property=ActiveState','--property=MainPID'],capture_output=True,text=True,timeout=3);values=dict(line.split('=',1) for line in result.stdout.splitlines() if '=' in line)
   if values.get('ActiveState') not in ['inactive','failed'] or values.get('MainPID')!='0':raise RuntimeError('Input cancellation is unconfirmed')
  if existed and usb():
   module=sender();keyboard=module.Keyboard(device(),.06,.08)
   try:keyboard.write(module.RELEASE,timeout=.3)
   finally:keyboard.close()
   if controller_ready():
    pad=Gamepad(controller())
    pad.close()
  (job/'request.json').unlink(missing_ok=True);(job/'done.json').write_text(json.dumps({'ok':False,'stopped':True}))
 return {'stopped':True}
if __name__=='__main__':
 try:
  action=sys.argv[1]
  if action=='status':print(json.dumps(status()),flush=True)
  elif action=='enqueue':print(json.dumps(enqueue(json.load(sys.stdin))),flush=True)
  elif action=='execute':execute(sys.argv[2])
  elif action=='follow':follow(sys.argv[2])
  elif action=='halt':print(json.dumps(halt(json.load(sys.stdin))),flush=True)
  else:raise ValueError('Unknown input service action')
 except Exception as error:print(str(error),file=sys.stderr);raise SystemExit(1)
