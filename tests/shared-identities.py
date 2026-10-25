#!/usr/bin/env python3
"""Validate catalog adoption in private files without contacting any server."""
import hashlib,json,os,pathlib,ssl,subprocess,sys,tempfile
binary=pathlib.Path(sys.argv[1]).resolve()
def run(op,**fields):
    return json.loads(subprocess.check_output([str(binary)],input=json.dumps(dict(op=op,**fields)),text=True))
with tempfile.TemporaryDirectory(prefix="xnedit-identities-") as folder:
    root=pathlib.Path(folder); cert=root/"client.pem"; key=root/"client.key"; catalog=root/"index.json"
    subprocess.run(["openssl","req","-x509","-newkey","rsa:2048","-nodes","-days","1","-subj","/CN=Generic Gemini fixture","-keyout",str(key),"-out",str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    os.chmod(key,0o600)
    fingerprint=hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
    document={"version":1,"identities":[{"id":fingerprint,"name":"Servo fixture 世界","certificate":str(cert),"key":str(key)}]}
    def save():
        catalog.write_text(json.dumps(document));os.chmod(catalog,0o600)
    save();before=key.read_bytes()
    assert run("shared-identities",catalog=str(catalog))["identities"]==[[fingerprint,"Servo fixture 世界"]]
    assert run("adopt-shared",catalog=str(catalog),identity=fingerprint)==dict(ok=True,certificate=str(cert),key=str(key))
    assert key.read_bytes()==before
    os.chmod(key,0o644)
    assert not run("adopt-shared",catalog=str(catalog),identity=fingerprint)["ok"]
    os.chmod(key,0o600);document["identities"][0]["id"]="0"*64;save()
    assert run("shared-identities",catalog=str(catalog))["identities"]==[]
    assert not run("adopt-shared",catalog=str(catalog),identity="0"*64)["ok"]
    document["identities"][0]["key"]="relative.key";save()
    assert not run("shared-identities",catalog=str(catalog))["ok"]
    document["identities"][0]["key"]=str(key);save();os.chmod(catalog,0o644)
    assert not run("shared-identities",catalog=str(catalog))["ok"]
    print("PASS shared identity catalog selection, generic certificate, fingerprint validation, private-file enforcement, path validation, and unchanged source key")
