#!/usr/bin/env python3
"""Real loopback HTTP, byte stream, Finger, Gopher and Telnet transfers."""
import getpass, hashlib, http.server, json, os, pathlib, shutil, socket, subprocess, sys, tempfile, threading, time, urllib.parse
binary=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parents[1]/'build-fltk/remote-driver'
checks=[]
def run(request,env=None):
    p=subprocess.run([str(binary)],input=json.dumps(request)+'\n',capture_output=True,text=True,timeout=12,env=env)
    assert p.returncode==0,p.stderr
    return json.loads(p.stdout)
def check(value,label):
    assert value,label
    checks.append(label);print('PASS',label,flush=True)
class Http(http.server.BaseHTTPRequestHandler):
    pages={'/note.txt':b'Original\r\n','/ignore.txt':b'Original\r\n'}
    puts=[]
    def log_message(self,*args):pass
    def do_GET(self):
        if self.path=='/large':body=b'x'*(8*1024*1024+1)
        elif self.path=='/slow':time.sleep(2);body=b'late'
        elif self.path=='/redirect':
            self.send_response(302);self.send_header('Location','http://example.invalid/');self.end_headers();return
        elif self.path not in self.pages:self.send_error(404);return
        else:body=self.pages[self.path]
        self.send_response(200);self.send_header('Content-Type','text/plain; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.end_headers()
        try:self.wfile.write(body)
        except OSError:pass
    def do_PUT(self):
        body=self.rfile.read(int(self.headers['Content-Length']));self.puts.append((self.path,body))
        if self.path!='/ignore.txt':self.pages[self.path]=body
        self.send_response(204);self.end_headers()
class Stream:
    def __init__(self,kind):
        self.kind=kind;self.received=[];self.errors=[];self.stop=False
        self.socket=socket.socket();self.socket.bind(('127.0.0.1',0));self.socket.listen();self.socket.settimeout(.1)
        self.port=self.socket.getsockname()[1];self.thread=threading.Thread(target=self.loop,daemon=True);self.thread.start()
    def loop(self):
        while not self.stop:
            try:conn,_=self.socket.accept()
            except socket.timeout:continue
            except OSError:break
            threading.Thread(target=self.serve,args=(conn,),daemon=True).start()
    def serve(self,c):
        try:
            with c:
                c.settimeout(4);data=b''
                if self.kind in ['telnet','telnet-send','telnet-nvt']:
                    c.sendall(bytes([255,253,1,255,251,3,255,250,24,0,65,255,240]))
                    expected=6+(len('Sent 世界\r\n'.encode()) if self.kind=='telnet-send' else 0)
                    while len(data)<expected:
                        part=c.recv(expected-len(data))
                        if not part:break
                        data+=part
                    self.received.append(data)
                    c.sendall(b'Banner\r\x00World\r\n' if self.kind=='telnet-nvt' else 'Banner 世界\r\n'.encode());return
                while True:
                    part=c.recv(16384)
                    if not part:break
                    data+=part
                self.received.append(data)
                if self.kind=='slow':time.sleep(2)
                if self.kind=='gopher':c.sendall(b'1Menu\t/\thost\t70\r\n..literal\r\n.\r\n')
                else:c.sendall('Response 世界\r\n'.encode())
        except OSError:pass
        except Exception as error:self.errors.append(str(error))
    def close(self):self.stop=True;self.socket.close();self.thread.join(timeout=2)
class FtpFixture:
    def __init__(self):
        self.pages={'note.txt':b'Original\r\n'};self.stop=False
        self.socket=socket.socket();self.socket.bind(('127.0.0.1',0));self.socket.listen();self.socket.settimeout(.1)
        self.port=self.socket.getsockname()[1];self.thread=threading.Thread(target=self.loop,daemon=True);self.thread.start()
    def loop(self):
        while not self.stop:
            try:connection,_=self.socket.accept()
            except socket.timeout:continue
            except OSError:break
            threading.Thread(target=self.serve,args=(connection,),daemon=True).start()
    def serve(self,connection):
        passive=None
        try:
            with connection:
                connection.settimeout(4);lines=connection.makefile('rb')
                def reply(text):connection.sendall((text+'\r\n').encode())
                reply('220 local FTP fixture')
                for line in lines:
                    command,_,arg=line.decode().strip().partition(' ');arg=arg.lstrip('/')
                    if command=='USER':reply('331 Password required')
                    elif command=='PASS':reply('230 Logged in')
                    elif command=='PWD':reply('257 "/"')
                    elif command in ['TYPE','CWD','OPTS']:reply('200 OK')
                    elif command=='SYST':reply('215 UNIX Type: L8')
                    elif command=='EPSV':
                        passive=socket.socket();passive.bind(('127.0.0.1',0));passive.listen();passive.settimeout(4)
                        reply(f'229 Entering Extended Passive Mode (|||{passive.getsockname()[1]}|)')
                    elif command=='SIZE':reply('213 '+str(len(self.pages[arg])) if arg in self.pages else '550 Not found')
                    elif command in ['RETR','STOR']:
                        if command=='RETR' and arg not in self.pages:reply('550 Not found');continue
                        reply('150 Opening data connection');data,_=passive.accept();data.settimeout(4)
                        with data:
                            if command=='RETR':data.sendall(self.pages[arg])
                            else:
                                content=b''
                                while True:
                                    block=data.recv(16384)
                                    if not block:break
                                    content+=block
                                self.pages[arg]=content
                        passive.close();passive=None;reply('226 Transfer complete')
                    elif command=='QUIT':reply('221 Bye');break
                    else:reply('200 OK')
        except (OSError,ValueError):pass
        finally:
            if passive:passive.close()
    def close(self):self.stop=True;self.socket.close();self.thread.join(timeout=2)
def ftp_checks():
    fixture=FtpFixture();url=f'ftp://127.0.0.1:{fixture.port}/note.txt'
    try:
        response=run({'op':'fetch','url':url});check(response['ok'] and response['body']=='Original\r\n','FTP retrieves exact bytes through a passive data connection')
        base=hashlib.sha256(fixture.pages['note.txt']).hexdigest();body='FTP Edited 世界\r\n'
        response=run({'op':'push','read':url,'url':url,'baseline':base,'body':body})
        check(response['ok'] and response['verified'] and fixture.pages['note.txt']==body.encode(),'FTP uploads and reads back the saved text')
    finally:fixture.close()
class SshFixture:
    def __init__(self,root):
        self.root=pathlib.Path(root);self.home=self.root/'home';(self.home/'.ssh').mkdir(parents=True)
        self.key=self.home/'.ssh/id_ed25519';self.host=self.root/'host'
        for key in [self.key,self.host]:
            subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-f',str(key)],check=True)
        (self.root/'authorized').write_text(self.key.with_suffix('.pub').read_text())
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));self.port=sock.getsockname()[1]
        self.config=self.root/'sshd_config';self.config.write_text(f"Port {self.port}\nListenAddress 127.0.0.1\nHostKey {self.host}\nPidFile {self.root}/pid\nUsePAM no\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nPubkeyAuthentication yes\nStrictModes no\nAuthorizedKeysFile {self.root}/authorized\nSubsystem sftp internal-sftp\nAllowUsers {getpass.getuser()}\nLogLevel ERROR\n")
        self.log=(self.root/'sshd.log').open('w')
        self.process=subprocess.Popen([shutil.which('sshd'),'-D','-e','-f',str(self.config)],stdout=self.log,stderr=self.log)
        self.known=self.home/'.ssh/known_hosts';self.known.write_text(f'[127.0.0.1]:{self.port} '+self.host.with_suffix('.pub').read_text())
        self.env={**os.environ,'HOME':str(self.home)};self.env.pop('SSH_AUTH_SOCK',None)
        time.sleep(.2)
        assert self.process.poll() is None,(self.root/'sshd.log').read_text()
    def url(self,scheme,path):return f'{scheme}://{getpass.getuser()}@127.0.0.1:{self.port}'+urllib.parse.quote(str(path))
    def close(self):self.process.terminate();self.process.wait(timeout=5);self.log.close()
def ssh_checks():
    if not shutil.which('sshd') or not shutil.which('ssh-keygen'):
        print('SKIP isolated SSH tests: sshd/ssh-keygen unavailable');return
    with tempfile.TemporaryDirectory(prefix='xnedit-sftp-') as root:
        f=SshFixture(root)
        try:
            path=f.root/'note 世界 with spaces.txt';path.write_bytes(b'old\r\n');path.chmod(0o600)
            for scheme in ['sftp','scp','ssh']:
                uri=f.url(scheme,path);response=run({'op':'fetch','url':uri},f.env)
                check(response['ok'] and response['body'].encode()==path.read_bytes(),scheme+' retrieves a real file through isolated sshd with known-host verification')
                digest=hashlib.sha256(path.read_bytes()).hexdigest();body=scheme+' Edited 世界\r\n'
                response=run({'op':'push','read':uri,'url':uri,'baseline':digest,'body':body},f.env)
                check(response['ok'] and response['verified'] and path.read_bytes()==body.encode(),scheme+' saves and verifies exact Unicode content')
                check(path.stat().st_mode&0o777==0o600,scheme+' preserves an existing private file mode')
            f.known.write_text(f'[127.0.0.1]:{f.port} '+f.key.with_suffix('.pub').read_text())
            check(not run({'op':'fetch','url':f.url('sftp',path)},f.env)['ok'],'an SSH host key mismatch blocks access')
        finally:f.close()
def main():
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Http);threading.Thread(target=server.serve_forever,daemon=True).start()
    root=f'http://127.0.0.1:{server.server_port}';streams=[]
    try:
        r=run({'op':'fetch','url':root+'/note.txt'});check(r['ok'] and r['body']=='Original\r\n','HTTP GET retrieves exact text and line endings')
        base=hashlib.sha256(r['body'].encode()).hexdigest();body='Edited 世界\r\n'
        r=run({'op':'push','read':root+'/note.txt','url':root+'/note.txt','baseline':base,'body':body,'mime':'text/plain'})
        check(r['ok'] and r['verified'] and Http.pages['/note.txt']==body.encode(),'HTTP PUT checks baseline and verifies uploaded bytes by GET')
        before=len(Http.puts)
        r=run({'op':'push','read':root+'/note.txt','url':root+'/note.txt','baseline':base,'body':'Other'})
        check(r.get('kind')=='conflict' and len(Http.puts)==before,'changed remote content blocks upload before writing')
        r=run({'op':'push','read':root+'/new.txt','url':root+'/new.txt','body':'new'})
        check(r.get('kind')=='conflict','creating a remote file needs the existing overwrite/create confirmation')
        r=run({'op':'push','read':root+'/new.txt','url':root+'/new.txt','body':'','overwrite':True})
        check(r['ok'] and r['verified'] and Http.pages['/new.txt']==b'','empty file uploads can be created and verified')
        r=run({'op':'push','read':root+'/ignore.txt','url':root+'/ignore.txt','body':'ignored','overwrite':True})
        check(not r['ok'] and 'verified' in r['error'],'a server that ignores the write cannot mark a document saved')
        check(run({'op':'fetch','url':root+'/redirect'})['status']==302,'HTTP redirects are not followed to another destination')
        check(not run({'op':'fetch','url':root+'/large'})['ok'],'HTTP retrieval enforces the 8 MiB limit')
        check('canceled' in run({'op':'fetch','url':root+'/slow','cancel_ms':50})['error'],'HTTP retrieval is cancellable')
        for scheme,port in [('scp',22),('sftp',22),('ssh',22),('finger',79),('telnet',23),('ftp',21),('ftps',990),('gopher',70),('nex',1900)]:
            check(run({'op':'parse','url':f'{scheme}://host/'})['peer']==f'host:{port}',scheme+' default port')
        for bad in ['scp://user:password@host/path','sftp://user%3Apassword@host/path','nc://host/','file:///etc/passwd','smtp://host/','http://host/path#fragment','nc://host:1234/path?query']:
            check(not run({'op':'parse','url':bad})['ok'],'reject unsupported/ambiguous URL '+bad)
        for scheme in ['nc','tcp','finger','gopher','nex']:
            stream=Stream('gopher' if scheme=='gopher' else 'plain');streams.append(stream)
            suffix='/0/selector' if scheme=='gopher' else '/alice' if scheme=='finger' else '/request%0D%0A' if scheme in ['nc','tcp'] else '/path/'
            url=f'{scheme}://127.0.0.1:{stream.port}'+suffix
            r=run({'op':'fetch','url':url});check(r['ok'],scheme+' retrieves text')
            expected=b'/selector\r\n' if scheme=='gopher' else b'alice\r\n' if scheme=='finger' else b'request\r\n' if scheme in ['nc','tcp'] else b'/path/\r\n'
            check(stream.received[-1]==expected,scheme+' sends the exact selector/request')
            if scheme=='gopher':check(r['body']=='1Menu\t/\thost\t70\r\n.literal\r\n','Gopher removes framing and unescapes dot-stuffed text')
            if scheme in ['nc','tcp']:
                r=run({'op':'push','read':url,'url':url,'body':'Sent 世界\r\n'})
                check(r['ok'] and not r['verified'] and stream.received[-1]=='Sent 世界\r\n'.encode(),scheme+' Push sends the document once without claiming file-save confirmation')
            if scheme=='finger':
                before=len(stream.received);r=run({'op':'push','read':url,'url':url,'body':'x'})
                check(not r['ok'] and len(stream.received)==before,'Finger is retrieval-only and never receives a document upload')
        telnet=Stream('telnet');streams.append(telnet)
        r=run({'op':'fetch','url':f'telnet://127.0.0.1:{telnet.port}/'})
        check(r['ok'] and r['body']=='Banner 世界\r\n','Telnet strips negotiations and preserves document text')
        check(telnet.received and bytes([255,252,1]) in telnet.received[0],'Telnet declines unsupported options without terminal side effects')
        sender=Stream('telnet-send');streams.append(sender)
        uri=f'telnet://127.0.0.1:{sender.port}/'
        r=run({'op':'push','url':uri,'read':uri,'body':'Sent 世界\r\n'})
        check(r['ok'] and not r['verified'] and sender.received[0].startswith('Sent 世界\r\n'.encode()),'Telnet Push sends text, negotiates options and leaves file-save state unconfirmed')
        check(not run({'op':'fetch','url':f'finger://127.0.0.1:{telnet.port}/alice%0D%0Asecond'})['ok'],'Finger rejects multiline query injection')
        nvt=Stream('telnet-nvt');streams.append(nvt)
        r=run({'op':'fetch','url':f'telnet://127.0.0.1:{nvt.port}/'})
        check(r['ok'] and r['body']=='Banner\rWorld\r\n','Telnet CR-NUL is decoded into text rather than rejected as binary')
        nvt_sender=Stream('telnet-send');streams.append(nvt_sender)
        uri=f'telnet://127.0.0.1:{nvt_sender.port}/'
        r=run({'op':'push','url':uri,'read':uri,'body':'Sent 世界\n'})
        check(r['ok'] and nvt_sender.received[0].startswith('Sent 世界\r\n'.encode()),'Telnet sends LF lines as NVT CRLF')

        slow=Stream('slow');streams.append(slow)
        r=run({'op':'fetch','url':f'nc://127.0.0.1:{slow.port}/','cancel_ms':50})
        check(not r['ok'] and 'canceled' in r['error'],'raw TCP cancellation wakes a blocked read')
        check(all(not s.errors for s in streams),'protocol fixture threads complete without errors')
    finally:
        server.shutdown();server.server_close()
        for s in streams:s.close()
    ftp_checks()
    ssh_checks()
    print('PASS',len(checks),'remote file and stream checks')
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='xnedit-transfer-home-') as home:
        os.environ['HOME']=home
        main()
