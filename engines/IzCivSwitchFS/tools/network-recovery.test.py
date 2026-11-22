"""Exercise the real recovery shell against fake sysfs/NM; never touches host Wi-Fi."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / 'engines/CivSwitchLDN/bash/network-panel.sh'
MOCK = r'''#!/usr/bin/env python3
import json,os,sys,pathlib,subprocess
root=pathlib.Path(os.environ['MOCK_ROOT']); state=root/'nm.json'; v=json.loads(state.read_text()); a=sys.argv[1:]
name=pathlib.Path(sys.argv[0]).name
if name=='helper':
 if a[0]=='-c':
  (root/'sys/class/net'/a[-2]/'flags').write_text('0x1' if a[-1]=='up' else '0x0'); sys.exit(0)
 if a[0].endswith('panel_network.py'):
  import shutil; shutil.rmtree(root/'sys/class/net/iz2s-civ7-ldn',ignore_errors=True); sys.exit(0)
 os.execv(sys.executable,[sys.executable]+a)
if name=='sleep':sys.exit(0)
if a[:2]==['--wait',a[1]] and a[0]=='--wait':a=a[2:]
with (root/'calls').open('a') as f:f.write(' '.join(a)+'\n')
if a[:2]==['radio','wifi']:
 if len(a)>2:v['radio']='enabled'
 else:print(v['radio'])
elif a[0]=='-g':
 key=a[1]; print({'GENERAL.CON-UUID':v['connection'],'GENERAL.NM-MANAGED':v['managed'],'GENERAL.STATE':v['status'],'IP4.ADDRESS':v['ip']}[key])
elif a[:2]==['device','set']:
 v['managed']=a[-1]; v['status']='30 (disconnected)' if a[-1]=='yes' else '10 (unmanaged)'
elif a[:2]==['connection','up']:
 if v.get('fail'):sys.exit(1)
 v['connection']=a[3];v['status']='100 (connected)';v['ip']='192.168.1.2/24'
else:raise Exception(a)
state.write_text(json.dumps(v))
'''

class Recovery(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup); self.root=Path(self.temp.name)
  self.sys=self.root/'sys'; self.device=self.sys/'class/net/wlan0'; self.device.mkdir(parents=True)
  phy=self.sys/'class/ieee80211/phy0';phy.mkdir(parents=True)
  (self.device/'phy80211').symlink_to(phy);(self.device/'address').write_text('02:00:00:00:00:01');(self.device/'flags').write_text('0x1')
  self.bin=self.root/'bin';self.bin.mkdir()
  for name in ['nmcli','helper','sleep']:
   file=self.bin/name;file.write_text(MOCK);file.chmod(0o755)
  self.initial={'connection':'11111111-1111-1111-1111-111111111111','managed':'yes','status':'100 (connected)','ip':'192.168.1.2/24','radio':'enabled'}
  self.write_state(self.initial)
  self.receipt=self.root/'receipt.json'
  self.env={**os.environ,'PATH':str(self.bin)+':'+os.environ['PATH'],'MOCK_ROOT':str(self.root),'IZ2S_NETWORK_SYSFS':str(self.sys),'IZ2S_NETWORK_STATE':str(self.receipt)}
 def write_state(self,value): (self.root/'nm.json').write_text(json.dumps(value))
 def run_script(self,command='exit 0',restore=False):
  args=['--restore',str(self.bin/'helper')] if restore else ['--phy','phy0',str(self.bin/'helper'),'bash','-c',command]
  return subprocess.run(['bash',str(SCRIPT),*args],env=self.env,text=True,capture_output=True,timeout=20)
 def assert_restored(self,result):
  self.assertIn('CIV7_LDN_NETWORK_RESTORED',result.stdout,result.stderr)
  state=json.loads((self.root/'nm.json').read_text());self.assertEqual(state['managed'],'yes');self.assertEqual(state['status'],'100 (connected)')
  self.assertFalse(self.receipt.exists());self.assertNotIn('ethernet',(self.root/'calls').read_text())
 def test_delivery_success_restores_original_profile(self):
  r=self.run_script('test -f "$IZ2S_NETWORK_STATE"');self.assertEqual(r.returncode,0,r.stderr);self.assert_restored(r)
 def test_delivery_failure_still_restores(self):
  r=self.run_script('exit 7');self.assertEqual(r.returncode,7);self.assert_restored(r)
 def test_failed_recovery_keeps_receipt_for_retry(self):
  self.write_state({**self.initial,'fail':True});r=self.run_script();self.assertNotEqual(r.returncode,0);self.assertTrue(self.receipt.exists());self.assertNotIn('CIV7_LDN_NETWORK_RESTORED',r.stdout)
  self.write_state(self.initial);r=self.run_script(restore=True);self.assertEqual(r.returncode,0,r.stderr);self.assert_restored(r)
 def test_renamed_adapter_is_resolved_by_mac(self):
  r=self.run_script('mv "$IZ2S_NETWORK_SYSFS/class/net/wlan0" "$IZ2S_NETWORK_SYSFS/class/net/wlan1"')
  self.assertEqual(r.returncode,0,r.stderr);self.assert_restored(r);self.assertIn('ifname wlan1',(self.root/'calls').read_text())
 def test_missing_adapter_is_not_reported_as_restored(self):
  r=self.run_script('rm -r "$IZ2S_NETWORK_SYSFS/class/net/wlan0"');self.assertNotEqual(r.returncode,0);self.assertTrue(self.receipt.exists());self.assertNotIn('CIV7_LDN_NETWORK_RESTORED',r.stdout)
 def test_leftover_temporary_interface_is_removed_before_restore(self):
  r=self.run_script('mkdir "$IZ2S_NETWORK_SYSFS/class/net/iz2s-civ7-ldn"');self.assertEqual(r.returncode,0,r.stderr);self.assert_restored(r);self.assertFalse((self.device.parent/'iz2s-civ7-ldn').exists())
 def test_disabled_radio_is_enabled_before_delivery(self):
  self.write_state({**self.initial,'radio':'disabled'});r=self.run_script();self.assertEqual(r.returncode,0,r.stderr);self.assert_restored(r);self.assertIn('radio wifi on',(self.root/'calls').read_text())
 def test_no_previous_profile_does_not_force_disconnect(self):
  self.write_state({**self.initial,'connection':'--','status':'30 (disconnected)','ip':''});r=self.run_script();self.assertEqual(r.returncode,0,r.stderr);self.assertNotIn('device disconnect',(self.root/'calls').read_text());self.assertFalse(self.receipt.exists())
 def test_sigterm_restores_before_exit(self):
  r=self.run_script('kill -TERM "$PPID"; exit 0');self.assertEqual(r.returncode,143,r.stderr);self.assert_restored(r)

if __name__=='__main__': unittest.main()
