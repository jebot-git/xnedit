#!/usr/bin/env python3
"""Drive native editing, save/undo and menu fallback on a private bus/display."""
import asyncio,json,os,re,signal,subprocess,sys,tempfile,time
from pathlib import Path
if '--registrar' in sys.argv:
 from dbus_next.aio import MessageBus
 async def hold():
  bus=await MessageBus().connect();await bus.request_name('com.canonical.AppMenu.Registrar');print('ready',flush=True);await asyncio.Future()
 asyncio.run(hold());sys.exit()
from Xlib import X,XK,display
from Xlib.ext import xtest
from PIL import Image
signal.pthread_sigmask(signal.SIG_SETMASK,[])
if os.environ.get('XNEDIT_TEST_BUS')!='1':os.execvpe('dbus-run-session',['dbus-run-session','--',sys.executable,*sys.argv],{**os.environ,'XNEDIT_TEST_BUS':'1'})
root=Path(__file__).resolve().parents[1];out=root/'build-fltk/test-artifacts';out.mkdir(parents=True,exist_ok=True);children=[];checks=[]
def wait(fn,seconds=15):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:
   result=fn()
   if result:return result
  except (OSError,ValueError):pass
  time.sleep(.08)
 raise AssertionError('Timed out; see '+str(out))
def check(value,name):
 assert value,name
 checks.append(name);print('PASS',name,flush=True)
def stop(p):
 if p.poll() is None:
  p.terminate()
  try:p.wait(timeout=4)
  except subprocess.TimeoutExpired:p.kill();p.wait()
with tempfile.TemporaryDirectory(prefix='xnedit-ui-')as tmp:
 tmp=Path(tmp);first=tmp/'first.txt';first.write_text('hello žluťoučký\n');second=tmp/'second.py';second.write_text('print("second")\n')
 rd,wr=os.pipe();xvfb=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1600x1000x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL);children.append(xvfb);os.close(wr)
 with os.fdopen(rd)as f:ds=':'+f.readline().strip()
 env={**os.environ,'DISPLAY':ds,'FLTK_BACKEND':'x11','XDG_CURRENT_DESKTOP':'Dome','XDG_CONFIG_HOME':str(tmp/'config'),'XDG_CACHE_HOME':str(tmp/'cache'),'XDG_STATE_HOME':str(tmp/'state'),'LIBGL_ALWAYS_SOFTWARE':'1','LD_LIBRARY_PATH':'/opt/fltk-vr/toolkit/lib'};env.pop('WAYLAND_DISPLAY',None)
 xd=display.Display(ds)
 def windows():return xd.screen().root.query_tree().children
 def win():return next((w for w in windows()if 'XNEdit XR' in str(w.get_wm_name()or'')),None)
 def key(w,name,ctrl=False,shift=False):
  if name=='.':name='period'
  w.set_input_focus(X.RevertToParent,X.CurrentTime);xd.sync();mods=[]
  if ctrl:mods.append('Control_L')
  if shift:mods.append('Shift_L')
  for mod in mods:xtest.fake_input(xd,X.KeyPress,xd.keysym_to_keycode(XK.string_to_keysym(mod)))
  code=xd.keysym_to_keycode(XK.string_to_keysym(name));xtest.fake_input(xd,X.KeyPress,code);xtest.fake_input(xd,X.KeyRelease,code)
  for mod in reversed(mods):xtest.fake_input(xd,X.KeyRelease,xd.keysym_to_keycode(XK.string_to_keysym(mod)))
  xd.sync();time.sleep(.12)
 def shot(w,name):
  g=w.get_geometry();raw=xd.screen().root.get_image(g.x,g.y,g.width,g.height,X.ZPixmap,0xffffffff);im=Image.frombytes('RGB',(g.width,g.height),raw.data,'raw','BGRX');im.save(out/(name+'.png'));return im
 def menu(w,method,*args):
  service=w.get_full_property(xd.intern_atom('_KDE_NET_WM_APPMENU_SERVICE_NAME'),X.AnyPropertyType);path=w.get_full_property(xd.intern_atom('_KDE_NET_WM_APPMENU_OBJECT_PATH'),X.AnyPropertyType)
  return subprocess.check_output(['gdbus','call','--session','--dest',bytes(service.value).decode(),'--object-path',bytes(path.value).decode(),'--method','com.canonical.dbusmenu.'+method,'--',*args],text=True)
 def command(w,name):
  layout=menu(w,'GetLayout','0','-1','[]');m=re.search(r"\((\d+), \{'label': <'"+re.escape(name)+"'>",layout);assert m,name;menu(w,'Event',m[1],'clicked','<0>','0')
 log=(out/'editor.log').open('w')
 try:
  app=subprocess.Popen([os.environ.get('XNEDIT_TEST_BINARY',str(root/'build-fltk/xnedit')),str(first)],env=env,stdout=log,stderr=log);children.append(app);w=wait(win);time.sleep(.8)
  check(app.poll()is None,'native FLTK editor opens a UTF-8 document')
  initial=shot(w,'initial');check(len(set(initial.crop((0,2,420,22)).getdata()))>3,'local menu remains available without a menu host')
  key(w,'End',True)
  for c in 'added':key(w,c)
  key(w,'s',True);wait(lambda:first.read_text().endswith('added'));check(True,'typed changes save through the native document model')
  key(w,'z',True);key(w,'s',True);wait(lambda:first.read_text().endswith('adde'));check(True,'Ctrl+Z invokes upstream-backed undo')
  key(w,'z',True,True);key(w,'s',True);wait(lambda:first.read_text().endswith('added'));check(True,'Ctrl+Shift+Z restores the edit')
  host=subprocess.Popen([sys.executable,__file__,'--registrar'],stdout=subprocess.PIPE,text=True);children.append(host);assert host.stdout.readline().strip()=='ready';time.sleep(.8)
  image=shot(w,'universal');check(image.crop((0,0,170,40)).tobytes()==initial.crop((0,30,170,70)).tobytes(),'Dome menu host collapses the local bar')
  check('Find...' in menu(w,'GetLayout','0','-1','[]'),'universal menu exports actual editor commands')
  command(w,'Split view');time.sleep(.3);shot(w,'split')
  key(w,'End',True);key(w,'exclam',shift=True);key(w,'s',True);wait(lambda:first.read_text().endswith('added!'));check(True,'keyboard editing and saving survive universal-menu and split-view transitions')
  def click(x,y):
   g=w.get_geometry();xtest.fake_input(xd,X.MotionNotify,x=g.x+x,y=g.y+y);xd.sync();time.sleep(.05)
   for event in [X.ButtonPress,X.ButtonRelease]:xtest.fake_input(xd,event,1);xd.sync();time.sleep(.05)
  def field(x,y,value):
   click(x,y);key(w,'a',True)
   for c in value:key(w,c)
  command(w,'Replace...');time.sleep(.2);field(180,55,'added');field(180,90,'changed');click(w.get_geometry().width-200,90);key(w,'s',True)
  shot(w,'search-literal');wait(lambda:first.read_text().endswith('changed!'));check(True,'native search row performs literal replacement')
  click(w.get_geometry().width-185,55);field(180,55,'changed.');field(180,90,'done');click(w.get_geometry().width-200,90);key(w,'s',True)
  wait(lambda:first.read_text().endswith('done'));check(True,'native regex replacement uses the upstream regex engine')
  key(w,'Escape');check(app.poll()is None,'Escape closes search without exiting the editor')
  stop(host);time.sleep(.7);restored=shot(w,'restored');check(restored.crop((0,0,350,30)).tobytes()==initial.crop((0,0,350,30)).tobytes(),'local menu returns when its host disappears')
  key(w,'q',True);wait(lambda:app.poll()is not None);check(app.returncode==0,'clean document exits without a crash')
  app=subprocess.Popen([os.environ.get('XNEDIT_TEST_BINARY',str(root/'build-fltk/xnedit')),str(first),str(second)],env=env,stdout=log,stderr=log);children.append(app);w=wait(win);wait(lambda:'second.py' in str(w.get_wm_name()));key(w,'End',True)
  for c in 'tabbed':key(w,c)
  key(w,'s',True);wait(lambda:second.read_text().endswith('tabbed'));check(first.read_text().endswith('done'),'multiple tabs keep their buffers and paths independent')
  key(w,'Page_Down',True);wait(lambda:'first.txt' in str(w.get_wm_name()));key(w,'q',True);wait(lambda:app.poll()is not None)
  saved_text=first.read_text();app=subprocess.Popen([os.environ.get('XNEDIT_TEST_BINARY',str(root/'build-fltk/xnedit')),str(first)],env=env,stdout=log,stderr=log);children.append(app);w=wait(win);time.sleep(.3);key(w,'End',True)
  for c in 'recover':key(w,c)
  recovery=wait(lambda:next(iter((tmp/'state/xnedit-xr/recovery').glob('*.json')),None),10)
  payload=json.loads(recovery.read_text());check(payload['text'].endswith('recover') and (recovery.stat().st_mode&0o777)==0o600,'modified buffer receives a private recovery copy')
  app.kill();app.wait(timeout=5);check(first.read_text()==saved_text,'crash simulation leaves the saved file untouched')
  app=subprocess.Popen([os.environ.get('XNEDIT_TEST_BINARY',str(root/'build-fltk/xnedit')),str(first)],env=env,stdout=log,stderr=log);children.append(app)
  dialog=wait(lambda:next((i for i in windows()if i.get_attributes().map_state==X.IsViewable and 70<i.get_geometry().height<250 and i.get_geometry().width>250),None));key(dialog,'Return');time.sleep(.4);w=wait(win);key(w,'s',True);wait(lambda:first.read_text().endswith('recover'));check(True,'default recovery action restores unsaved text and can save it');key(w,'q',True);wait(lambda:app.poll()is not None)
  before=first.read_bytes();app=subprocess.Popen([os.environ.get('XNEDIT_TEST_BINARY',str(root/'build-fltk/xnedit')),'--read-only',str(first)],env=env,stdout=log,stderr=log);children.append(app);w=wait(win);time.sleep(.3);key(w,'End',True);key(w,'x');key(w,'s',True);check(first.read_bytes()==before,'read-only documents reject typing and saving');key(w,'q',True);wait(lambda:app.poll()is not None)

 finally:
  xd.close()
  for p in reversed(children):stop(p)
  log.close()
(out/'ui-tests.json').write_text(json.dumps(checks,indent=2)+'\n')
