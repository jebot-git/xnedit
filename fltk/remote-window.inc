// SPDX-License-Identifier: GPL-2.0-or-later
// Window-side orchestration; workers only own copied requests and transport state.
void Window::loadRemoteSettings() {
  try {
    if(!fs::exists(configRoot()/"remote.json"))return;
    if(fs::file_size(configRoot()/"remote.json")>1024*1024)throw std::runtime_error("Remote settings exceed 1 MiB");
    Json settings;std::ifstream(configRoot()/"remote.json")>>settings;
    remoteIdentity=identityFromJson(settings.value("identity",Json::object()));
    remotePins=settings.value("pins",std::map<std::string,std::string>{});
    lastRemoteUrl=settings.value("last_url",lastRemoteUrl);
  } catch(const std::exception &){remoteFeedback="Cannot read remote settings; review the identity and server trust before connecting.";}
}
void Window::saveRemoteSettings() {
  fs::create_directories(configRoot());
  atomicWrite(configRoot()/"remote.json",Json{{"identity",identityJson(remoteIdentity)},
    {"pins",remotePins},{"last_url",lastRemoteUrl}}.dump(2),true);
}
void Window::startRemote(RemoteOperation operation) {
  if(remoteJob){remoteFeedback="A transfer is already running.";return;}
  auto job=std::make_unique<RemoteJob>();job->operation=operation;
  smallnet::Session session;session.identity=operation.document.identity;session.pins=remotePins;
  auto cancellation=job->cancel;session.cancel=cancellation.get();
  job->future=std::async(std::launch::async,[operation,session,cancellation]{
    RemoteOutcome result;
    try {
      if(operation.kind==RemoteOperation::Fetch)result.response=smallnet::fetch(operation.document.readUrl,session);
      else result.published=smallnet::push(operation.document.readUrl,operation.document.pushUrl,
        operation.document.baseline,operation.bytes,operation.document.mime,operation.token,session,operation.overwrite);
    } catch(const smallnet::TrustRequired &e){result.kind=RemoteOutcome::Trust;result.peer=e.peer;result.fingerprint=e.fingerprint;result.previous=e.previous;result.subject=e.subject;}
    catch(const smallnet::Conflict &e){result.kind=RemoteOutcome::Changed;result.message=e.what();}
    catch(const std::exception &e){result.kind=RemoteOutcome::Error;result.message=e.what();}
    return result;
  });
  remoteJob=std::move(job);remoteFeedback.clear();
}
void Window::retrieveRemote(const std::string &url,Page *replace,bool readOnly) {
  if(remoteJob && replace){remoteFeedback="A transfer is already running.";return;}
  try {
    auto parsed=smallnet::parse(url);if(!smallnet::canRetrieve(parsed))throw std::runtime_error("This URL is upload-only");
    RemoteOperation operation;operation.document.readUrl=parsed.str();operation.document.pushUrl=smallnet::uploadUrl(parsed);
    operation.document.identity=parsed.scheme!="gemini" && parsed.scheme!="scroll"?smallnet::Identity{}:replace&&replace->remote?replace->remote->identity:remoteIdentity;
    operation.page=replace;operation.readOnly=replace?replace->doc.readOnly:readOnly;
    if(replace){operation.generation=replace->doc.generation();operation.encoding=replace->doc.encoding;operation.eol=replace->doc.eol;operation.bom=replace->doc.bom;}
    lastRemoteUrl=parsed.str();saveRemoteSettings();if(remoteJob)pendingRetrievals.push_back(std::move(operation));else startRemote(std::move(operation));
  }catch(const std::exception &e){report(e.what());}
}
void Window::showRemoteIdentity() {
  if(remoteJob){remoteFeedback="Finish or cancel the transfer before changing its identity.";return;}
  if(auto page=current();page&&page->remote&&smallnet::parse(page->remote->readUrl).scheme!="gemini" && smallnet::parse(page->remote->readUrl).scheme!="scroll") {
    fl_message("SCP/SFTP/SSH use your SSH agent or ~/.ssh/id_ed25519, id_ecdsa or id_rsa, and verified ~/.ssh/known_hosts.\nHTTP(S)/FTP(S) can use a private ~/.netrc. Passwords are not accepted in URLs.\nFinger, Gopher, Nex, nc/TCP and Telnet do not use Gemini client identities.\nTransfers have a 20-second timeout and an 8 MiB limit.");return;
  }
  if(!connectionDialog)connectionDialog=std::make_unique<ConnectionDialog>();
  auto page=current();
  connectionDialog->values(page&&page->remote?page->remote->identity:remoteIdentity);
  connectionDialog->use=[this](smallnet::Identity identity){
    remoteIdentity=identity;
    if(auto page=current();page&&page->remote)page->remote->identity=identity;
    try{saveRemoteSettings();remoteFeedback="Remote identity updated.";}catch(const std::exception &e){report(e.what());}
  };
  connectionDialog->show();
}
void Window::pushRemote(Page *page,bool prompt) {
  if(!page)return;
  if(remoteJob){remoteFeedback="A transfer is already running.";return;}
  if(page->doc.readOnly){remoteFeedback="This document is read only.";return;}
  if(page->doc.encoding!="UTF-8"){report("Remote text editing requires UTF-8. Choose Document → Encoding → UTF-8 before pushing.");return;}
  auto begin=[this,page](std::string uri,std::string mime,std::string token){
    try {
      if(std::find(pages.begin(),pages.end(),page)==pages.end())throw std::runtime_error("The source tab was closed");
      if(page->doc.length()>int(smallnet::maxBytes))throw std::runtime_error("Remote pushes are limited to 8 MiB");
      auto target=smallnet::parse(uri);if(!smallnet::canPush(target))throw std::runtime_error("This protocol retrieves text only. Choose an upload URL such as sftp://, or Save as locally");
      if(target.scheme=="spartan" && page->doc.length()==0)throw std::runtime_error("Empty Spartan documents cannot be uploaded");
      RemoteOperation operation;operation.kind=RemoteOperation::Push;operation.page=page;
      operation.wasRemote=bool(page->remote);
      if(page->remote)operation.document=*page->remote;
      else {operation.document.readUrl=target.reading().str();operation.document.identity=target.scheme!="titan"?smallnet::Identity{}:remoteIdentity;}
      operation.document.pushUrl=target.str();operation.document.mime=mime;
      operation.snapshot=page->doc.text();operation.encoding=page->doc.encoding;operation.eol=page->doc.eol;operation.bom=page->doc.bom;
      operation.bytes=encodeFile(operation.snapshot,operation.encoding,operation.bom,operation.eol);
      page->remoteToken=token;operation.token=std::move(token);startRemote(std::move(operation));
    }catch(const std::exception &e){report(e.what());}
  };
  if(page->remote && (page->remote->pushUrl.empty() || smallnet::streamProtocol(smallnet::parse(page->remote->readUrl))))prompt=true;
  if(prompt || !page->remote) {
    if(!pushDialog)pushDialog=std::make_unique<PushDialog>();
    std::string uri=page->remote?(page->remote->pushUrl.empty()?page->remote->readUrl:page->remote->pushUrl):smallnet::parse(lastRemoteUrl).writing().str();
    pushDialog->values(uri,page->remote?page->remote->mime:"text/gemini");pushDialog->use=begin;pushDialog->show();
  } else begin(page->remote->pushUrl,page->remote->mime,page->remoteToken);
}
void Window::finishRemote() {
  if(!remoteJob || remoteJob->future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
  auto job=std::move(remoteJob);auto operation=job->operation;
  RemoteOutcome result;
  try{result=job->future.get();}catch(const std::exception &e){remoteFeedback=e.what();report(e.what());return;}
  if(g_cancellable_is_cancelled(job->cancel.get())) {
    remoteFeedback=operation.kind==RemoteOperation::Push?"Push canceled; retrieve before retrying because delivery may have occurred.":"Retrieval canceled.";return;
  }
  try {
    if(result.kind==RemoteOutcome::Trust) {
      const char *state=result.previous.empty()?"First connection to this server.":"The server certificate changed.";
      if(fl_choice("%s\n%s\n%s\nSHA-256: %s\n\nTrust this fingerprint and continue?","Cancel","Trust and continue",nullptr,
        state,result.peer.c_str(),result.subject.c_str(),result.fingerprint.c_str())==1){remotePins[result.peer]=result.fingerprint;saveRemoteSettings();startRemote(std::move(operation));}
      else remoteFeedback="Server trust was not granted; no request was sent.";
      return;
    }
    if(result.kind==RemoteOutcome::Changed) {
      if(fl_choice("%s\n\nPush these contents over the remote version?","Cancel","Push anyway",nullptr,result.message.c_str())==1){operation.overwrite=true;startRemote(std::move(operation));}
      else remoteFeedback="Push stopped; remote contents changed.";
      return;
    }
    if(result.kind==RemoteOutcome::Error){remoteFeedback=result.message;report(result.message);return;}
    if(operation.kind==RemoteOperation::Fetch) {
      auto &response=result.response;
      if(!response.success())throw std::runtime_error("The server returned "+std::to_string(response.status)+": "+response.meta);
      std::string mime=smallnet::textMime(response.meta);
      Document decoded;decoded.loadContents(response.body);
      if(decoded.encoding!="UTF-8")throw std::runtime_error("Remote editing currently accepts UTF-8 text documents");
      auto page=operation.page;
      if(page && (page->doc.generation()!=operation.generation || page->doc.encoding!=operation.encoding || page->doc.eol!=operation.eol || page->doc.bom!=operation.bom))page=nullptr;
      if(!page){page=current();if(!page || page->doc.modified() || page->remote || !page->doc.path.empty() || page->doc.length())page=newDocument();}
      auto remote=operation.document;remote.readUrl=response.url;remote.pushUrl=smallnet::uploadUrl(smallnet::parse(response.url));remote.baseline=smallnet::hash(response.body);remote.mime=mime;
      page->remote=remote;page->doc.path.clear();page->doc.readOnly=operation.readOnly;page->doc.encoding="UTF-8";page->doc.loadContents(response.body);page->refresh();
      tabs.value(page);page->show();page->first->take_focus();remoteFeedback="Retrieved "+std::to_string(response.body.size())+" bytes.";
    } else {
      if(!result.published.verified) {
        remoteFeedback="Text sent; file save unconfirmed. Edits remain unsaved.";
        updateTitle();recoverSave();return;
      }
      auto page=operation.page;
      bool same=operation.wasRemote?(page->remote && page->remote->readUrl==operation.document.readUrl):(!page->remote && page->doc.path.empty());
      if(!same){page=newDocument();page->doc.encoding=operation.encoding;page->doc.loadContents(operation.bytes);}
      auto remote=operation.document;remote.readUrl=result.published.readUrl;remote.baseline=result.published.digest;
      page->remote=remote;page->doc.path.clear();page->doc.markSaved(operation.snapshot,operation.encoding,operation.eol,operation.bom);
      page->labelChanged();page->highlight();remoteFeedback="Remote upload confirmed: "+smallnet::parse(remote.readUrl).scheme;
      if(!page->doc.modified() && !page->recovery.empty())fs::remove(page->recovery);
    }
    updateTitle();recoverSave();
  }catch(const std::exception &e){remoteFeedback=e.what();report(e.what());}
}
