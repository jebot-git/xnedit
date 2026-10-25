#!/usr/bin/env python3
"""Scroll and Gopher+ fixtures, shared with native Wayland UI tests."""
import contextlib, json, pathlib, ssl, subprocess, sys, tempfile, time, urllib.parse
from remote import Fixture
from remote_files import Stream
binary=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parents[1]/'build-fltk/remote-driver'
class ScrollFixture(Fixture):
    def __init__(self,root):
        root=pathlib.Path(root);root.mkdir(parents=True,exist_ok=True)
        super().__init__(root);self.url=self.url.replace('gemini://','scroll://');self.events=[]
    def serve(self,raw):
        try:
            with self.context.wrap_socket(raw,server_side=True) as connection:
                connection.settimeout(4);header=b''
                while not header.endswith(b'\r\n'):
                    c=connection.recv(1)
                    if not c:return
                    header+=c
                    if len(header)>4096:raise AssertionError('oversized Scroll request')
                url,languages=header[:-2].decode().split(' ');path=urllib.parse.urlsplit(url).path
                self.events.append((url,languages,bool(connection.getpeercert(binary_form=True))))
                if path=='/redirect':reply=('30 '+self.url+'/page.scroll\r\n').encode()
                elif path=='/external':reply=b'30 scroll://example.invalid/\r\n'
                elif path=='/missing':reply=b'51 Not found\r\n'
                elif path=='/bad-meta':reply=b'24 text/scroll\r\nAuthor\r\n'
                elif path=='/long-meta':reply=b'24 text/scroll\r\n'+b'x'*1024+b'\r\n\r\n\r\n'
                elif path=='/invalid-meta':reply=b'24 text/scroll\r\n\xff\r\n\r\n\r\n'
                else:
                    if path=='/slow':time.sleep(2)
                    body=b'x'*(8*1024*1024+1) if path=='/large' else '# Scroll\r\n* Content 世界\r\n'.encode()
                    reply=b'24 text/scroll; charset=utf-8; lang=en\r\nAuthor\r\n2026-01-01T00:00:00Z\r\n2026-01-02T00:00:00Z\r\n'+body
                connection.sendall(reply)
                with contextlib.suppress(OSError):connection.unwrap().close()
        except (OSError,ssl.SSLError):pass
        except Exception as error:self.errors.append(str(error))
class PlusFixture(Stream):
    body=b'+INFO: 0Text\t/doc\thost\t70\t+\r\n+ADMIN:\r\n Admin: Alice\r\n'
    def __init__(self):super().__init__('plus')
    def serve(self,connection):
        try:
            with connection:
                connection.settimeout(4);request=b''
                while True:
                    block=connection.recv(16384)
                    if not block:break
                    request+=block
                self.received.append(request)
                if request.startswith(b'/sized'):reply=('+'+str(len(self.body))+'\r\n').encode()+self.body
                elif request.startswith(b'/eof'):reply=b'+-2\r\n'+self.body+b'.\r\n'
                elif request.startswith(b'/truncated'):reply=b'+999\r\n'+self.body
                elif request.startswith(b'/bad-dot'):reply=b'+-1\r\n'+self.body
                elif request.startswith(b'/error'):reply=b'--1\r\n1 Not available\r\n.\r\n'
                elif request.startswith(b'/plain'):reply=self.body+b'.\r\n'
                else:reply=b'+-1\r\n'+self.body+b'.\r\n'
                connection.sendall(reply)
        except OSError:pass
        except Exception as error:self.errors.append(str(error))
def run(request):
    p=subprocess.run([str(binary)],input=json.dumps(request)+'\n',capture_output=True,text=True,timeout=12)
    assert p.returncode==0,p.stderr
    return json.loads(p.stdout)
def main():
    count=0
    def check(ok,label):
        nonlocal count
        assert ok,label
        count+=1;print('PASS',label,flush=True)
    with tempfile.TemporaryDirectory(prefix='xnedit-scroll-') as directory:
        fixture=ScrollFixture(pathlib.Path(directory)/'scroll');plus=PlusFixture()
        try:
            url=fixture.url+'/page.scroll'
            response=run({'op':'fetch','url':url,'pins':fixture.pin,'languages':'cs-CZ,en'})
            check(response['ok'] and response['status']==24 and response['body']=='# Scroll\r\n* Content 世界\r\n','Scroll UDC success returns only editable body, excluding metadata')
            check(response['author']=='Author' and response['published']=='2026-01-01T00:00:00Z' and response['modified']=='2026-01-02T00:00:00Z','Scroll author and dates are parsed separately')
            check(fixture.events[-1][1]=='cs-CZ,en','Scroll sends the BCP47 language list with the request')
            before=len(fixture.events);response=run({'op':'fetch','url':url})
            check(response.get('kind')=='trust' and len(fixture.events)==before,'Scroll TOFU blocks application requests before trust')
            response=run({'op':'fetch','url':url,'pins':fixture.pin,'certificate':str(fixture.client),'key':str(fixture.client_key)})
            check(response['ok'] and fixture.events[-1][2],'Scroll uses the existing client identity mechanism')
            response=run({'op':'fetch','url':fixture.url+'/redirect','pins':fixture.pin})
            check(response['ok'] and response['url']==url,'Scroll follows only same-server redirects')
            for path in ['/external','/bad-meta','/long-meta','/invalid-meta','/large']:
                check(not run({'op':'fetch','url':fixture.url+path,'pins':fixture.pin})['ok'],'Scroll rejects '+path)
            response=run({'op':'fetch','url':fixture.url+'/missing','pins':fixture.pin})
            check(response['ok'] and response['status']==51,'Scroll non-success responses need no metadata lines')
            response=run({'op':'fetch','url':fixture.url+'/slow','pins':fixture.pin,'cancel_ms':50})
            check(not response['ok'] and 'canceled' in response['error'],'Scroll retrieval can be canceled')
            check(run({'op':'parse','url':'scroll://host/'})['peer']=='host:5699','Scroll defaults to port 5699')
            check(run({'op':'parse','url':'gopher+://host/'})['peer']=='host:70','Gopher+ defaults to port 70')
            root=f'gopher+://127.0.0.1:{plus.port}'
            response=run({'op':'fetch','url':root+'/'})
            check(response['ok'] and plus.received[-1]==b'\t!\r\n','Gopher+ root uses an empty selector')
            for path in ['/doc','/sized','/plain']:
                response=run({'op':'fetch','url':root+path})
                check(response['ok'] and response['meta']=='text/x-gopherplus' and response['body'].encode()==plus.body,'Gopher+ attribute framing '+path)
                check(plus.received[-1]==(path+'\t!\r\n').encode(),'Gopher+ requests attributes with TAB ! '+path)
            response=run({'op':'fetch','url':root+'/eof'})
            check(response['ok'] and response['body'].endswith('.\r\n'),'Gopher+ EOF framing preserves a literal period line')
            for path in ['/truncated','/bad-dot']:
                check(not run({'op':'fetch','url':root+path})['ok'],'Gopher+ refuses incomplete framing '+path)
            response=run({'op':'fetch','url':root+'/error'})
            check(response['ok'] and not 20<=response['status']<=29,'Gopher+ errors do not become editable successful documents')
            uri=f'gopher://127.0.0.1:{plus.port}/0/doc%09!'
            check(run({'op':'fetch','url':uri})['meta']=='text/x-gopherplus','standard encoded Gopher+ requests work through gopher:// URLs')
            for uri in [url,root+'/doc']:
                before=len(fixture.events)+len(plus.received)
                response=run({'op':'push','read':uri,'url':uri,'body':'text'})
                check(not response['ok'] and before==len(fixture.events)+len(plus.received),'retrieval-only transport never receives a direct upload '+uri.split(':')[0])
            check(not fixture.errors and not plus.errors,'smallnet fixtures complete without unexpected errors')
        finally:fixture.close();plus.close()
    print('PASS',count,'remaining smallnet checks')
if __name__=='__main__':main()
