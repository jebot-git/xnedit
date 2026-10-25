// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
using Json=nlohmann::json;
int main(){
 std::shared_ptr<GCancellable> cancel(g_cancellable_new(),[](GCancellable *p){g_object_unref(p);});
 std::thread timer;Json output;
 try {
  std::string line;std::getline(std::cin,line);auto r=Json::parse(line);auto op=r.at("op").get<std::string>();
  if(op=="mime"){output={{"ok",true},{"mime",smallnet::textMime(r.at("meta"))}};}
  else if(op=="identities"){output={{"ok",true},{"identities",smallnet::identities(r.at("store"))}};}
  else if(op=="parse"){auto u=smallnet::parse(r.at("url"));output={{"ok",true},{"url",u.str()},{"peer",u.peer()}};}
  else {
   smallnet::Session s;s.cancel=cancel.get();s.timeoutSeconds=r.value("timeout",5);s.languages=r.value("languages",std::string());s.pins=r.value("pins",std::map<std::string,std::string>{});
   s.identity={r.value("certificate",""),r.value("key",""),r.value("penguin_store",""),r.value("penguin_id","")};
   if(r.value("cancelled",false))g_cancellable_cancel(cancel.get());
   if(r.contains("cancel_ms"))timer=std::thread([cancel,ms=r.at("cancel_ms").get<int>()]{std::this_thread::sleep_for(std::chrono::milliseconds(ms));g_cancellable_cancel(cancel.get());});
   if(op=="fetch"){auto response=smallnet::fetch(r.at("url"),s);output={{"ok",true},{"status",response.status},{"meta",response.meta},{"body",response.body},{"url",response.url},{"author",response.author},{"published",response.published},{"modified",response.modified}};}
   else if(op=="push"){auto published=smallnet::push(r.at("read"),r.at("url"),r.value("baseline",""),r.at("body"),r.value("mime","text/gemini"),r.value("token",""),s,r.value("overwrite",false));output={{"ok",true},{"status",published.response.status},{"meta",published.response.meta},{"digest",published.digest},{"url",published.readUrl},{"verified",published.verified}};}
   else throw std::runtime_error("Unknown test operation");
  }
 }catch(const smallnet::TrustRequired &e){output={{"ok",false},{"kind","trust"},{"peer",e.peer},{"fingerprint",e.fingerprint},{"previous",e.previous}};}
 catch(const smallnet::Conflict &e){output={{"ok",false},{"kind","conflict"},{"error",e.what()}};}
 catch(const std::exception &e){output={{"ok",false},{"kind","error"},{"error",e.what()}};}
 if(timer.joinable())timer.join();std::cout<<output.dump()<<'\n';return 0;
}
