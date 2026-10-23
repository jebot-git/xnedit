#!/usr/bin/env python3
"""Install only the prepared release and explicitly listed Kate packages."""
import hashlib,json,os,shutil,subprocess,time
from pathlib import Path
assert os.geteuid()==0,'Run this prepared final step with sudo'
root=Path(__file__).resolve().parent;plan=json.loads((root/'plan.json').read_text());stage=root/'stage'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()if path.is_file()else None
for name,digest in plan['helper_sha256'].items():assert name=='finish-user.py' and sha(root/name)==digest
user=plan['user'];assert user['uid']>0 and Path(user['mime']).is_absolute();assert sha(Path(user['mime']))==user['before'],'User MIME preferences changed since preparation'
assert sha(root/'user-dome-mimeapps.list')==user['desired_sha256']
for item in plan['manifest']:
 relative=Path(item['path']);assert not relative.is_absolute()and'..'not in relative.parts
 assert item['path'].startswith(('usr/local/bin/','usr/local/share/doc/xnedit-xr/','usr/local/share/applications/'))or item['path']=='etc/xdg/dome-mimeapps.list'
 source=stage/relative;assert source.is_file()and not source.is_symlink()and sha(source)==item['sha256']
 assert sha(Path('/')/relative)==item['before'],'System file changed since preparation: '+item['path']
packages=plan['packages'];assert set(packages)<= {'kate','kate-plugins','kate-krunner-plugin'}
for name,expected in packages.items():assert subprocess.check_output(['rpm','-q','--qf','%{NAME}-%{VERSION}-%{RELEASE}.%{ARCH}',name],text=True)==expected
if packages:subprocess.run(['rpm','-e','--test',*packages],check=True)
backup=Path('/var/backups/de-xr-update')/('xnedit-fltk-'+time.strftime('%Y%m%d-%H%M%S'));backup.mkdir(parents=True)
def save_old(path):
 if path.is_file()or path.is_symlink():
  target=backup/path.relative_to('/');target.parent.mkdir(parents=True,exist_ok=True)
  if not target.exists()and not target.is_symlink():subprocess.run(['cp','-a','--reflink=auto','--',str(path),str(target)],check=True)
for item in plan['manifest']:
 destination=Path('/')/item['path'];save_old(destination);destination.parent.mkdir(parents=True,exist_ok=True);temporary=destination.with_name('.'+destination.name+'.xnedit-new');assert not temporary.exists()
 shutil.copy2(stage/item['path'],temporary);os.chown(temporary,0,0);temporary.chmod(item['mode']);os.replace(temporary,destination)
subprocess.run(['update-desktop-database','/usr/local/share/applications'],check=True)
options=subprocess.check_output(['/usr/local/bin/xnedit','--build-options'],text=True);assert 'WAYLAND=ON X11=OFF' in options and 'MOTIF=OFF' in options
save_old(Path(user['mime']))
subprocess.run(['runuser','-u',user['name'],'--','/usr/bin/python3',str(root/'finish-user.py')],check=True)
for mime in ['text/plain','text/x-python','application/json']:
 result=subprocess.check_output(['runuser','-u',user['name'],'--','env','LANG=C','XDG_CURRENT_DESKTOP=Dome','XDG_CONFIG_HOME='+str(Path(user['mime']).parent),'gio','mime',mime],text=True)
 assert result.splitlines()[0].rstrip().endswith('org.domed.XNEdit.desktop'),'New default did not resolve: '+mime
if packages:
 for path in subprocess.check_output(['rpm','-ql',*packages],text=True).splitlines():save_old(Path(path))
 subprocess.run(['rpm','-e',*packages],check=True)
 subprocess.run(['update-desktop-database','/usr/share/applications'],check=True)
shutil.copy2(root/'plan.json',backup/'plan.json')
report={'backup':str(backup),'installed_files':len(plan['manifest']),'removed_packages':packages,'default':'org.domed.XNEdit.desktop','sessions_restarted':False}
(backup/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
