#!/usr/bin/env python3
"""Test the editor's real plaintext Spartan transport against isolated TCP servers."""
import hashlib,pathlib,socket,sys,threading,time
import remote
if len(sys.argv)>1:remote.binary=pathlib.Path(sys.argv[1]).resolve()
run=remote.run
check=remote.check

class Fixture:
 def __init__(self,ipv6=False):
  self.socket=socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET,socket.SOCK_STREAM)
  self.socket.bind(('::1' if ipv6 else '127.0.0.1',0));self.socket.listen();self.socket.settimeout(.2)
  self.host='[::1]' if ipv6 else 'localhost';self.url=f'spartan://{self.host}:{self.socket.getsockname()[1]}'
  self.pages={'/page.gmi':'# Original\r\nHello 世界\r\n'.encode(),'/empty.gmi':b'', '/decline':b'old', '/drop':b'old','/publish-redirect':b'old','/server-error':b'old'}
  self.events=[];self.errors=[];self.stop=threading.Event();self.thread=threading.Thread(target=self.accept,daemon=True);self.thread.start()
 def accept(self):
  while not self.stop.is_set():
   try:conn,_=self.socket.accept()
   except socket.timeout:continue
   except OSError:return
   threading.Thread(target=self.serve,args=(conn,),daemon=True).start()
 def serve(self,conn):
  try:
   with conn:
    conn.settimeout(4);header=b''
    while not header.endswith(b'\r\n'):
     c=conn.recv(1)
     if not c:return
     header+=c
     assert len(header)<=1024
    host,path,size=header[:-2].decode('ascii').split(' ');assert host==self.host
    left=int(size);body=b''
    while left:
     data=conn.recv(min(left,65536))
     if not data:return
     body+=data;left-=len(data)
    self.events.append((path,int(size),body,header))
    if int(size):
     if path=='/decline':response=b'4 Upload refused\r\n'
     elif path=='/server-error':response=b'5 Write failed\r\n'
     else:
      if path=='/publish-redirect':self.pages['/published.gmi']=body;response=b'3 /published.gmi\r\n'
      else:self.pages[path]=body;response=b'2 text/plain\r\nStored'
      if path=='/drop':return
    elif path=='/redirect':response=b'3 /page.gmi\r\n'
    elif path=='/external':response=b'3 spartan://example.invalid/\r\n'
    elif path=='/network-path':response=b'3 //example.invalid/\r\n'
    elif path=='/redirect-query':response=b'3 /submit?data\r\n'
    elif path=='/badheader':response=b'20 text/plain\r\n'
    elif path=='/badstatus':response=b'1 text/plain\r\n'
    elif path=='/nonascii':response='2 text/plain; name=世界\r\n'.encode()
    elif path=='/large':response=b'2 text/plain\r\n'+b'x'*(8*1024*1024+1)
    elif path=='/slow':time.sleep(2);response=b'2 text/plain\r\nslow'
    elif path=='/unavailable':response=b'5 Read failed\r\n'
    elif path not in self.pages:response=b'4 Resource unavailable\r\n'
    else:response=b'2 text/gemini; charset=utf-8\r\n'+self.pages[path]
    conn.sendall(response)
  except OSError:pass
  except Exception as error:self.errors.append(str(error))
 def close(self):self.stop.set();self.socket.close();self.thread.join(timeout=2)

def main():
 f=Fixture()
 try:
  fetch=lambda path='/page.gmi',**kw:run({'op':'fetch','url':f.url+path,**kw})
  def push(path,body,baseline='',**kw):return run({'op':'push','url':f.url+path,'read':f.url+path,'baseline':baseline,'body':body,**kw})
  check(run({'op':'parse','url':'spartan://EXAMPLE.org/'})['url']=='spartan://example.org/','Spartan default port 300')
  check(run({'op':'parse','url':'spartan://[::1]:300/'})['peer']=='[::1]:300','Spartan IPv6 origin')
  check(run({'op':'parse','url':'spartan://BÜCHER.example/世界.gmi'})['url']=='spartan://xn--bcher-kva.example/%E4%B8%96%E7%95%8C.gmi','Spartan IDNA and UTF-8 URL encoding')
  check(run({'op':'parse','url':'spartan://anon@example.org/page#section'})['url']=='spartan://example.org/page','userinfo and fragment have no protocol meaning')
  response=fetch(certificate='/missing-private-certificate',key='/missing-private-key',penguin_store='/missing-store',penguin_id='unused',pins={f'localhost:{f.socket.getsockname()[1]}':'0'*64})
  check(response['ok'] and response['status']==2 and response['body'].encode()==f.pages['/page.gmi'],'plaintext retrieval ignores TLS identities/pins and preserves exact bytes')
  check(f.events[-1][3]==b'localhost /page.gmi 0\r\n','Spartan retrieval wire header omits port and TLS')
  original=hashlib.sha256(f.pages['/page.gmi']).hexdigest();new='# Spartan edit\r\nŽluťoučký 世界\r\n'
  response=push('/page.gmi',new,original)
  check(response['ok'] and response['status']==2 and f.pages['/page.gmi']==new.encode(),'Spartan uploads exact UTF-8/CRLF document bytes')
  check(f.events[-1][1]==len(new.encode()) and f.events[-1][3]==f'localhost /page.gmi {len(new.encode())}\r\n'.encode(),'Spartan content length counts UTF-8 bytes')
  before=len(f.events);response=push('/empty.gmi','',hashlib.sha256(b'').hexdigest())
  check(not response['ok'] and 'empty' in response['error'] and len(f.events)==before,'empty uploads rejected before preflight or network activity')
  response=push('/page.gmi','later',original)
  check(response['kind']=='conflict' and f.pages['/page.gmi']==new.encode(),'Spartan remote changes stop mutation')
  check(push('/page.gmi','forced',original,overwrite=True)['ok'],'explicit Spartan overwrite')
  response=push('/new.gmi','created\n')
  check(response['kind']=='conflict' and '/new.gmi' not in f.pages,'ambiguous Spartan client error requires explicit creation confirmation')
  check(push('/new.gmi','created\n',overwrite=True)['ok'] and f.pages['/new.gmi']==b'created\n','confirmed creation of a Spartan resource')
  response=push('/decline','refused',hashlib.sha256(b'old').hexdigest());check(not response['ok'] and f.pages['/decline']==b'old','Spartan client error is not successful upload')
  response=push('/server-error','refused',hashlib.sha256(b'old').hexdigest());check(not response['ok'] and f.pages['/server-error']==b'old','Spartan server error is not successful upload')
  check(not push('/unavailable','no write',overwrite=True)['ok'],'failed preflight server error cannot be overwritten blindly')
  response=push('/drop','uncertain',hashlib.sha256(b'old').hexdigest());check(not response['ok'] and 'may have arrived' in response['error'],'interrupted Spartan acknowledgement is uncertain and never retried')
  count=len(f.events);check(push('/drop','uncertain',hashlib.sha256(b'old').hexdigest())['ok'] and len(f.events)==count+1,'matching remote contents avoid duplicate upload after uncertainty')
  response=push('/publish-redirect','published',hashlib.sha256(b'old').hexdigest());check(response['ok'] and response['url']==f.url+'/published.gmi','Spartan publication redirect verified against the uploaded bytes')
  check(fetch('/redirect')['url']==f.url+'/page.gmi','Spartan absolute-path retrieval redirect')
  for path in ['/external','/network-path','/redirect-query','/badheader','/badstatus','/nonascii','/large']:
   check(not fetch(path)['ok'],'reject unsafe or malformed Spartan response '+path)
  before=len(f.events)
  check(not fetch('/page.gmi?data')['ok'] and len(f.events)==before,'editing retrieval never converts a query into an unintended upload')
  check(not push('/page.gmi?data','hello',overwrite=True)['ok'] and len(f.events)==before,'Spartan upload URLs cannot add query data')
  check(not push('/page.gmi','hello',token='secret')['ok'] and len(f.events)==before,'Spartan does not transmit Titan tokens')
  started=time.monotonic();response=fetch('/slow',cancel_ms=100);check(not response['ok'] and time.monotonic()-started<1.5,'Spartan background transfer cancellation')
  check(not fetch(cancelled=True)['ok'],'Spartan pre-canceled transfer')
  check(not f.errors,'plaintext fixture received no unexpected TLS or malformed requests')
 finally:f.close()
 if socket.has_ipv6:
  try:v6=Fixture(True)
  except OSError:return
  try:
   response=run({'op':'fetch','url':v6.url+'/page.gmi'})
   check(response['ok'] and v6.events[-1][3]==b'[::1] /page.gmi 0\r\n','IPv6 wire host is bracketed without the port')
  finally:v6.close()
if __name__=='__main__':main()
