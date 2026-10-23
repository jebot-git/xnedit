#!/usr/bin/env python3
"""Apply prepared DomeD text defaults with the desktop user's own privileges."""
from pathlib import Path
import hashlib,json,os,tempfile
root=Path(__file__).resolve().parent;plan=json.loads((root/'plan.json').read_text());user=plan['user'];assert os.geteuid()==user['uid'] and os.geteuid()!=0
path=Path(user['mime']);actual=hashlib.sha256(path.read_bytes()).hexdigest()if path.is_file()else None
assert actual==user['before'],'DomeD user MIME defaults changed after preparation'
data=(root/'user-dome-mimeapps.list').read_bytes();assert hashlib.sha256(data).hexdigest()==user['desired_sha256']
path.parent.mkdir(parents=True,exist_ok=True);fd,name=tempfile.mkstemp(prefix='.xnedit-defaults-',dir=path.parent)
try:
 os.fchmod(fd,user['mode'])
 with os.fdopen(fd,'wb')as output:output.write(data);output.flush();os.fsync(output.fileno())
 os.replace(name,path)
finally:
 if os.path.exists(name):os.unlink(name)
