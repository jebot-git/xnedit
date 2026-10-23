// SPDX-License-Identifier: GPL-2.0-or-later
#include "search.h"
#include <cassert>
#include <iostream>
int main() {
  Document doc;doc.assign("one\ntwo",true);
  assert(doc.find("$",doc.length(),true,true,false)->start==doc.length());
  assert(doc.find("^one",doc.length(),true,true,true)->start==0);
  assert(!doc.find("one",0,false,true,true));
  assert(!doc.find("one",0,false,false,true));
  assert(!doc.find("one",0,true,true,true));
  SearchCursor cursor;
  auto first=cursor.next(doc,"^",0,true,true,false);
  auto second=cursor.next(doc,"^",first->match.start,true,true,false);
  assert(first->match.start==0 && second->match.start==4 && !second->wrapped);
  auto wrapped=cursor.next(doc,"^",second->match.start,true,true,false);
  assert(wrapped->wrapped && wrapped->match.start==0);
  auto previous=cursor.next(doc,"^",4,true,true,true);
  assert(previous->match.start==0 && !previous->wrapped);
  previous=cursor.next(doc,"^",0,true,true,true);
  assert(previous->wrapped && previous->match.start==4);
  doc.assign("é日z",true);SearchCursor unicode;
  auto a=unicode.next(doc,"(?=.)",0,true,true,false);
  auto b=unicode.next(doc,"(?=.)",a->match.start,true,true,false);
  auto c=unicode.next(doc,"(?=.)",b->match.start,true,true,false);
  assert(a->match.start==0 && b->match.start==2 && c->match.start==5);
  SearchCursor eof;
  auto end=eof.next(doc,"$",0,true,true,false);assert(end->match.start==6);
  end=eof.next(doc,"$",6,true,true,false);assert(end->wrapped && end->match.start==6);
  for(auto &example:regexExamples()) {
    Document sample;sample.assign(example.sample,true);
    auto match=sample.find(example.pattern,0,true,true,false,example.replacement);
    if(!match){std::cerr<<"Missing example match: "<<example.label<<'\n';std::abort();}
  }
  doc.assign("Doe, Jane\nDoe, John",true);
  assert(doc.replaceAll(R"((\w+), (\w+))",R"(\2 \1)",true,true)==2);
  assert(doc.text()=="Jane Doe\nJohn Doe");doc.undo();assert(doc.text()=="Doe, Jane\nDoe, John");
  doc.assign("a\nb",true);assert(doc.replaceAll("$",";",true,true)==2);assert(doc.text()=="a;\nb;");
  doc.assign("abc",true);assert(doc.replaceAll("(?=.)",">",true,true)==3);assert(doc.text()==">a>b>c");
  doc.assign("a a",true);auto old=doc.text();bool invalid=false;
  try {doc.replaceAll("[","x",true,true);}catch(...){invalid=true;}
  assert(invalid && doc.text()==old && !doc.modified());
  assert(doc.replaceAll("a","&\\1",false,true)==2);assert(doc.text()=="&\\1 &\\1");
  std::cout<<"PASS forward/backward and wrapped search, UTF-8 zero-width progress, EOF anchors, "
             "capture replacement, literal mode, invalid patterns, undo and "<<regexExamples().size()<<" helper examples\n";
}
