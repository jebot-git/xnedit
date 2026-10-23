#!/usr/bin/env python3
"""Prepare a verified release installation; privileged mutation is a separate final step."""
import argparse,hashlib,json,os,pwd,shutil,stat,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
def merge_mimes(text,mimes):
 wanted=set(mimes);seen=set();lines=text.splitlines();out=[];inside=False;found=False
 def missing():return [m+'=org.domed.XNEdit.desktop;' for m in mimes if m not in seen]
 for line in lines:
  stripped=line.strip()
  if stripped.startswith('[') and stripped.endswith(']'):
   if inside:out.extend(missing());seen.update(wanted)
   inside=stripped=='[Default Applications]';found|=inside
  if inside and '=' in line and not stripped.startswith(('#',';')):
   key=line.split('=',1)[0].strip()
   if key in wanted:
    if key not in seen:out.append(key+'=org.domed.XNEdit.desktop;');seen.add(key)
    continue
  out.append(line)
 if inside:out.extend(missing())
 elif not found:out+=['','[Default Applications]',*missing()]
 return '\n'.join(out)+'\n'
def prepare(directory,remove_kate):
 if os.geteuid()==0:raise SystemExit('Prepare and build as the regular desktop user')
 directory=directory.resolve();directory.mkdir(parents=True,exist_ok=True);stage=directory/'stage';stage.mkdir(exist_ok=True)
 cache=(ROOT/'build-fltk/CMakeCache.txt').read_text()
 assert 'CMAKE_BUILD_TYPE:STRING=Release' in cache and 'XNEDIT_ENABLE_X11:BOOL=OFF' in cache,'Stage only the optimized default Wayland build'
 subprocess.run(['ctest','--test-dir',str(ROOT/'build-fltk'),'--output-on-failure'],check=True)
 subprocess.run(['cmake','--install',str(ROOT/'build-fltk'),'--prefix','/usr/local','--strip'],env={**os.environ,'DESTDIR':str(stage)},check=True)
 subprocess.run(['desktop-file-validate',str(stage/'usr/local/share/applications/org.domed.XNEdit.desktop')],check=True)
 mimes=(ROOT/'packaging/dome-text-mimes.list').read_text().splitlines();system=Path('/etc/xdg/dome-mimeapps.list');target=stage/'etc/xdg/dome-mimeapps.list';target.parent.mkdir(parents=True,exist_ok=True);target.write_text(merge_mimes(system.read_text()if system.exists()else'',mimes));target.chmod(0o644)
 user_mime=(Path(os.environ.get('XDG_CONFIG_HOME',Path.home()/'.config'))/'dome-mimeapps.list').resolve();user_before=sha(user_mime);desired=directory/'user-dome-mimeapps.list';desired.write_text(merge_mimes(user_mime.read_text()if user_mime.exists()else'',mimes));desired.chmod(0o600)
 for name in ['finish-user.py','install-prepared.py']:shutil.copy2(ROOT/'scripts'/name,directory/name)
 manifest=[]
 for path in sorted(stage.rglob('*')):
  if path.is_file():
   assert not path.is_symlink()
   relative=path.relative_to(stage).as_posix();destination=Path('/')/relative
   manifest.append({'path':relative,'sha256':sha(path),'mode':stat.S_IMODE(path.stat().st_mode),'before':sha(destination)})
 packages={}
 if remove_kate:
  for name in ['kate','kate-plugins','kate-krunner-plugin']:
   result=subprocess.run(['rpm','-q','--qf','%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}',name],capture_output=True,text=True)
   if result.returncode==0:packages[name]=result.stdout
   elif 'not installed' not in result.stdout+result.stderr:raise RuntimeError(result.stderr+result.stdout)
 user=pwd.getpwuid(os.getuid())
 plan={'manifest':manifest,'user':{'name':user.pw_name,'uid':user.pw_uid,'gid':user.pw_gid,'mime':str(user_mime),'before':user_before,'desired_sha256':sha(desired),'mode':stat.S_IMODE(user_mime.stat().st_mode)if user_mime.exists()else 0o644},'packages':packages,'helper_sha256':{'finish-user.py':sha(directory/'finish-user.py')},'session_restart':False}
 (directory/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
 print(json.dumps({'stage':str(stage),'plan':str(directory/'plan.json'),'files':len(manifest),'remove_packages':packages,'install_command':['sudo','python3',str(directory/'install-prepared.py')]}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepare',required=True,type=Path);p.add_argument('--remove-kate',action='store_true');args=p.parse_args();prepare(args.prepare,args.remove_kate)
