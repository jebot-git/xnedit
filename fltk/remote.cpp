// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote.h"
#include "remote-files.h"
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
namespace smallnet {
namespace {
using Clock = std::chrono::steady_clock;
template<class T, void (*Free)(T *)> using Ossl = std::unique_ptr<T, decltype(Free)>;
std::string encode(const std::string &s) {
  const char *hex = "0123456789ABCDEF";
  std::string out;
  for (size_t i=0;i<s.size();++i) {
    unsigned char c=s[i];
    if (c=='%') {
      if (i+2>=s.size() || !g_ascii_isxdigit(s[i+1]) || !g_ascii_isxdigit(s[i+2]))
        throw std::runtime_error("Malformed URL escape");
      out.append(s,i,3);i+=2;
    } else if (c<128 && (g_ascii_isalnum(c) || strchr("-._~/:@!$&'()*+,=?",c))) out+=c;
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
void cancelled(const Session &s) {
  if (s.cancel && g_cancellable_is_cancelled(s.cancel)) throw std::runtime_error("Transfer canceled");
}
std::string readFile(const std::string &path,size_t limit,bool secret=false) {
  struct stat st{};
  if (stat(path.c_str(),&st) || !S_ISREG(st.st_mode) || st.st_size<0 || uint64_t(st.st_size)>limit)
    throw std::runtime_error("Cannot read identity file, or file exceeds its size limit");
  if (secret && (st.st_uid!=getuid() || (st.st_mode&0077)))
    throw std::runtime_error("Identity keys and Penguin Mail storage must be owned by you and have owner-only permissions");
  std::ifstream f(path,std::ios::binary);
  if (!f) throw std::runtime_error("Cannot open identity file");
  std::string data((std::istreambuf_iterator<char>(f)),{});
  if (data.size()>limit) throw std::runtime_error("Identity file exceeds its size limit");
  return data;
}
nlohmann::json store(const std::string &path) {
  try { return nlohmann::json::parse(readFile(path,8*1024*1024,true)); }
  catch (const nlohmann::json::exception &) { throw std::runtime_error("Invalid Penguin Mail identity storage"); }
}
int noPassword(char *,int,int,void *) {return 0;}
void identity(SSL_CTX *ctx,const Identity &id) {
  if (id.empty()) return;
  std::string cert,key;
  if (!id.penguinId.empty()) {
    auto state=store(id.penguinStore);
    bool found=false;
    for (const auto &record:state.at("identities")) if (record.at("id")==id.penguinId) {
      cert=record.at("certificate").get<std::string>();key=record.at("key").get<std::string>();found=true;break;
    }
    if (!found) throw std::runtime_error("Selected Penguin Mail identity is no longer available");
  } else {
    if (id.certificate.empty() || id.key.empty()) throw std::runtime_error("Select both the identity certificate and its private key");
    cert=readFile(id.certificate,65536);key=readFile(id.key,65536,true);
  }
  if (cert.size()>65536 || key.size()>65536) throw std::runtime_error("Identity exceeds 64 KiB");
  Ossl<BIO,BIO_free_all> cb(BIO_new_mem_buf(cert.data(),int(cert.size())),BIO_free_all);
  Ossl<BIO,BIO_free_all> kb(BIO_new_mem_buf(key.data(),int(key.size())),BIO_free_all);
  Ossl<X509,X509_free> certificate(PEM_read_bio_X509(cb.get(),nullptr,nullptr,nullptr),X509_free);
  Ossl<EVP_PKEY,EVP_PKEY_free> privateKey(PEM_read_bio_PrivateKey(kb.get(),nullptr,noPassword,nullptr),EVP_PKEY_free);
  if (!certificate || !privateKey || X509_check_private_key(certificate.get(),privateKey.get())!=1)
    throw std::runtime_error("Identity certificate/key mismatch, invalid PEM, or encrypted private key");
  if (SSL_CTX_use_certificate(ctx,certificate.get())!=1 || SSL_CTX_use_PrivateKey(ctx,privateKey.get())!=1)
    throw std::runtime_error("Cannot load client identity");
}
struct Connection {
  const Session &session;
  Clock::time_point deadline;
  GSocketConnection *tcp=nullptr;
  Ossl<SSL_CTX,SSL_CTX_free> context{nullptr,SSL_CTX_free};
  Ossl<SSL,SSL_free> tls{nullptr,SSL_free};
  int fd=-1;
  bool plain=false;
  ~Connection() {tls.reset();if(tcp)g_object_unref(tcp);}
  void wait(int error) {
    if (error!=SSL_ERROR_WANT_READ && error!=SSL_ERROR_WANT_WRITE)
      throw std::runtime_error("TLS connection failed or was closed unexpectedly");
    while (true) {
      cancelled(session);
      if (Clock::now()>=deadline) throw std::runtime_error("Remote transfer timed out");
      pollfd p{fd,short(error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT),0};
      int n=::poll(&p,1,100);
      if (n>0) return;
      if (n<0 && errno!=EINTR) throw std::runtime_error("Network polling failed");
    }
  }
  Connection(const Url &url,const Session &s):session(s),deadline(Clock::now()+std::chrono::seconds(s.timeoutSeconds)) {
    static std::once_flag initialized;
    std::call_once(initialized,[]{std::signal(SIGPIPE,SIG_IGN);});
    cancelled(s);
    std::unique_ptr<GSocketClient,decltype(&g_object_unref)> client(g_socket_client_new(),g_object_unref);
    g_socket_client_set_enable_proxy(client.get(),FALSE);
    g_socket_client_set_timeout(client.get(),std::max(1,s.timeoutSeconds));
    GError *error=nullptr;
    tcp=g_socket_client_connect_to_host(client.get(),url.host.c_str(),url.port,s.cancel,&error);
    if (!tcp) {
      if(error)g_error_free(error);
      cancelled(s);throw std::runtime_error("Cannot connect to "+url.peer());
    }
    // A constructor exception must also release the established TCP stream.
    try {
      fd=g_socket_get_fd(g_socket_connection_get_socket(tcp));
      if (fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0) throw std::runtime_error("Cannot configure connection");
      plain = url.scheme != "gemini" && url.scheme != "titan" && url.scheme != "scroll";
      if (plain) return;
      context.reset(SSL_CTX_new(TLS_client_method()));
      if(!context || !SSL_CTX_set_min_proto_version(context.get(),TLS1_2_VERSION)) throw std::runtime_error("Cannot initialize TLS");
      SSL_CTX_set_verify(context.get(),SSL_VERIFY_NONE,nullptr); // Gemini TOFU is checked below before application bytes.
      SSL_CTX_set_options(context.get(),SSL_OP_IGNORE_UNEXPECTED_EOF);
      identity(context.get(),s.identity);
      tls.reset(SSL_new(context.get()));
      if (!tls || !SSL_set_fd(tls.get(),fd)) throw std::runtime_error("Cannot initialize TLS stream");
      if (!g_hostname_is_ip_address(url.host.c_str())) SSL_set_tlsext_host_name(tls.get(),url.host.c_str());
      for(;;){cancelled(s);int n=SSL_connect(tls.get());if(n==1)break;wait(SSL_get_error(tls.get(),n));}
      Ossl<X509,X509_free> cert(SSL_get1_peer_certificate(tls.get()),X509_free);
      if (!cert) throw std::runtime_error("Server supplied no certificate");
      if (X509_cmp_current_time(X509_get0_notBefore(cert.get()))>=0 || X509_cmp_current_time(X509_get0_notAfter(cert.get()))<=0)
        throw std::runtime_error("Server certificate is not currently valid");
      int names=g_hostname_is_ip_address(url.host.c_str())?X509_check_ip_asc(cert.get(),url.host.c_str(),0):X509_check_host(cert.get(),url.host.c_str(),0,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS,nullptr);
      if(names!=1)throw std::runtime_error("Server certificate does not name "+url.host);
      unsigned char digest[EVP_MAX_MD_SIZE];unsigned int count=0;
      if(!X509_digest(cert.get(),EVP_sha256(),digest,&count))throw std::runtime_error("Cannot fingerprint server certificate");
      std::string fingerprint;const char *hex="0123456789abcdef";
      for(unsigned int i=0;i<count;++i){fingerprint+=hex[digest[i]>>4];fingerprint+=hex[digest[i]&15];}
      auto pin=s.pins.find(url.peer());
      if(pin==s.pins.end() || pin->second!=fingerprint) {
        char subject[1024]{};X509_NAME_oneline(X509_get_subject_name(cert.get()),subject,sizeof subject);
        throw TrustRequired(url.peer(),fingerprint,pin==s.pins.end()?"":pin->second,subject);
      }
    } catch (...) {tls.reset();g_object_unref(tcp);tcp=nullptr;throw;}
  }
  void write(const std::string &bytes) {
    size_t pos=0;
    while(pos<bytes.size()) {
      cancelled(session);if(Clock::now()>=deadline)throw std::runtime_error("Remote transfer timed out");
      int n=plain ? int(::send(fd,bytes.data()+pos,std::min(bytes.size()-pos,size_t(16384)),MSG_NOSIGNAL))
                  : SSL_write(tls.get(),bytes.data()+pos,int(std::min(bytes.size()-pos,size_t(16384))));
      if(n>0)pos+=n;
      else if(!plain)wait(SSL_get_error(tls.get(),n));
      else if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))wait(SSL_ERROR_WANT_WRITE);
      else if(n<0 && errno==EINTR)continue;
      else throw std::runtime_error("Remote connection closed during upload");
    }
  }
  int read(char *out,int limit) {
    for(;;){cancelled(session);if(Clock::now()>=deadline)throw std::runtime_error("Remote transfer timed out");
      int n=plain ? int(::recv(fd,out,limit,0)) : SSL_read(tls.get(),out,limit);
      if(n>0)return n;
      if(plain){
        if(n==0)return 0;
        if(errno==EINTR)continue;
        if(errno==EAGAIN || errno==EWOULDBLOCK){wait(SSL_ERROR_WANT_READ);continue;}
        throw std::runtime_error("Remote connection failed while receiving data");
      }
      int e=SSL_get_error(tls.get(),n);if(e==SSL_ERROR_ZERO_RETURN)return 0;wait(e);}
  }
  std::string metadataLine() {
    std::string value;char c;
    while(value.size()<1024) {
      if(!read(&c,1))throw std::runtime_error("Incomplete Scroll metadata");
      value+=c;
      if(value.size()>=2 && value.substr(value.size()-2)=="\r\n") {
        value.resize(value.size()-2);
        if(value.find_first_of("\r\n")!=value.npos || value.find('\0')!=value.npos || !g_utf8_validate(value.data(),value.size(),nullptr))throw std::runtime_error("Invalid Scroll metadata");
        return value;
      }
    }
    throw std::runtime_error("Scroll metadata line exceeds 1024 bytes");
  }
  Response response(bool body,bool scroll=false) {
    Response r;std::string header;char c;
    while(header.size()<1024) {if(!read(&c,1))throw std::runtime_error("Incomplete response header");header+=c;if(header.size()>=2 && header.substr(header.size()-2)=="\r\n")break;}
    const size_t digits=plain?1:2;
    bool valid=header.size()>=digits+3 && header.substr(header.size()-2)=="\r\n" && header[digits]==' ';
    if(plain)valid=valid && header[0]>='2' && header[0]<='5';
    else valid=valid && g_ascii_isdigit(header[0]) && g_ascii_isdigit(header[1]);
    if(!valid)throw std::runtime_error(plain?"Invalid or oversized Spartan response header":"Invalid or oversized Gemini response header");
    r.status=plain?header[0]-'0':(header[0]-'0')*10+header[1]-'0';r.meta=header.substr(digits+1,header.size()-digits-3);
    if(r.meta.find_first_of("\r\n")!=std::string::npos || r.meta.find('\0')!=std::string::npos || !g_utf8_validate(r.meta.data(),r.meta.size(),nullptr))throw std::runtime_error("Invalid response metadata");
    if(plain){
      for(unsigned char c:r.meta)if((c<32 && c!='\t')||c>=127)throw std::runtime_error("Spartan response metadata must be ASCII");
      if(r.status==2 && r.meta.empty())throw std::runtime_error("Spartan success requires a MIME type");
    }
    if(scroll && r.success()){r.author=metadataLine();r.published=metadataLine();r.modified=metadataLine();}
    if(body && r.success()) {char data[16384];for(;;){int n=read(data,sizeof data);if(!n)break;if(r.body.size()+n>maxBytes)throw std::runtime_error("Remote document exceeds 8 MiB");r.body.append(data,n);}}
    return r;
  }
};
bool waitForRateLimit(const Response &response,const Session &session) {
  if(response.status!=44)return false;
  auto text=response.meta.substr(0,response.meta.find(' '));
  if(text.empty() || text.size()>2 || !std::all_of(text.begin(),text.end(),[](unsigned char c){return g_ascii_isdigit(c);}))return false;
  unsigned seconds=unsigned(std::stoul(text));
  if(seconds>5)return false;
  auto until=Clock::now()+std::chrono::milliseconds(std::max(100u,seconds*1000));
  while(Clock::now()<until){cancelled(session);::poll(nullptr,0,50);}
  return true;
}
std::string spartanHeader(const Url &url,size_t bytes) {
  if(!url.query.empty())throw std::runtime_error("Spartan editing URLs cannot contain queries; queries represent upload data");
  auto host=url.host.find(':')==std::string::npos?url.host:"["+url.host+"]";
  auto header=host+" "+url.path+" "+std::to_string(bytes)+"\r\n";
  if(header.size()>1024)throw std::runtime_error("Spartan request header exceeds 1024 bytes");
  return header;
}
Url redirect(const Url &url,const std::string &location) {
  bool spartan=url.scheme=="spartan";
  if(spartan && (location.empty() || location[0]!='/' || location.rfind("//",0)==0))
    throw std::runtime_error("Spartan redirects must use an absolute path on the same server");
  GError *error=nullptr;char *resolved=g_uri_resolve_relative(url.str().c_str(),location.c_str(),G_URI_FLAGS_ENCODED,&error);
  if(!resolved){if(error)g_error_free(error);throw std::runtime_error("Invalid remote redirect");}
  std::string text=resolved;g_free(resolved);auto next=parse(text);
  if(next.scheme!=url.scheme || next.peer()!=url.peer())throw std::runtime_error("Cross-server redirect blocked; open the destination explicitly");
  if(spartan && !next.query.empty())throw std::runtime_error("Spartan retrieval redirect cannot supply upload data");
  return next;
}
std::string decoded(const std::string &value) {
  char *p=g_uri_unescape_string(value.c_str(),nullptr);
  if(!p)throw std::runtime_error("Invalid URL escape or NUL byte");
  std::string out=p;g_free(p);return out;
}
Response streamTransfer(const Url &url,const Session &s,const std::string *upload) {
  bool telnet=url.scheme=="telnet",gopher=url.scheme=="gopher" || url.scheme=="gopher+",plus=url.scheme=="gopher+",attributes=plus;
  std::string request;
  if(upload)request=*upload;
  else if(streamProtocol(url))request=decoded(url.path.substr(1));
  else if(url.scheme=="finger")request=decoded(url.path=="/"?url.user:url.path.substr(1))+"\r\n";
  else if(gopher) {
    auto selector=url.path;
    if(selector.size()>1 && std::string("017").find(selector[1])!=std::string::npos)selector=selector.substr(2);
    selector=decoded(selector);
    if(selector=="/")selector.clear();
    if(plus && selector.find('\t')==selector.npos)selector+="\t!";
    auto last=selector.rfind('\t');
    if(last!=selector.npos && last+1<selector.size()) {
      char command=selector[last+1];
      if(command=='!' || command=='$' || command=='+' || command=='?')plus=true;
      attributes=command=='!' || command=='$';
    }
    request=selector+"\r\n";
  } else request=decoded(url.path)+"\r\n";
  if(request.size()>maxBytes)throw std::runtime_error("Stream request exceeds 8 MiB");
  if(!streamProtocol(url))for(unsigned char c:request.substr(0,request.size()-2))
    if(c=='\r' || c=='\n' || c==0)throw std::runtime_error("The selector/query must be a single line");
  Connection connection(url,s);
  if(telnet) {
    std::string escaped;
    for(size_t i=0;i<request.size();++i) {
      unsigned char c=request[i];
      if(c=='\n' && (i==0 || request[i-1]!='\r'))escaped+='\r';
      escaped+=char(c);if(c==255)escaped+=char(c);
      if(c=='\r' && (i+1==request.size() || request[i+1]!='\n'))escaped+='\0';
    }
    connection.write(escaped);
  } else {
    connection.write(request);
    if(::shutdown(connection.fd,SHUT_WR))throw std::runtime_error("Cannot finish the outgoing stream");
  }
  Response result{20,"text/plain","",url.str()};
  if(url.scheme=="gopher" && url.path.size()>1 && (url.path[1]=='1' || url.path[1]=='7'))result.meta="text/x-gophermap";
  if(attributes)result.meta="text/x-gopherplus";
  if(url.scheme=="nex" && url.path.back()=='/')result.meta="text/x-nex";
  enum {Data, Iac, Option, Sub, SubIac} state=Data;
  unsigned char command=0;bool afterCr=false;
  char block[16384];
  for(;;) {
    int count=connection.read(block,sizeof block);if(!count)break;
    for(int i=0;i<count;++i) {
      unsigned char c=block[i];bool literal=!telnet;
      if(telnet)switch(state) {
        case Data:if(c==255)state=Iac;else literal=true;break;
        case Iac:
          if(c==255){literal=true;state=Data;}
          else if(c==251 || c==252 || c==253 || c==254){command=c;state=Option;}
          else state=c==250?Sub:Data;
          break;
        case Option:
          if(command==251 || command==253)connection.write(std::string{char(255),char(command==251?254:252),char(c)});
          state=Data;break;
        case Sub:if(c==255)state=SubIac;break;
        case SubIac:state=c==240?Data:Sub;break;
      }
      if(literal) {
        if(telnet && c==0 && afterCr){afterCr=false;continue;}
        afterCr=c=='\r';
        if(result.body.size()>=maxBytes)throw std::runtime_error("Remote document exceeds 8 MiB");
        result.body+=char(c);
      }
    }
  }
  if(telnet && state!=Data)throw std::runtime_error("Truncated Telnet negotiation");
  if(gopher) {
    bool dotTerminated=true,requireTerminator=false;
    if(plus) {
      auto end=result.body.find("\r\n");
      if(end==result.body.npos)throw std::runtime_error("Incomplete Gopher+ response");
      auto header=result.body.substr(0,end);
      bool framed=header.size()>1 && (header[0]=='+' || header[0]=='-') &&
          std::all_of(header.begin()+1,header.end(),[](unsigned char c){return g_ascii_isdigit(c) || c=='-';});
      if(framed) {
        long count;
        try{size_t used=0;count=std::stol(header.substr(1),&used);if(used!=header.size()-1)throw std::runtime_error("Invalid Gopher+ length");}
        catch(...){throw std::runtime_error("Invalid Gopher+ length");}
        result.body.erase(0,end+2);
        if(count>=0) {
          if(size_t(count)>maxBytes)throw std::runtime_error("Remote document exceeds 8 MiB");
          if(result.body.size()<size_t(count))throw std::runtime_error("Truncated Gopher+ response");
          result.body.resize(count);dotTerminated=false;
        } else if(count==-2)dotTerminated=false;
        else if(count==-1)requireTerminator=true;
        else throw std::runtime_error("Invalid Gopher+ framing");
        if(header[0]=='-'){result.status=50;result.meta="Gopher+ request failed";}
      } else if(header.rfind("+INFO:",0)!=0)throw std::runtime_error("Invalid Gopher+ attribute response");
    }
    if(dotTerminated) {
      std::string text;bool terminated=false;
      for(size_t i=0;i<result.body.size();) {
        auto end=result.body.find('\n',i);auto next=end==std::string::npos?result.body.size():end+1;
        auto line=result.body.substr(i,next-i);
        if(line==".\r\n" || line==".\n" || line=="."){terminated=true;break;}
        if(line.rfind("..",0)==0)line.erase(0,1);
        text+=line;i=next;
      }
      if(requireTerminator && !terminated)throw std::runtime_error("Truncated Gopher+ period-terminated response");
      result.body=std::move(text);
    }
  }
  return result;
}
std::string scrollLanguages(const Session &session) {
  std::string list=session.languages;
  if(list.empty()) {
    const char *const *names=g_get_language_names();
    std::vector<std::string> seen;
    for(size_t i=0;names[i] && seen.size()<8;++i) {
      std::string name=names[i];name=name.substr(0,name.find_first_of(".@"));
      std::replace(name.begin(),name.end(),'_','-');
      if(name=="C" || name=="POSIX" || name.empty() || std::find(seen.begin(),seen.end(),name)!=seen.end())continue;
      if(name.size()>64 || !std::all_of(name.begin(),name.end(),[](unsigned char c){return g_ascii_isalnum(c) || c=='-';}))continue;
      seen.push_back(name);if(!list.empty())list+=',';list+=name;
    }
    if(list.empty())list="en";
  }
  if(list.size()>512 || list.front()==',' || list.back()==',' || list.find(",,")!=list.npos || !std::all_of(list.begin(),list.end(),[](unsigned char c){return g_ascii_isalnum(c) || c=='-' || c==',';}))throw std::runtime_error("Invalid Scroll language list");
  return list;
}
Response get(Url url,const Session &s) {
  for(int redirects=0;redirects<6;++redirects) {
    const bool spartan=url.scheme=="spartan";
    auto header=spartan?spartanHeader(url,0):url.str()+(url.scheme=="scroll"?" "+scrollLanguages(s):"")+"\r\n";
    cancelled(s);Connection c(url,s);c.write(header);auto r=c.response(true,url.scheme=="scroll");r.url=url.str();
    if(!spartan && redirects==0 && waitForRateLimit(r,s))continue;
    if(r.status!=3 && r.status!=30 && r.status!=31)return r;
    url=redirect(url,r.meta);
  }
  throw std::runtime_error("Too many remote redirects");
}
}
int defaultPort(const std::string &scheme) {
  static const std::map<std::string,int> ports={{"gemini",1965},{"titan",1965},{"spartan",300},
    {"scp",22},{"sftp",22},{"ssh",22},{"finger",79},{"telnet",23},{"nc",0},{"tcp",0},
    {"gopher",70},{"gopher+",70},{"scroll",5699},{"nex",1900},{"http",80},{"https",443},{"ftp",21},{"ftps",990}};
  auto found=ports.find(scheme);return found==ports.end()?-1:found->second;
}
bool streamProtocol(const Url &u) {return u.scheme=="nc" || u.scheme=="tcp" || u.scheme=="telnet";}
bool canRetrieve(const Url &u) {return defaultPort(u.scheme)>=0 && u.scheme!="titan";}
bool canPush(const Url &u) {return u.scheme=="titan" || u.scheme=="spartan" || streamProtocol(u) ||
    u.scheme=="scp" || u.scheme=="sftp" || u.scheme=="ssh" || u.scheme=="ftp" || u.scheme=="ftps" || u.scheme=="http" || u.scheme=="https";}
std::string uploadUrl(const Url &u) {auto target=u.writing();return canPush(target)?target.str():"";}
std::string Url::peer()const{return (host.find(':')==std::string::npos?host:"["+host+"]")+":"+std::to_string(port);}
std::string Url::str()const{return scheme+"://"+(user.empty()?"":user+"@")+(host.find(':')==std::string::npos?host:"["+host+"]")+(port==defaultPort(scheme)?"":":"+std::to_string(port))+path+(query.empty()?"":"?"+query);}
Url Url::withScheme(const std::string &s)const{auto copy=*this;copy.scheme=s;return copy;}
Url parse(const std::string &text) {
  if(text.empty() || text.find('\0')!=std::string::npos || !g_utf8_validate(text.data(),text.size(),nullptr))throw std::runtime_error("Invalid remote URL");
  for(unsigned char c:text)if(c<=32 || c==127)throw std::runtime_error("URL whitespace and controls must be percent encoded");
  GError *error=nullptr;GUri *raw=g_uri_parse(text.c_str(),G_URI_FLAGS_ENCODED,&error);
  if(!raw){if(error)g_error_free(error);throw std::runtime_error("Invalid remote URL");}
  std::unique_ptr<GUri,decltype(&g_uri_unref)> uri(raw,g_uri_unref);
  const char *scheme=g_uri_get_scheme(raw),*host=g_uri_get_host(raw);
  bool spartan=scheme && !strcmp(scheme,"spartan");
  if(!scheme || defaultPort(scheme)<0 || !host || !*host)throw std::runtime_error("Unsupported remote protocol or missing hostname");
  if(!spartan && g_uri_get_fragment(raw))throw std::runtime_error("Remote URLs cannot contain fragments");
  char *ascii=g_hostname_to_ascii(host);if(!ascii)throw std::runtime_error("Invalid server hostname");
  Url out;out.scheme=scheme;out.host=ascii;g_free(ascii);std::transform(out.host.begin(),out.host.end(),out.host.begin(),g_ascii_tolower);
  for(unsigned char c:out.host)if(c<=32 || c>=127 || strchr("/@;?#\\",c))throw std::runtime_error("Invalid server hostname");
  if(out.host.find('%')!=std::string::npos)throw std::runtime_error("IPv6 zone identifiers are not supported");
  if(const char *user=g_uri_get_userinfo(raw);user && !spartan) {
    if(out.scheme!="scp" && out.scheme!="sftp" && out.scheme!="ssh" && out.scheme!="ftp" && out.scheme!="ftps" && out.scheme!="http" && out.scheme!="https" && out.scheme!="finger")
      throw std::runtime_error("This protocol does not accept user information");
    char *decoded=g_uri_unescape_string(user,nullptr);if(!decoded)throw std::runtime_error("Invalid username");
    std::string name=decoded;g_free(decoded);
    for(unsigned char c:name)if(c<=32 || c>=127 || strchr(":/@?#\\",c))throw std::runtime_error("Use a username without a password in the URL; use SSH keys/agent or a private .netrc for authentication");
    out.user=encode(name);
  }
  int port=g_uri_get_port(raw);out.port=port==-1?defaultPort(out.scheme):port;
  if(out.port<1 || out.port>65535)throw std::runtime_error("Specify a valid port (nc/tcp have no default port)");
  out.path=encode(g_uri_get_path(raw)?g_uri_get_path(raw):"");if(out.path.empty())out.path="/";
  out.query=encode(g_uri_get_query(raw)?g_uri_get_query(raw):"");
  if(out.scheme!="gemini" && out.scheme!="titan" && out.scheme!="scroll" && out.scheme!="http" && out.scheme!="https" && !out.query.empty())throw std::runtime_error("This editing protocol does not accept URL query parameters");
  if(out.str().size()+2>size_t(out.scheme=="gemini" || out.scheme=="titan"?1024:16384))throw std::runtime_error("Remote URL exceeds its request limit");
  return out;
}
std::string textMime(const std::string &metadata) {
  std::string lower=metadata;std::transform(lower.begin(),lower.end(),lower.begin(),g_ascii_tolower);
  auto trim=[](std::string s){auto a=s.find_first_not_of(" \t");if(a==std::string::npos)return std::string();return s.substr(a,s.find_last_not_of(" \t")-a+1);};
  auto type=trim(lower.substr(0,lower.find(';')));if(type.empty())type="text/gemini";
  if(type.rfind("text/",0)!=0 && type!="application/json" && type!="application/xml" && type!="application/javascript" && type!="application/yaml" && type!="application/toml" && !(type.size()>5 && type.substr(type.size()-5)=="+json"))throw std::runtime_error("The remote resource is not a text document");
  for(size_t pos=lower.find(';');pos!=std::string::npos;) {
    auto end=lower.find(';',pos+1);auto field=trim(lower.substr(pos+1,end==std::string::npos?end:end-pos-1));
    auto equals=field.find('=');
    if(equals!=std::string::npos && trim(field.substr(0,equals))=="charset") {
      auto charset=trim(field.substr(equals+1));if(charset.size()>1 && charset.front()=='"' && charset.back()=='"')charset=charset.substr(1,charset.size()-2);
      if(charset!="utf-8" && charset!="utf8" && charset!="us-ascii")throw std::runtime_error("Remote editing requires UTF-8 or ASCII text; the server declares a different charset");
    }
    pos=end;
  }
  return type;
}
std::string hash(const std::string &s) {char *p=g_compute_checksum_for_data(G_CHECKSUM_SHA256,reinterpret_cast<const guchar *>(s.data()),s.size());std::string r=p;g_free(p);return r;}
TrustRequired::TrustRequired(std::string p,std::string f,std::string prev,std::string name):std::runtime_error("Server certificate requires confirmation"),peer(std::move(p)),fingerprint(std::move(f)),previous(std::move(prev)),subject(std::move(name)){}
std::vector<std::pair<std::string,std::string>> identities(const std::string &path) {
  std::vector<std::pair<std::string,std::string>> out;
  try {auto state=store(path);for(const auto &record:state.at("identities"))out.emplace_back(record.at("id").get<std::string>(),record.at("address").get<std::string>());}
  catch(const nlohmann::json::exception &){throw std::runtime_error("Invalid Penguin Mail identities");}return out;
}
Response fetch(const std::string &url,const Session &s) {
  auto parsed=parse(url);if(!canRetrieve(parsed))throw std::runtime_error("This URL is upload-only");
  if(parsed.scheme=="gemini" || parsed.scheme=="spartan" || parsed.scheme=="scroll")return get(parsed,s);
  if(streamProtocol(parsed) || parsed.scheme=="finger" || parsed.scheme=="gopher" || parsed.scheme=="gopher+" || parsed.scheme=="nex")return streamTransfer(parsed,s,nullptr);
  return fileFetch(parsed,s);
}
Published push(const std::string &readUrl,const std::string &titanUrl,const std::string &baseline,const std::string &body,const std::string &mime,const std::string &token,const Session &s,bool overwrite) {
  auto target=parse(titanUrl),read=parse(readUrl);
  if(target.scheme!="titan" && target.scheme!="spartan") {
    if(!canPush(target))throw std::runtime_error("This protocol only retrieves text; choose another upload URL or Save as locally");
    if(body.size()>maxBytes)throw std::runtime_error("Push exceeds 8 MiB");
    if(!token.empty())throw std::runtime_error("Titan tokens cannot be sent to other protocols");
    if(streamProtocol(target)) {auto response=streamTransfer(target,s,&body);return {read.str(),baseline,response,false};}
    return filePush(read,target,baseline,body,mime,s,overwrite);
  }
  const bool spartan=target.scheme=="spartan";
  if((spartan?read.scheme!="spartan":target.scheme!="titan" || read.scheme!="gemini") || target.peer()!=read.peer())
    throw std::runtime_error("The read and upload URLs must use the same protocol and server");
  if(body.size()>maxBytes)throw std::runtime_error("Push exceeds 8 MiB");
  if(spartan && body.empty())throw std::runtime_error("Spartan uploads cannot be empty; zero-length requests retrieve a resource");
  std::string header;
  if(spartan){
    if(!token.empty())throw std::runtime_error("Spartan uploads do not accept Titan tokens");
    spartanHeader(read,0);header=spartanHeader(target,body.size());
  } else {
    if(mime.empty() || mime.find('/')==std::string::npos || mime.size()>128)throw std::runtime_error("Enter a MIME type such as text/gemini");
    for(unsigned char c:mime)if(c<=32 || c>=127 || c==';')throw std::runtime_error("Invalid Titan MIME type");
    for(unsigned char c:token)if(c<=32 || c>=127 || c==';')throw std::runtime_error("Titan token must be printable ASCII without whitespace or semicolons");
    header=target.str()+";size="+std::to_string(body.size())+";mime="+mime+(token.empty()?"":";token="+token)+"\r\n";
    if(header.size()>1024)throw std::runtime_error("Titan request header exceeds 1024 bytes");
  }
  auto before=get(read,s);auto digest=hash(body);
  if(before.success()) {
    if(hash(before.body)==digest)return {before.url,digest,{spartan?2:20,"Already matches remote contents","",before.url}};
    if(!overwrite && (baseline.empty() || hash(before.body)!=baseline))throw Conflict("The remote page changed or already exists. Your edits have not been uploaded.");
  } else if(spartan && before.status==4){
    if(!overwrite)throw Conflict("Spartan could not retrieve the target. Confirm creation or replacement explicitly before uploading.");
  } else if(before.status!=51)throw std::runtime_error("Cannot check remote contents before push: "+std::to_string(before.status)+" "+before.meta);
  else if(!baseline.empty() && !overwrite)throw Conflict("The remote page was removed. Your edits have not been uploaded.");
  Response result;
  for(int attempt=0;attempt<2;++attempt) {
    Connection connection(target,s);
    try {connection.write(header);connection.write(body);result=connection.response(false);}
    catch(const std::exception &){throw std::runtime_error("Push was not confirmed and may have arrived. Retrieve the remote page before retrying.");}
    // Status 44 explicitly declines this attempt. Retry only this response,
    // never a missing/ambiguous acknowledgement, and honor the bounded delay.
    if(!spartan && attempt==0 && waitForRateLimit(result,s))continue;
    break;
  }
  if(result.success())return {read.str(),digest,result};
  if(result.status==3 || result.status==30 || result.status==31) {
    try {
      auto check=redirect(read,result.meta);auto confirmed=get(check,s);
      if(confirmed.success() && hash(confirmed.body)==digest)return {confirmed.url,digest,result};
    }catch(const std::exception &){}
    throw std::runtime_error("The server returned a redirect, but publication could not be verified. Check the destination before retrying.");
  }
  throw std::runtime_error(std::string(spartan?"Spartan":"Titan")+" declined the push: "+std::to_string(result.status)+" "+result.meta);
}
}
