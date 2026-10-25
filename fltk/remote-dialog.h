// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu-window.h"
#include "remote.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Secret_Input.H>
#include <FL/Fl_Return_Button.H>

// Only paths and an identity reference are retained. Penguin Mail's private
// keys stay in its own store; Titan tokens stay in memory for this session.
class ConnectionDialog : public dx::GpuWindow {
  Fl_Choice mode{180,35,430,32,"Identity:"};
  Fl_Input certificate{180,90,470,32,"Certificate PEM:"};
  Fl_Button chooseCertificate{660,90,90,32,"Browse..."};
  Fl_Input key{180,135,470,32,"Private key PEM:"};
  Fl_Button chooseKey{660,135,90,32,"Browse..."};
  Fl_Input store{180,190,470,32,"Penguin Mail store:"};
  Fl_Button load{660,190,90,32,"Load"};
  Fl_Choice available{180,235,570,32,"Penguin identity:"};
  Fl_Box help{20,285,730,60,"Choose Anonymous, PEM files, Penguin Mail, or shared Servo Dome identities.\nEncrypted PEM keys are not supported. Private keys are never copied to editor settings."};
  Fl_Return_Button apply{520,365,120,34,"Apply"};
  Fl_Button cancel{650,365,100,34,"Cancel"};
  std::vector<std::pair<std::string,std::string>> records;
  void fill() {
    try {
      records=mode.value()==3?smallnet::sharedIdentities(smallnet::sharedIdentityCatalog()):smallnet::identities(store.value());available.clear();
      for(auto &record:records){std::string label;for(char c:record.second){label+=c;if(c=='@')label+='@';}int item=available.add("identity");available.replace(item,label.c_str());}
      if(!records.empty())available.value(0);
    } catch(const std::exception &e){fl_alert("%s",e.what());}
  }
public:
  std::function<void(smallnet::Identity)> use;
  ConnectionDialog():dx::GpuWindow(780,420,"Remote identity — XNEdit XR") {
    end();set_modal();mode.add("Anonymous|PEM files|Penguin Mail|Shared identities");mode.value(0);
    mode.tooltip("Applies to the current remote document and subsequent retrievals.");
    mode.callback([](Fl_Widget *,void *p){auto s=static_cast<ConnectionDialog *>(p);
      s->available.copy_label(s->mode.value()==3?"Shared identity:":"Penguin identity:");
      if(s->mode.value()==3)s->fill();else {s->records.clear();s->available.clear();}
    },this);
    certificate.tooltip("Public client certificate in PEM format");key.tooltip("Owner-only PEM private key; it may be the same combined PEM file");
    store.tooltip("Penguin Mail misfin/state.json; identity data remains in that private store");
    help.align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);help.box(FL_FLAT_BOX);
    chooseCertificate.callback([](Fl_Widget *,void *p){auto s=static_cast<ConnectionDialog *>(p);Fl_Native_File_Chooser f;f.title("Identity certificate PEM");if(!f.show())s->certificate.value(f.filename());},this);
    chooseKey.callback([](Fl_Widget *,void *p){auto s=static_cast<ConnectionDialog *>(p);Fl_Native_File_Chooser f;f.title("Identity private key PEM");if(!f.show())s->key.value(f.filename());},this);
    load.callback([](Fl_Widget *,void *p){static_cast<ConnectionDialog *>(p)->fill();},this);
    apply.callback([](Fl_Widget *,void *p){auto s=static_cast<ConnectionDialog *>(p);smallnet::Identity identity;
      if(s->mode.value()==1){identity.certificate=s->certificate.value();identity.key=s->key.value();if(identity.certificate.empty()||identity.key.empty()){fl_alert("Choose both certificate and key files.");return;}}
      if(s->mode.value()==2){int n=s->available.value();if(n<0 || n>=int(s->records.size())){fl_alert("Load and choose a Penguin Mail identity.");return;}identity.penguinStore=s->store.value();identity.penguinId=s->records[n].first;}
      if(s->mode.value()==3){int n=s->available.value();if(n<0||n>=int(s->records.size())){fl_alert("Load and choose a shared identity.");return;}
        try{identity=smallnet::adoptSharedIdentity(smallnet::sharedIdentityCatalog(),s->records[n].first);}
        catch(const std::exception &e){fl_alert("%s",e.what());return;}}
      s->hide();if(s->use)s->use(identity);
    },this);
    cancel.callback([](Fl_Widget *,void *p){static_cast<ConnectionDialog *>(p)->hide();},this);
    callback([](Fl_Widget *w,void *){w->hide();});dx::style(this);
  }
  void values(const smallnet::Identity &identity) {
    certificate.value(identity.certificate.c_str());key.value(identity.key.c_str());
    const char *data=getenv("XDG_DATA_HOME"),*home=getenv("HOME");
    std::string path=data?data:std::string(home?home:"/tmp")+"/.local/share";
    store.value((identity.penguinStore.empty()?path+"/penguin-mail/misfin/state.json":identity.penguinStore).c_str());
    mode.value(!identity.penguinId.empty()?2:!identity.certificate.empty()?1:0);
    available.clear();records.clear();
    if(!identity.penguinId.empty()){fill();for(size_t i=0;i<records.size();++i)if(records[i].first==identity.penguinId)available.value(i);}
  }
};
class PushDialog : public dx::GpuWindow {
  Fl_Input url{130,35,600,32,"Upload URL:"};
  Fl_Input mime{130,90,600,32,"MIME type:"};
  Fl_Secret_Input token{130,145,600,32,"Token:"};
  Fl_Box help{20,195,710,58,"The token is optional when the server authorizes your client certificate.\nTokens are not saved in settings or recovery copies."};
  void protocol() {
    bool titan=g_ascii_strncasecmp(url.value(),"titan://",8)==0;
    bool spartan=g_ascii_strncasecmp(url.value(),"spartan://",10)==0;
    bool stream=g_ascii_strncasecmp(url.value(),"nc://",5)==0 || g_ascii_strncasecmp(url.value(),"tcp://",6)==0 || g_ascii_strncasecmp(url.value(),"telnet://",9)==0;
    if(titan){mime.activate();token.activate();help.copy_label("The token is optional when the server authorizes your client certificate.\nTokens are not saved in settings or recovery copies.");}
    else {
      token.value("");token.deactivate();
      if(spartan || stream)mime.deactivate();else mime.activate();
      help.copy_label(g_ascii_strncasecmp(url.value(),"finger://",9)==0 || g_ascii_strncasecmp(url.value(),"gopher://",9)==0 || g_ascii_strncasecmp(url.value(),"nex://",6)==0 || g_ascii_strncasecmp(url.value(),"gopher+://",10)==0 || g_ascii_strncasecmp(url.value(),"scroll://",9)==0?"This protocol only retrieves text. Choose a file upload URL, such as sftp://,\nor use Save as to write a local copy.":stream?"Send these document bytes to the service. This is a byte stream, not a file save.\nThe server must close its response; edits remain unsaved after sending.":spartan?"Spartan sends the document directly without MIME or token fields.\nEmpty documents cannot be uploaded.":"File uploads check the destination and read it back to verify the saved bytes.\nSCP/SFTP use SSH keys or agent and known_hosts; HTTP/FTP can use ~/.netrc.");
    }
    redraw();
  }
  Fl_Return_Button push{455,275,160,34,"Check and push"};
  Fl_Button cancel{625,275,105,34,"Cancel"};
public:
  std::function<void(std::string,std::string,std::string)> use;
  PushDialog():dx::GpuWindow(760,330,"Push remote document — XNEdit XR") {
    end();set_modal();url.when(FL_WHEN_CHANGED);url.callback([](Fl_Widget *,void *p){static_cast<PushDialog *>(p)->protocol();},this);help.align(FL_ALIGN_LEFT|FL_ALIGN_INSIDE);help.box(FL_FLAT_BOX);
    push.callback([](Fl_Widget *,void *p){auto s=static_cast<PushDialog *>(p);auto uri=std::string(s->url.value()),type=std::string(s->mime.value()),secret=std::string(s->token.value());s->token.value("");s->hide();if(s->use)s->use(uri,type,secret);},this);
    cancel.callback([](Fl_Widget *,void *p){auto s=static_cast<PushDialog *>(p);s->token.value("");s->hide();},this);
    callback([](Fl_Widget *w,void *){auto s=static_cast<PushDialog *>(w);s->token.value("");s->hide();});dx::style(this);
  }
  void values(const std::string &uri,const std::string &type){url.value(uri.c_str());mime.value(type.c_str());token.value("");protocol();}
};
