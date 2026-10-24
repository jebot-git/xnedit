#!/usr/bin/env python3
"""Validate the default Wayland build against an isolated real DomeD compositor."""
import os,subprocess,tempfile,time,signal,json,re,socket,sys,urllib.request
from pathlib import Path
from Xlib import X,XK,display
from Xlib.ext import xtest
from PIL import Image
from remote import Fixture
from spartan import Fixture as SpartanFixture
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('XNEDIT_WL_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'XNEDIT_WL_BUS':'1'})
root=Path(__file__).resolve().parents[1];out=root/'build-fltk/wayland-test';out.mkdir(parents=True,exist_ok=True);children=[];logs=[];checks=[]
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   result=fn()
   if result:return result
  except(OSError,ValueError,KeyError,StopIteration):pass
  time.sleep(.08)
 raise AssertionError('Timed out; see '+str(out))
def check(value,name):assert value,name;checks.append(name);print('PASS',name,flush=True)
def start(args,name,env):
 log=(out/(name+'.log')).open('w');logs.append(log);p=subprocess.Popen(args,env=env,stdout=log,stderr=log);children.append(p);return p
with tempfile.TemporaryDirectory(prefix='xnedit-wayland-')as tmp:
 os.chmod(tmp,0o700);file=Path(tmp)/'native.txt';file.write_text('native Wayland\n')
 rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
 with os.fdopen(rd)as f:ds=':'+f.readline().strip()
 with socket.socket()as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 env={**os.environ,'DISPLAY':ds,'XDG_CURRENT_DESKTOP':'Dome','XDG_RUNTIME_DIR':tmp,'XDG_CONFIG_HOME':tmp+'/config','XDG_CACHE_HOME':tmp+'/cache','XDG_STATE_HOME':tmp+'/state','LIBGL_ALWAYS_SOFTWARE':'1','DOME_START_XR':'0','DOME_LOGIN_SESSION':'0','DOME_APPEARANCE_SERVICE':'0','DOME_RENDERER':'gles','DOME_PREVIEW_PORT':str(port),'LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib'}
 for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','FLTK_BACKEND']:env.pop(key,None)
 try:
  rejected=subprocess.run([str(root/'build-fltk/xnedit'),'--x11'],env=env,capture_output=True,text=True,timeout=5)
  check(rejected.returncode!=0 and 'not built' in rejected.stderr,'default binary rejects the unbuilt X11 backend')
  unavailable=subprocess.run([str(root/'build-fltk/xnedit')],env={**env,'WAYLAND_DISPLAY':'missing-display'},capture_output=True,text=True,timeout=5)
  check(unavailable.returncode!=0 and 'no Wayland connection' in unavailable.stderr,'Wayland absence does not fall back to a usable X11 display')
  compositor=start([os.environ.get('DOME_COMPOSITOR','/usr/local/bin/dome-compositor'),'--backend','nested'],'compositor',env)
  log=wait(lambda:(out/'compositor.log').read_text()if'Dome Wayland socket:'in(out/'compositor.log').read_text()else None)
  env['WAYLAND_DISPLAY']=re.search(r'Dome Wayland socket: (\S+)',log)[1]
  remote_fixture=Fixture(Path(tmp))
  spartan_fixture=SpartanFixture()
  settings=Path(tmp)/'config/xnedit-xr';settings.mkdir(parents=True,exist_ok=True)
  identity_store=Path(tmp)/'penguin-state.json'
  identity_store.write_text(json.dumps({'identities':[{'id':'ui-test','address':'test@localhost','certificate':remote_fixture.client.read_text(),'key':remote_fixture.client_key.read_text()}]}));identity_store.chmod(0o600)
  (settings/'remote.json').write_text(json.dumps({'pins':remote_fixture.pin,'identity':{'penguin_store':str(identity_store),'penguin_id':'ui-test'},'last_url':remote_fixture.url+'/page.gmi'}));(settings/'remote.json').chmod(0o600)
  env['XNEDIT_REMOTE_TEST_URL']=remote_fixture.url+'/page.gmi'
  env['XNEDIT_SPARTAN_TEST_URL']=spartan_fixture.url+'/page.gmi'
  viewport=subprocess.run([str(root/'build-fltk/editor-viewport-test')],env={**env,'XNEDIT_UI_CAPTURE_DIR':str(out),'XDG_STATE_HOME':tmp+'/viewport-state'},capture_output=True,text=True,timeout=30)
  (out/'viewport.log').write_text(viewport.stdout+viewport.stderr)
  remote_fixture.close()
  spartan_fixture.close()
  check(any(event[1]>0 and event[2].startswith(b'# Spartan UI') for event in spartan_fixture.events),'native Spartan editor uploads the edited bytes over plaintext TCP')
  check(any(event[0]=='titan' and event[2].startswith(b'# Remote UI') and event[4] for event in remote_fixture.events),'native remote editor pushes exact content with its selected client identity')
  check(viewport.returncode==0,'editor viewport, language modes, regex helper and find/replace regressions pass on native Wayland')
  for capture in out.glob('*.ppm'):
   Image.open(capture).save(capture.with_suffix('.png'))
  app=start([str(root/'build-fltk/xnedit'),str(file)],'editor',{**env,'WAYLAND_DEBUG':'client'})
  def state():return json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/api/state'))['scene']
  def window():return next((w for w in state()['windows']if w['app_id']=='org.domed.XNEdit'),None)
  w=wait(window);check(True,'default build opens as a native Wayland application in DomeD')
  wait(lambda:'set_address' in (out/'editor.log').read_text());check(True,'native Wayland appmenu protocol publishes the universal menu')
  xd=display.Display(ds);host=wait(lambda:next((w for w in xd.screen().root.query_tree().children if str(w.get_wm_name()or'').startswith('Dome')),None));host.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync()
  urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}/api/action',data=json.dumps({'op':'focus','id':w['id']}).encode(),headers={'Content-Type':'application/json'})).close();time.sleep(.3)
  def key(name,ctrl=False):
   mod=xd.keysym_to_keycode(XK.string_to_keysym('Control_L'));code=xd.keysym_to_keycode(XK.string_to_keysym(name))
   if ctrl:xtest.fake_input(xd,X.KeyPress,mod)
   xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code)
   if ctrl:xtest.fake_input(xd,X.KeyRelease,mod)
   xd.sync();time.sleep(.15)
  key('End',True)
  for c in 'saved':key(c)
  key('s',True);wait(lambda:file.read_text().endswith('saved'));check(True,'native Wayland input edits and saves the document')
  key('f',True);time.sleep(.3)
  g=host.get_geometry();raw=host.get_image(0,0,g.width,g.height,X.ZPixmap,0xffffffff);im=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX');im.save(out/'native-search.png')
  check(len(set(im.getdata()))>20,'DomeD renders the editor and search controls')
  key('Escape');key('q',True);wait(lambda:app.poll()is not None);check(app.returncode==0,'native Wayland editor closes cleanly');xd.close()
 finally:
  if "remote_fixture" in locals():remote_fixture.close()
  if "spartan_fixture" in locals():spartan_fixture.close()
  for child in reversed(children):
   if child.poll()is None:
    child.terminate()
    try:child.wait(timeout=4)
    except subprocess.TimeoutExpired:child.kill();child.wait()
  for log in logs:log.close()
(out/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
