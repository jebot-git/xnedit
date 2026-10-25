#!/usr/bin/env python3
"""Exercise the real C++ transport against an isolated mutual-TLS Gemini/Titan server."""
import contextlib,hashlib,json,os,pathlib,socket,ssl,subprocess,sys,tempfile,threading,time,urllib.parse
binary=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parents[1]/"build-fltk/remote-driver"

def certificate(root,base,host='localhost'):
 crt=root/(base+'.crt');key=root/(base+'.key')
 subprocess.run(['openssl','req','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:prime256v1','-nodes','-days','1','-subj','/UID=test/CN='+host,'-addext','subjectAltName=DNS:'+host+',IP:127.0.0.1','-keyout',str(key),'-out',str(crt)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 os.chmod(key,0o600);fp=hashlib.sha256(ssl.PEM_cert_to_DER_cert(crt.read_text())).hexdigest();return crt,key,fp

class Fixture:
 def __init__(self,root):
  self.crt,self.key,self.fp=certificate(root,'server');self.client,self.client_key,self.client_fp=certificate(root,'client')
  self.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);self.context.load_cert_chain(self.crt,self.key);self.context.verify_mode=ssl.CERT_OPTIONAL;self.context.load_verify_locations(self.client)
  self.socket=socket.socket();self.socket.bind(('127.0.0.1',0));self.socket.listen();self.socket.settimeout(.2);self.port=self.socket.getsockname()[1]
  self.url=f'gemini://localhost:{self.port}';self.titan=f'titan://localhost:{self.port}';self.pin={f'localhost:{self.port}':self.fp}
  self.pages={'/page.gmi':'# Original\r\nHello 世界\r\n'.encode(),'/drop':b'old','/decline':b'old','/publish-redirect':b'old','/redirect-fail':b'old','/token':b'old','/rate':b'old'}
  self.events=[];self.errors=[];self.stop=threading.Event();self.thread=threading.Thread(target=self.accept,daemon=True);self.thread.start()
 def accept(self):
  while not self.stop.is_set():
   try:raw,_=self.socket.accept()
   except socket.timeout:continue
   except OSError:return
   threading.Thread(target=self.serve,args=(raw,),daemon=True).start()
 def serve(self,raw):
  try:
   with self.context.wrap_socket(raw,server_side=True) as conn:
    conn.settimeout(4);header=b''
    while not header.endswith(b'\r\n'):
     byte=conn.recv(1)
     if not byte:return
     header+=byte
     if len(header)>4096:raise AssertionError('oversized header')
    text=header[:-2].decode();parts=text.split(';');url=urllib.parse.urlsplit(parts[0]);path=url.path
    if url.scheme=='titan':
     params=dict(p.split('=',1) for p in parts[1:]);left=int(params['size']);body=b''
     while left:
      data=conn.recv(min(left,65536))
      if not data:break
      body+=data;left-=len(data)
     self.events.append(('titan',path,body,params,bool(conn.getpeercert(binary_form=True))))
     if path=='/rate' and len([e for e in self.events if e[0]=='titan' and e[1]=='/rate'])==1:response=b'44 1\r\n'
     elif path=='/decline':response=b'51 Refused\r\n'
     elif path=='/token' and params.get('token')!='test-token':response=b'59 Token required\r\n'
     else:
      assert not left;self.pages[path]=body
      if path=='/drop':return
      if path=='/publish-redirect':response=('30 '+self.url+path+'\r\n').encode()
      elif path=='/redirect-fail':response=b'30 gemini://example.invalid/\r\n'
      else:response=b'20 Stored\r\n'
    else:
     self.events.append(('gemini',path,bool(conn.getpeercert(binary_form=True))))
     if path=='/redirect':response=('30 '+self.url+'/page.gmi\r\n').encode()
     elif path=='/external':response=b'30 gemini://example.invalid/\r\n'
     elif path=='/badheader':response=b'20x Invalid\r\n'
     elif path=='/large':response=b'20 text/plain\r\n'+b'x'*(8*1024*1024+1)
     elif path=='/slow':time.sleep(2);response=b'20 text/plain\r\nslow'
     elif path not in self.pages:response=b'51 Not found\r\n'
     else:response=b'20 text/gemini; charset=utf-8\r\n'+self.pages[path]
    conn.sendall(response)
    with contextlib.suppress(OSError):conn.unwrap().close()
  except (OSError,ssl.SSLError):pass
  except Exception as error:self.errors.append(str(error))
 def close(self):self.stop.set();self.socket.close();self.thread.join(timeout=2)

def run(request):
 result=subprocess.run([str(binary)],input=json.dumps(request)+'\n',capture_output=True,text=True,timeout=12)
 assert result.returncode==0,(result.returncode,result.stderr)
 return json.loads(result.stdout)

def check(condition,label):
 assert condition,label
 print('PASS',label,flush=True)

def main():
 with tempfile.TemporaryDirectory(prefix='xnedit-smallnet-') as name:
  root=pathlib.Path(name);f=Fixture(root)
  try:
   check(run({'op':'mime','meta':'text/gemini; charset="UTF-8"; lang=en'})['mime']=='text/gemini','editable text metadata accepts UTF-8')
   check(not run({'op':'mime','meta':'text/plain; charset=ISO-8859-1'})['ok'],'non-UTF-8 charset is refused before editing')
   fetch=lambda path='/page.gmi',**kw:run({'op':'fetch','url':f.url+path,'pins':f.pin,**kw})
   def push(path,body,baseline='',**kw):return run({'op':'push','url':f.titan+path,'read':f.url+path,'pins':f.pin,'baseline':baseline,'body':body,**kw})
   check(run({'op':'parse','url':'gemini://BÜCHER.example/世界.gmi'})['url']=='gemini://xn--bcher-kva.example/%E4%B8%96%E7%95%8C.gmi','IDNA and UTF-8 URL encoding')
   check(run({'op':'parse','url':'gemini://[::1]/'})['peer']=='[::1]:1965','IPv6 origin and default port')
   for bad in ['smtp://host/','gemini://user:secret@host/','gemini://host/\r\nInjected','gemini://host/%XX','gemini://host/#fragment','gemini://host:0/','gemini://local%0Ahost/']:
    check(not run({'op':'parse','url':bad})['ok'],'reject unsafe URL '+repr(bad))
   before=len(f.events);response=fetch(pins={});check(response['kind']=='trust' and response['fingerprint']==f.fp and len(f.events)==before,'first trust check sends no application data')
   response=fetch(pins={f'localhost:{f.port}':'0'*64});check(response['kind']=='trust' and response['previous']=='0'*64 and len(f.events)==before,'changed pin blocks request before application data')
   response=fetch();check(response['ok'] and response['body'].encode()==f.pages['/page.gmi'],'retrieve exact UTF-8/CRLF bytes')
   original=hashlib.sha256(f.pages['/page.gmi']).hexdigest();new='# Edited\r\nŽluťoučký 世界\r\n'
   response=push('/page.gmi',new,original,certificate=str(f.client),key=str(f.client_key),token='test-token')
   event=[e for e in f.events if e[0]=='titan'][-1]
   check(response['ok'] and f.pages['/page.gmi']==new.encode() and event[3]['size']==str(len(new.encode())) and event[4],'mutual-TLS Titan push uses UTF-8 byte size and exact bytes')
   count=len([e for e in f.events if e[0]=='titan']);response=push('/page.gmi','newer',original)
   check(response['kind']=='conflict' and len([e for e in f.events if e[0]=='titan'])==count,'remote changes stop push before mutation')
   check(push('/page.gmi','forced',original,overwrite=True)['ok'],'explicit overwrite after conflict')
   check(push('/created.gmi','created\n')['ok'] and f.pages['/created.gmi']==b'created\n','create a missing remote page')
   check(fetch('/redirect')['url']==f.url+'/page.gmi','same-server Gemini redirect')
   check(not fetch('/external')['ok'],'cross-server redirect blocked')
   check(not fetch('/badheader')['ok'],'malformed response rejected')
   check(not fetch('/large')['ok'],'8 MiB response bound')
   started=time.monotonic();response=fetch('/slow',cancel_ms=100)
   check(not response['ok'] and time.monotonic()-started<1.5,'in-flight retrieval can be canceled')
   check(not fetch(cancelled=True)['ok'],'pre-canceled request')
   response=push('/decline','refused',hashlib.sha256(b'old').hexdigest());check(not response['ok'] and f.pages['/decline']==b'old','declined upload is not successful')
   response=push('/drop','uncertain',hashlib.sha256(b'old').hexdigest());check(not response['ok'] and 'may have arrived' in response['error'],'lost response is reported as uncertain without retry')
   count=len([e for e in f.events if e[0]=='titan']);response=push('/drop','uncertain',hashlib.sha256(b'old').hexdigest())
   check(response['ok'] and len([e for e in f.events if e[0]=='titan'])==count,'retry verifies matching contents without duplicate upload')
   check(push('/publish-redirect','published',hashlib.sha256(b'old').hexdigest())['ok'],'publication redirect verified against retrieved bytes')
   check(not push('/redirect-fail','published',hashlib.sha256(b'old').hexdigest())['ok'],'unverified upload redirect is not success')
   check(push('/rate','rate-limited',hashlib.sha256(b'old').hexdigest())['ok'] and f.pages['/rate']==b'rate-limited','explicit slow-down rejection is retried after its requested delay')
   check(not push('/token','x',hashlib.sha256(b'old').hexdigest(),token='bad;injected=1')['ok'],'Titan token delimiter injection rejected')
   store=root/'state.json';store.write_text(json.dumps({'identities':[{'id':'chosen','address':'test@localhost','certificate':f.client.read_text(),'key':f.client_key.read_text()}]}));os.chmod(store,0o600)
   check(run({"op":"identities","store":str(store)})["identities"]==[["chosen","test@localhost"]],"identity chooser reads stable identity references")
   response=fetch(penguin_store=str(store),penguin_id='chosen');check(response['ok'] and f.events[-1][2],'existing Penguin Mail identity is loaded without copying a key file')
   changed,key,changed_fp=certificate(root,'changed');f.context.load_cert_chain(changed,key)
   response=fetch();check(response['kind']=='trust' and response['fingerprint']==changed_fp,'server certificate rotation requires fresh trust')
   wrong,key,_=certificate(root,'wrong','wrong.example');f.context.load_cert_chain(wrong,key)
   response=fetch();check(not response['ok'] and response.get('kind')!='trust' and 'does not name' in response['error'],'hostname mismatch is refused before trust prompt')
   check(not f.errors,'fixture has no unexpected errors')
  finally:f.close()

if __name__=="__main__":
 main()
