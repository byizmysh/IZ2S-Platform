#!/usr/bin/env python3
"""List X11/XWayland application clients only; never expose a desktop/root capture."""
import ctypes as C,json,os
x=C.CDLL('libX11.so.6');D=C.c_void_p;W=C.c_ulong
class Attributes(C.Structure):
 _fields_=[('x',C.c_int),('y',C.c_int),('width',C.c_int),('height',C.c_int),('border_width',C.c_int),('depth',C.c_int),('visual',D),('root',W),('class_',C.c_int),('bit_gravity',C.c_int),('win_gravity',C.c_int),('backing_store',C.c_int),('backing_planes',C.c_ulong),('backing_pixel',C.c_ulong),('save_under',C.c_int),('colormap',C.c_ulong),('map_installed',C.c_int),('map_state',C.c_int),('all_event_masks',C.c_long),('your_event_mask',C.c_long),('do_not_propagate_mask',C.c_long),('override_redirect',C.c_int),('screen',D)]
x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=D
x.XDefaultRootWindow.argtypes=[D];x.XDefaultRootWindow.restype=W
x.XInternAtom.argtypes=[D,C.c_char_p,C.c_int];x.XInternAtom.restype=W
x.XQueryTree.argtypes=[D,W,C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(W)),C.POINTER(C.c_uint)]
x.XGetWindowProperty.argtypes=[D,W,W,C.c_long,C.c_long,C.c_int,W,C.POINTER(W),C.POINTER(C.c_int),C.POINTER(W),C.POINTER(W),C.POINTER(C.POINTER(C.c_ubyte))]
x.XGetGeometry.argtypes=[D,W,C.POINTER(W),C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint),C.POINTER(C.c_uint)]
x.XFree.argtypes=[C.c_void_p];x.XCloseDisplay.argtypes=[D]
x.XSync.argtypes=[D,C.c_int];x.XGetInputFocus.argtypes=[D,C.POINTER(W),C.POINTER(C.c_int)]
x.XGetWindowAttributes.argtypes=[D,W,C.POINTER(Attributes)]
x.XStringToKeysym.argtypes=[C.c_char_p];x.XStringToKeysym.restype=C.c_ulong
x.XKeysymToKeycode.argtypes=[D,C.c_ulong];x.XKeysymToKeycode.restype=C.c_uint
ERROR_HANDLER=C.CFUNCTYPE(C.c_int,D,D)
ignore_x_error=ERROR_HANDLER(lambda display,event:0)
x.XSetErrorHandler.argtypes=[ERROR_HANDLER];x.XSetErrorHandler(ignore_x_error)
d=x.XOpenDisplay(None)
if not d:raise SystemExit('The X11/XWayland display is unavailable; use browser capture for native Wayland windows')
try:
 class ClientSpec(C.Structure):
  _fields_=[('client',W),('mask',C.c_uint)]
 class ClientValue(C.Structure):
  _fields_=[('spec',ClientSpec),('length',C.c_long),('value',D)]
 try:res=C.CDLL('libXRes.so.1')
 except OSError:raise SystemExit('Native window capture requires libXRes1. Use Browser capture on this system.')
 res.XResQueryClientIds.argtypes=[D,C.c_long,C.POINTER(ClientSpec),C.POINTER(C.c_long),C.POINTER(C.POINTER(ClientValue))]
 res.XResGetClientPid.argtypes=[C.POINTER(ClientValue)];res.XResGetClientPid.restype=C.c_int
 res.XResClientIdsDestroy.argtypes=[C.c_long,C.POINTER(ClientValue)]
 def client_pid(window):
  spec=ClientSpec(window,2);count=C.c_long();values=C.POINTER(ClientValue)()
  res.XResQueryClientIds(d,1,C.byref(spec),C.byref(count),C.byref(values))
  if not values:return None
  try:
   for i in range(min(count.value,64)):
    pid=res.XResGetClientPid(C.byref(values[i]))
    if pid>1:return pid
  finally:res.XResClientIdsDestroy(count,values)
 def prop(window,name,all32=False):
  atom=x.XInternAtom(d,name.encode(),1)
  if not atom:return None
  typ=W();fmt=C.c_int();count=W();left=W();data=C.POINTER(C.c_ubyte)()
  result=x.XGetWindowProperty(d,window,atom,0,1024,0,0,C.byref(typ),C.byref(fmt),C.byref(count),C.byref(left),C.byref(data))
  if result or not data:return None
  try:
   if fmt.value==32 and count.value:
    values=C.cast(data,C.POINTER(W));return [int(values[i]) for i in range(count.value)] if all32 else int(values[0])
   if fmt.value==8:return C.string_at(data,count.value).decode(errors='replace')
  finally:x.XFree(data)
 def children(w):
  root=W();parent=W();items=C.POINTER(W)();n=C.c_uint()
  if not x.XQueryTree(d,w,C.byref(root),C.byref(parent),C.byref(items),C.byref(n)):return []
  try:return [int(items[i]) for i in range(n.value)]
  finally:
   if items:x.XFree(items)
 root=x.XDefaultRootWindow(d);clients=prop(root,'_NET_CLIENT_LIST_STACKING',True) or prop(root,'_NET_CLIENT_LIST',True) or [];windows=[];queue=clients or children(root);seen=set();managed=bool(clients)
 while queue and len(seen)<16384:
  w=queue.pop()
  if w in seen:continue
  seen.add(w);window_pid=prop(w,'_NET_WM_PID');pid=client_pid(w)
  types=prop(w,'_NET_WM_WINDOW_TYPE',True) or [];normal=x.XInternAtom(d,b'_NET_WM_WINDOW_TYPE_NORMAL',1);dialog=x.XInternAtom(d,b'_NET_WM_WINDOW_TYPE_DIALOG',1)
  if pid and pid>1 and (not types or normal in types or dialog in types):
   title=prop(w,'_NET_WM_NAME') or prop(w,'WM_NAME') or '';root=W();gx=C.c_int();gy=C.c_int();width=C.c_uint();height=C.c_uint();border=C.c_uint();depth=C.c_uint()
   ok=x.XGetGeometry(d,w,C.byref(root),C.byref(gx),C.byref(gy),C.byref(width),C.byref(height),C.byref(border),C.byref(depth))
   if ok and width.value>=4 and height.value>=4 and title and width.value<=16384 and height.value<=16384:
    attributes=Attributes();has_attributes=bool(x.XGetWindowAttributes(d,w,C.byref(attributes)));states=prop(w,'_NET_WM_STATE',True) or [];hidden_atom=x.XInternAtom(d,b'_NET_WM_STATE_HIDDEN',1);minimized=prop(w,'WM_STATE')==3 or bool(hidden_atom and hidden_atom in states);mapped=has_attributes and attributes.map_state==2
    focus=W();revert=C.c_int();x.XGetInputFocus(d,C.byref(focus),C.byref(revert))
    windows.append({'id':w,'pid':pid,'windowPid':window_pid,'pidSource':'xres','title':title,'width':width.value,'height':height.value,'display':os.environ.get('DISPLAY',''),'mapped':mapped,'mapState':attributes.map_state if has_attributes else None,'minimized':minimized,'hidden':not mapped or minimized,'focused':focus.value==w})
  if not managed:queue.extend(children(w))
 windows.sort(key=lambda w:w['title'].lower());print(json.dumps({'windows':windows,'coverage':'X11 / XWayland; native Wayland windows require browser capture'}))
finally:x.XCloseDisplay(d)
