// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote-files.h"
#include <curl/curl.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
namespace smallnet {
namespace {
struct Transfer {
  const Session &session;
  std::string output, input;
  size_t position=0;
  bool oversized=false;
  static size_t receive(char *data,size_t size,size_t count,void *context) {
    auto &t=*static_cast<Transfer *>(context);
    if(size && count>maxBytes/size){t.oversized=true;return 0;}
    size_t bytes=size*count;
    if(bytes>maxBytes-t.output.size()){t.oversized=true;return 0;}
    try {t.output.append(data,bytes);return bytes;}catch(...){return 0;}
  }
  static size_t send(char *data,size_t size,size_t count,void *context) {
    auto &t=*static_cast<Transfer *>(context);
    if(t.session.cancel && g_cancellable_is_cancelled(t.session.cancel))return CURL_READFUNC_ABORT;
    size_t bytes=std::min(size*count,t.input.size()-t.position);
    memcpy(data,t.input.data()+t.position,bytes);t.position+=bytes;return bytes;
  }
  static int progress(void *context,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    auto &t=*static_cast<Transfer *>(context);
    return t.session.cancel && g_cancellable_is_cancelled(t.session.cancel);
  }
};
Response transfer(const Url &url,const Session &session,const std::string *body,const std::string &mime) {
  static std::once_flag initialized;
  std::call_once(initialized,[]{if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("Cannot initialize remote file transfers");});
  if(session.cancel && g_cancellable_is_cancelled(session.cancel))throw std::runtime_error("Transfer canceled");
  std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> handle(curl_easy_init(),curl_easy_cleanup);
  if(!handle)throw std::runtime_error("Cannot create remote file transfer");
  auto target=url;bool ssh=url.scheme=="scp" || url.scheme=="sftp" || url.scheme=="ssh";
  if(target.scheme=="ssh")target.scheme="sftp";
  auto uri=target.str();char error[CURL_ERROR_SIZE]{};
  Transfer data{session};if(body)data.input=*body;
  curl_easy_setopt(handle.get(),CURLOPT_URL,uri.c_str());
  curl_easy_setopt(handle.get(),CURLOPT_PROTOCOLS_STR,target.scheme.c_str());
  curl_easy_setopt(handle.get(),CURLOPT_FOLLOWLOCATION,0L);
  curl_easy_setopt(handle.get(),CURLOPT_PROXY,"");
  curl_easy_setopt(handle.get(),CURLOPT_NOSIGNAL,1L);
  curl_easy_setopt(handle.get(),CURLOPT_PATH_AS_IS,1L);
  curl_easy_setopt(handle.get(),CURLOPT_CONNECTTIMEOUT_MS,long(std::max(1,session.timeoutSeconds))*1000);
  curl_easy_setopt(handle.get(),CURLOPT_TIMEOUT_MS,long(std::max(1,session.timeoutSeconds))*1000);
  curl_easy_setopt(handle.get(),CURLOPT_WRITEFUNCTION,Transfer::receive);
  curl_easy_setopt(handle.get(),CURLOPT_WRITEDATA,&data);
  curl_easy_setopt(handle.get(),CURLOPT_XFERINFOFUNCTION,Transfer::progress);
  curl_easy_setopt(handle.get(),CURLOPT_XFERINFODATA,&data);
  curl_easy_setopt(handle.get(),CURLOPT_NOPROGRESS,0L);
  curl_easy_setopt(handle.get(),CURLOPT_ERRORBUFFER,error);
  curl_easy_setopt(handle.get(),CURLOPT_NETRC,long(CURL_NETRC_OPTIONAL));
  if(ssh) {
    const char *home=getenv("HOME");if(!home || !*home)throw std::runtime_error("SSH transfers require HOME and a known_hosts file");
    auto directory=std::filesystem::path(home)/".ssh";
    auto known=(directory/"known_hosts").string();
    if(!std::filesystem::is_regular_file(known))throw std::runtime_error("SSH host trust is missing. Connect with ssh first to populate ~/.ssh/known_hosts");
    curl_easy_setopt(handle.get(),CURLOPT_SSH_KNOWNHOSTS,known.c_str());
    curl_easy_setopt(handle.get(),CURLOPT_SSH_AUTH_TYPES,long(CURLSSH_AUTH_AGENT|CURLSSH_AUTH_PUBLICKEY));
    if(url.user.empty())curl_easy_setopt(handle.get(),CURLOPT_USERNAME,g_get_user_name());
    for(const char *name:{"id_ed25519","id_ecdsa","id_rsa"}) {
      auto key=directory/name;
      if(std::filesystem::is_regular_file(key)){curl_easy_setopt(handle.get(),CURLOPT_SSH_PRIVATE_KEYFILE,key.c_str());break;}
    }
  }
  curl_slist *headers=nullptr;
  std::unique_ptr<curl_slist,decltype(&curl_slist_free_all)> headerOwner(nullptr,curl_slist_free_all);
  if(body) {
    if(body->size()>maxBytes)throw std::runtime_error("Push exceeds 8 MiB");
    curl_easy_setopt(handle.get(),CURLOPT_UPLOAD,1L);
    curl_easy_setopt(handle.get(),CURLOPT_READFUNCTION,Transfer::send);
    curl_easy_setopt(handle.get(),CURLOPT_READDATA,&data);
    curl_easy_setopt(handle.get(),CURLOPT_INFILESIZE_LARGE,curl_off_t(body->size()));
    if(url.scheme=="http" || url.scheme=="https") {
      for(unsigned char c:mime)if(c<32 || c==127)throw std::runtime_error("Invalid upload MIME type");
      headers=curl_slist_append(headers,("Content-Type: "+mime).c_str());
      headers=curl_slist_append(headers,"Expect:");headerOwner.reset(headers);
      curl_easy_setopt(handle.get(),CURLOPT_HTTPHEADER,headers);
    }
  }
  auto code=curl_easy_perform(handle.get());
  long status=0;curl_easy_getinfo(handle.get(),CURLINFO_RESPONSE_CODE,&status);
  if(session.cancel && g_cancellable_is_cancelled(session.cancel))throw std::runtime_error("Transfer canceled");
  if(data.oversized)throw std::runtime_error("Remote document exceeds 8 MiB");
  if(code==CURLE_REMOTE_FILE_NOT_FOUND)return {404,"text/plain","",url.str()};
  if(code!=CURLE_OK)throw std::runtime_error(std::string("Remote transfer failed: ")+(error[0]?error:curl_easy_strerror(code)));
  char *content=nullptr;curl_easy_getinfo(handle.get(),CURLINFO_CONTENT_TYPE,&content);
  return {status?int(status):20,content?content:"text/plain",std::move(data.output),url.str()};
}
}
Response fileFetch(const Url &url,const Session &s) {return transfer(url,s,nullptr,"");}
Published filePush(const Url &read,const Url &target,const std::string &baseline,
                   const std::string &body,const std::string &mime,const Session &session,bool overwrite) {
  auto previous=fileFetch(target,session);
  auto same=read.str()==target.str();
  auto digest=hash(body);
  if(previous.success()) {
    auto before=hash(previous.body);
    if(before==digest)return {target.str(),digest,previous};
    if(!overwrite && (!same || baseline.empty() || before!=baseline))
      throw Conflict("The destination already exists or changed since retrieval");
  } else if(previous.status!=404)throw std::runtime_error("Cannot check the upload destination: "+std::to_string(previous.status));
  else if(!overwrite)throw Conflict("The destination is missing. Create it with these contents?");
  auto sent=transfer(target,session,&body,mime);
  if(!sent.success())throw std::runtime_error("Upload declined: "+std::to_string(sent.status));
  auto confirmed=fileFetch(target,session);
  if(!confirmed.success() || hash(confirmed.body)!=digest)throw std::runtime_error("Upload may have arrived, but its contents could not be verified. Retrieve before retrying");
  return {target.str(),digest,sent};
}
}
