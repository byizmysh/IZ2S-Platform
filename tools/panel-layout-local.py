"""Exercise the real panel in isolated Chromium against saved local state."""
import base64, hashlib, json, os, pathlib, signal, socket, struct, subprocess, sys, time, urllib.request, urllib.parse

ROOT=pathlib.Path(__file__).resolve().parents[1]
PHASE=sys.argv[1] if len(sys.argv)>1 else 'after'
OUT=ROOT/'data/diagnostics/panel-layout'/PHASE
OUT.mkdir(parents=True,exist_ok=True)

class CDP:
    def __init__(self,url):
        u=urllib.parse.urlsplit(url);self.s=socket.create_connection((u.hostname,u.port),timeout=15);self.n=0
        key=base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f'GET {u.path} HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n'.encode())
        header=b''
        while not header.endswith(b'\r\n\r\n'):header+=self.s.recv(1)
        assert header.startswith(b'HTTP/1.1 101 '),header
    def exact(self,n):
        value=b''
        while len(value)<n:
            part=self.s.recv(n-len(value))
            if not part:raise EOFError('Chromium closed its debugging socket')
            value+=part
        return value
    def call(self,method,params=None):
        self.n+=1;data=json.dumps({'id':self.n,'method':method,'params':params or {}}).encode();n=len(data);mask=os.urandom(4)
        header=bytes([129,128|n]) if n<126 else bytes([129,254])+struct.pack('!H',n) if n<65536 else bytes([129,255])+struct.pack('!Q',n)
        self.s.sendall(header+mask+bytes(b^mask[i%4] for i,b in enumerate(data)))
        while True:
            a,b=self.exact(2);length=b&127
            if length==126:length=struct.unpack('!H',self.exact(2))[0]
            if length==127:length=struct.unpack('!Q',self.exact(8))[0]
            body=self.exact(length)
            if a&15==8:raise EOFError('Chromium closed the WebSocket')
            if a&15!=1:continue
            result=json.loads(body)
            if result.get('id')==self.n:
                if 'error' in result:raise RuntimeError(result['error'])
                return result['result']
    def evaluate(self,expression):
        r=self.call('Runtime.evaluate',{'expression':expression,'awaitPromise':True,'returnByValue':True})
        if 'exceptionDetails' in r:raise RuntimeError(r['exceptionDetails'])
        return r.get('result',{}).get('value')

AUDIT=r'''(()=>{
 const visible=e=>e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden';
 const rect=e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}};
 const controls=[...document.querySelectorAll('#panel-main button,#panel-main summary,#panel-main select')].filter(visible);
 const overflow=[...document.querySelectorAll('#panel-main *')].filter(visible).filter(e=>{const r=e.getBoundingClientRect();return r.right>innerWidth+1||r.left< -1}).map(e=>({tag:e.tagName,id:e.id,class:e.className?.baseVal??e.className,r:rect(e)}));
 const clipped=controls.filter(e=>e.scrollWidth>e.clientWidth+2||e.scrollHeight>e.clientHeight+2).map(e=>({id:e.id,class:e.className,text:e.innerText?.slice(0,100),scroll:[e.scrollWidth,e.scrollHeight],client:[e.clientWidth,e.clientHeight]}));
 const icons=[...document.querySelectorAll('.transport-option svg,.fixed-transport>svg')].filter(visible).map(e=>{const p=e.closest('.transport-option,.fixed-transport'),a=e.getBoundingClientRect(),b=p.getBoundingClientRect();return {parent:p.className,dx:Math.round((a.x+a.width/2-b.x-b.width/2)*100)/100,dy:Math.round((a.y+a.height/2-b.y-b.height/2)*100)/100,w:a.width,h:a.height}});
 const cards=[...document.querySelectorAll('#devices .device')].filter(visible).map(e=>({id:e.id,r:rect(e),icon:rect(e.querySelector('.device-heading>svg')),title:rect(e.querySelector('.device-picker,h3:not([hidden])')),transport:rect(e.querySelector('.transport-choice,.fixed-transport'))}));
 return {viewport:innerWidth,documentWidth:document.documentElement.scrollWidth,overflow,clipped,icons,cards,buttons:controls.map(e=>({id:e.id,text:e.innerText?.slice(0,70),r:rect(e)}))};
})()'''

server=None;chrome=None;log=None
try:
 server=subprocess.Popen(['node','data/build-cache/panel-layout-preview.cjs'],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
 url=server.stdout.readline().strip();assert url.startswith('http://127.0.0.1:'),url
 profile=ROOT/'data/build-cache'/('panel-layout-chromium-'+PHASE)
 profile.mkdir(exist_ok=True)
 portfile=profile/'DevToolsActivePort'
 if portfile.exists():portfile.unlink()
 log=open(OUT/'chromium.log','w')
 chrome=subprocess.Popen(['chromium','--headless','--no-sandbox','--disable-gpu','--disable-background-networking','--disable-component-update','--disable-default-apps','--no-first-run','--disable-sync','--hide-scrollbars','--remote-debugging-port=0',f'--user-data-dir={profile}','--host-resolver-rules=MAP * ~NOTFOUND, EXCLUDE localhost, EXCLUDE 127.0.0.1','about:blank'],stdout=log,stderr=log,start_new_session=True)
 for _ in range(100):
  if portfile.exists():break
  time.sleep(.1)
 port=portfile.read_text().splitlines()[0]
 req=urllib.request.Request(f'http://127.0.0.1:{port}/json/new?'+urllib.parse.quote(url,safe=''),method='PUT')
 target=json.load(urllib.request.urlopen(req));cdp=CDP(target['webSocketDebuggerUrl'])
 cdp.call('Page.enable')
 for _ in range(80):
  if cdp.evaluate("!!window.RUN21_LANGUAGE&&!document.getElementById('panel-main').hidden&&document.querySelectorAll('#devices .device').length>0"):break
  time.sleep(.1)
 else:raise RuntimeError('Layout fixture did not render')
 results=[]
 cases=[(1440,'setup','offline','ru'),(1100,'setup','offline','ru'),(820,'setup','offline','ru'),(600,'setup','offline','ru'),(420,'setup','offline','ru'),(360,'setup','offline','es'),(320,'setup','offline','en'),(1440,'apps','online','ru'),(820,'apps','online','ru'),(420,'apps','online','ru'),(360,'apps','online','es'),(320,'apps','online','zh'),(420,'app-details','online','ru'),(420,'history','online','ru'),(360,'history','online','es'),(320,'history','online','en'),(1440,'settings','offline','ru'),(420,'settings','offline','ru'),(360,'idle','idle','es')]
 for width,view,mode,lang in cases:
  urllib.request.urlopen(url.split('/#')[0]+'/__scenario?mode='+mode).read()
  cdp.call('Emulation.setDeviceMetricsOverride',{'width':width,'height':1000,'deviceScaleFactor':1,'mobile':False})
  marker=f'{view}-{width}-{lang}'
  init=cdp.call('Page.addScriptToEvaluateOnNewDocument',{'source':'window.__layoutCase='+json.dumps(marker)})['identifier']
  cdp.call('Page.navigate',{'url':url.split('/#')[0]+'/?layout-case='+urllib.parse.quote(marker)+'#local-layout-preview'})
  for _ in range(80):
   try:
    if cdp.evaluate("window.__layoutCase==="+json.dumps(marker)+"&&!!window.RUN21_LANGUAGE&&!document.getElementById('panel-main').hidden&&document.querySelectorAll('#devices .device').length>0"):break
   except RuntimeError:pass
   time.sleep(.1)
  else:raise RuntimeError('Layout fixture did not render after navigation')
  cdp.call('Page.removeScriptToEvaluateOnNewDocument',{'identifier':init})
  page='setup' if view in ('settings','idle') else 'apps' if view=='app-details' else view
  cdp.evaluate(f"(async()=>{{await RUN21_LANGUAGE.setLanguage('{lang}',{{persist:false}});document.querySelector('.sidebar [data-view=\"{page}\"]').click();if('{view}'==='settings'&&document.getElementById('session-settings-toggle').getAttribute('aria-expanded')!=='true')document.getElementById('session-settings-toggle').click();if('{view}'==='app-details')document.querySelectorAll('#apps .app-more,#apps .storage-more').forEach((e,i)=>{{if(i<4)e.open=true;}});window.scrollTo(0,0);await new Promise(r=>setTimeout(r,200));}})()")
  name=f'{view}-{width}-{lang}'
  audit=cdp.evaluate(AUDIT);audit.update({'name':name,'mode':mode,'language':lang});results.append(audit)
  picture=cdp.call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
  (OUT/(name+'.png')).write_bytes(base64.b64decode(picture['data']))
  if view=='setup' and width in (420,360):
   cdp.evaluate("document.getElementById('device-toolbar').scrollIntoView({block:'start'})")
   picture=cdp.call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
   (OUT/(name+'-actions.png')).write_bytes(base64.b64decode(picture['data']))
  if view in ('idle','settings'):
   height=min(5000,cdp.evaluate('document.documentElement.scrollHeight'))
   picture=cdp.call('Page.captureScreenshot',{'format':'png','captureBeyondViewport':True,'clip':{'x':0,'y':0,'width':width,'height':height,'scale':1}})
   (OUT/(name+'-full.png')).write_bytes(base64.b64decode(picture['data']))
  print(name,'overflow',len(audit['overflow']),'clipped',len(audit['clipped']),flush=True)
 report={'phase':PHASE,'consoleCommands':0,'stylesheetSha256':hashlib.sha256((ROOT/'panel/ui/style.css').read_bytes()).hexdigest(),'cases':results}
 (OUT/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
finally:
 if chrome:
  os.killpg(chrome.pid,signal.SIGTERM)
  try:chrome.wait(timeout=10)
  except subprocess.TimeoutExpired:os.killpg(chrome.pid,signal.SIGKILL);chrome.wait()
 if server:
  server.terminate()
  try:server.wait(timeout=10)
  except subprocess.TimeoutExpired:server.kill();server.wait()
 if log:log.close()
