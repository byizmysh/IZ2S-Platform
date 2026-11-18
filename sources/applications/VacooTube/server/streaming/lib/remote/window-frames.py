#!/usr/bin/env python3
"""One-shot KDE geometry; optional activation of one explicitly selected window."""
import dbus,dbus.service,json,os,tempfile,re,sys,xml.etree.ElementTree as ET
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib
target=None
if len(sys.argv)>1:
 if len(sys.argv)!=3 or sys.argv[1]!='--focus' or not re.fullmatch(r'\{?[a-fA-F0-9-]{36}\}?',sys.argv[2]):raise SystemExit('Invalid captured window identifier')
 target=sys.argv[2]
DBusGMainLoop(set_as_default=True)
bus=dbus.SessionBus();service='org.izmysh.PCScreen.p'+str(os.getpid());name=dbus.service.BusName(service,bus);loop=GLib.MainLoop();result=[]
class Frames(dbus.service.Object):
 @dbus.service.method('org.izmysh.PCScreen',in_signature='s',out_signature='')
 def Update(self,text):
  global result
  result=json.loads(str(text));loop.quit()
receiver=Frames(bus,'/Frames');tag='iz-pc-screen-frames-'+str(os.getpid())
script='''const r=x=>({x:x.x,y:x.y,w:x.width,h:x.height});const list=workspace.windowList().filter(w=>w.normalWindow&&w.caption).map(w=>({id:String(w.internalId),title:String(w.caption),pid:Number(w.pid),frame:r(w.frameGeometry),client:r(w.clientGeometry)}));callDBus(SERVICE,"/Frames","org.izmysh.PCScreen","Update",JSON.stringify(list));'''.replace('SERVICE',json.dumps(service))
if target:
 script='''const wanted=TARGET;const w=workspace.windowList().find(w=>w.normalWindow&&String(w.internalId)===wanted);let result;if(!w){result={focused:false,error:"The captured game window is no longer available"};}else{workspace.activeWindow=w;const ok=workspace.activeWindow===w;result={focused:ok,title:String(w.caption),error:ok?null:"The game window could not be activated"};}callDBus(SERVICE,"/Frames","org.izmysh.PCScreen","Update",JSON.stringify(result));'''.replace('TARGET',json.dumps(target)).replace('SERVICE',json.dumps(service))
f=tempfile.NamedTemporaryFile(mode='w',suffix='.js',prefix='iz-window-frames-',delete=False);f.write(script);f.close();manager=dbus.Interface(bus.get_object('org.kde.KWin','/Scripting'),'org.kde.kwin.Scripting')
try:
 ident=int(bus.call_blocking('org.kde.KWin','/Scripting','org.kde.kwin.Scripting','loadScript','ss',(f.name,tag)));paths=[]
 for parent in ['/Scripting','/']:
  xml=dbus.Interface(bus.get_object('org.kde.KWin',parent),'org.freedesktop.DBus.Introspectable').Introspect()
  for n in ET.fromstring(str(xml)).findall('node'):
   child=n.attrib['name']
   if child in [str(ident),'Script'+str(ident)]:paths.append(parent.rstrip('/')+'/'+child)
 if not paths:raise RuntimeError('KDE script endpoint unavailable')
 dbus.Interface(bus.get_object('org.kde.KWin',paths[0]),'org.kde.kwin.Script').run()
 GLib.timeout_add(1200,lambda:(loop.quit(),False)[1]);loop.run();print(json.dumps(result))
finally:
 try:manager.unloadScript(tag)
 except Exception:pass
 os.unlink(f.name)
