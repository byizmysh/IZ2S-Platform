"""Local or trusted-SSH transport for the packaged IZ2S input agent."""
import base64, ipaddress, json, os, subprocess, sys
from pathlib import Path
mode, action = sys.argv[1:3]
request = json.load(sys.stdin)
root = Path(request['root'])
if mode == 'ssh':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from ssh_transport import SSH
    ipaddress.IPv4Address(request['armAddress'])
    peer = SSH + [(os.environ.get('IZ2S_ARM_USER') or os.environ.get('IZ15_ARM_USER', 'root')) + '@' + request['armAddress']]
else:
    peer = []
program = '''import base64,hashlib,json,subprocess,sys
from pathlib import Path
r=json.loads(base64.b64decode(%r))
folder=Path(r['folder'])
action=%r
if action=='ensure':
 scope={'__name__':'iz2s_installer'}
 exec(compile(base64.b64decode(r['files']['install.py']),'iz2s-input-install','exec'),scope)
 scope['install'](folder,r['files'],r['hidDevice'])
 action='status'
package={}
if action not in ('halt','follow'):
 error=None
 try:
  package=json.loads((folder/'package.json').read_text())
  if package.get('format')!='iz2s.input-agent' or package.get('id')!='arm-input' or type(package.get('apiVersion')) is not int or package['apiVersion']!=1:
   error='Incompatible ARM input protocol. This panel requires input API 1; install a compatible input package explicitly.'
  else:
   # Verify the ARM installation against its own inventory, never the PC copy.
   checks=json.loads((folder/'checksums.json').read_text())
   for name in ('agent.py','type-bootstrap.py'):
    if checks.get('algorithm')!='sha256' or hashlib.sha256((folder/name).read_bytes()).hexdigest()!=checks['files'][name]:
     error='ARM input installation is damaged. Reinstall the input package using First-time setup.'
 except (OSError,ValueError,KeyError,TypeError):
  error='Install the ARM input package using First-time setup.'
 if error:
  if action=='status':
   print(json.dumps({'reachable':True,'moduleReady':False,'keyboard':False,'setupRequired':True,'inputApiVersion':package.get('apiVersion'),'error':error}));raise SystemExit(0)
  raise RuntimeError(error)
command=['python3',str(folder/'agent.py'),action]
body=None if action=='status' else json.dumps(r.get('body') or {})
if action=='follow':command.append(r['body']['requestId']);body=None
if action=='status':
 result=subprocess.run(command,input=body,text=True,capture_output=True)
 if result.returncode:
  sys.stderr.write(result.stderr);raise SystemExit(result.returncode)
 status=json.loads(result.stdout)
 status.update({'inputApiVersion':package['apiVersion'],'inputVersion':package.get('version'),'protocolCompatible':True})
 print(json.dumps(status))
else:
 result=subprocess.run(command,input=body,text=True)
raise SystemExit(result.returncode)
''' % (base64.b64encode(json.dumps(request).encode()).decode(), action)
process = subprocess.Popen(peer+['python3','-'], stdin=subprocess.PIPE)
try:
    process.communicate(program.encode())
    raise SystemExit(process.returncode)
finally:
    if process.poll() is None: process.terminate()
